# Project 

**Name** - Concurrent P2P Distributed Transfer Engine

**Language** - C++ 20

## Description

A BitTorrent-compliant peer-to-peer file transfer client. The engine connects to the BitTorrent swarm, discovers peers via trackers, manages concurrent TCP connections, downloads file pieces, validates data integrity with SHA-1, and assembles the final file(s) on disk.

## Terminologies
- **.torrent (Torrent File):** small metadata file(a few kb mostly).
- **Piece:** fixed size chunk of file (except last piece obviously).
- **Block (sub-piece):** pieces are further divided into blocks for wire transfer.
- **Peer:** any client participating in a torrent (upload/download)
- **Seeder:** Peer that has all pieces.
- **Leecher:** Peer that does not have all pieces yet - still downloading
- **Swarm:** Entire set of peers(seeders + leechers) for a given torrent
- **Tracker:** Server that keeps track of which peers are participating in a torrent.

## Bencode Encoding Format

Serialisation format used by bittorrent. It encodes .torrent files and tracker responses.

- simple
- 4 data types - int, string, list, dict

### Integer
- Format: i<number>e
- Example: 
    - i40e -> 40
    - i0e  -> 0
    - i-7e -> -7
- Rules:
    - No leading zero. i03e is INVALID, i00e is INVALID.
    - i0e is only way to encode zero
    - i-0e is INVALID
    - no size limit

### Byte String
- Format: <length>:<data>
- Example:
    - 5:crazy -> "crazy"
    - 0: -> "" (empty string)
    - 11:hello world -> "hello world"
- Rules:
    - Length is a decimal number (ASCII digits)
    - No leading zeros in length (except "0:" for empty string)
    - Data is raw bytes, not necessarily UTF-8
    - The "string" can contain arbitrary binary data (SHA-1 hashes, etc.)

### List:
- Format: l<items>e
- Example:
    - le             → [] (empty list)
    - li40ee         → [40]
    - li1ei2ei3ee    → [1, 2, 3]
    - l4:spam4:eggse → ["spam", "eggs"]
    - li1el4:spamee  → [1, ["spam"]]  (nested)
- Rules:
    - Items can be any Bencode type (including nested lists/dicts)
    - No separator between items

### Dictionary
- Format: d<key><value><key><value>...e
- Example:
    - de                         → {} (empty dict)
    - d3:cow3:moo4:spam4:eggse   → {"cow": "moo", "spam": "eggs"}
    - d4:infod4:name5:helloee    → {"info": {"name": "hello"}}  (nested)
- Rules:
    - Keys MUST be byte strings (not integers, lists, or dicts)
    - Keys MUST appear in sorted (lexicographic) order
    - Values can be any Bencode type
    - No duplicate keys

## Torrent File Structure & Metadata

A `.torrent` file is a Bencoded dictionary. The structure of this dictionary differs slightly depending on whether it describes a single-file torrent or a multi-file torrent.

### Common Keys

1. `announce` (string): The URL of the tracker.
2. `info` (dictionary): A dictionary containing metadata about the file(s) in the torrent.

### The `info` Dictionary

All fields within the `info` dictionary are used to calculate the torrent's **info_hash**.

#### Fields present in both Single-File and Multi-File Torrents:
- `name` (string): 
  - For single-file: the suggested filename.
  - For multi-file: the suggested directory name to store the files.
- `piece length` (integer): Number of bytes in each piece (typically a power of 2, e.g., 262144).
- `pieces` (string): A concatenated string of 20-byte SHA-1 hashes, one for each piece.

#### Fields for Single-File Torrents:
- `length` (integer): Size of the file in bytes.

#### Fields for Multi-File Torrents:
- `files` (list of dictionaries): A list of files, where each dictionary contains:
  - `length` (integer): Size of the file in bytes.
  - `path` (list of strings): A list of subdirectories and the filename (e.g., `["dir1", "file.txt"]` maps to `dir1/file.txt`).

### Info Hash Computation

The **info_hash** is a unique identifier for a torrent. It is calculated by taking the 20-byte SHA-1 hash of the bencoded `info` dictionary value *exactly as it appears* in the `.torrent` file. 

#### Critical Implementation Details
- **Raw Bytes Requirement**: Re-encoding a parsed dictionary value to Bencode is not guaranteed to produce identical bytes due to potential minor variations in formatting (e.g., key order, spacing, integer padding). Therefore, the exact raw byte range of the `info` dictionary value must be captured from the incoming `.torrent` file buffer during the parsing phase.
- **SHA-1 Hashing**: A standard one-shot SHA-1 hash is computed over the captured raw bytes (starting with `d` and ending with the matching `e` of the `info` dictionary) using a cryptographic hashing library (e.g., Crypto++ `SHA1`).  

## Non-Blocking TCP Sockets in Linux

BitTorrent networks require handling multiple concurrent connections to peers. To do this efficiently in a single thread, non-blocking I/O and event loops are utilized.

### Non-Blocking Socket Operations
- **O_NONBLOCK flag**: Configures a socket descriptor so that socket operations return immediately instead of blocking execution.
- **Asynchronous connect (`connect`)**:
  - Initiating a connection on a non-blocking socket returns `-1` with `errno` set to `EINPROGRESS`.
  - The connection process happens in the background. Once it finishes successfully, the socket becomes writable in `epoll`.
  - To verify the connection status, check the socket option `SO_ERROR` using `getsockopt`.
- **Non-blocking read (`recv`)**:
  - Returns `-1` with `errno` set to `EAGAIN` or `EWOULDBLOCK` if there is no data in the system buffer to read.
  - Returns `0` if the peer has closed the connection.
- **Non-blocking write (`send`)**:
  - Returns `-1` with `errno` set to `EAGAIN` or `EWOULDBLOCK` if the system send buffer is full.
  - Passing the flag `MSG_NOSIGNAL` is critical. It prevents the process from receiving a `SIGPIPE` signal (which crashes the application by default) if the peer closed the connection; instead, `send` safely returns `-1` with `errno = EPIPE`.

### Lifetime & Ownership (RAII)
- Sockets manage a scarce system resource (file descriptors). 
- To avoid leaks, the socket descriptor must be safely closed via `close()` inside the destructor.
- Sockets must be **move-only** to ensure there is exactly one owner of a file descriptor.

## Asynchronous Event Multiplexing with epoll

To scale to thousands of concurrent peer connections without incurring the overhead of thread-per-connection scheduling, Linux provides `epoll`, an $O(1)$ scaling event notification interface.

### Epoll Core Mechanics
- **`epoll_create1(int flags)`**: Instantiates an epoll control structure. The `EPOLL_CLOEXEC` flag is used to prevent the file descriptor from leaking across program executions.
- **`epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)`**: Registers interest in events (read `EPOLLIN`, write `EPOLLOUT`, errors `EPOLLERR` or peer connection drops `EPOLLRDHUP`/`EPOLLHUP`) on a specific file descriptor.
- **`epoll_wait(...)`**: Blocks the thread until registered file descriptors report events or a timeout occurs.

### Timer Min-Heap Design
- **Deadline Sorting**: Timers are stored in a min-priority queue (`std::priority_queue` with `std::greater`) based on their absolute expiration time (e.g. using `std::chrono::steady_clock`).
- **Dynamic Epoll Timeouts**:
  - In each iteration of the event loop, the time remaining until the next scheduled timer expiration is calculated.
  - This delta is passed as the `timeout` parameter to `epoll_wait`.
  - This ensures that if no socket events arrive, `epoll_wait` wakes up exactly at the time the next timer is scheduled to fire.
- **Deferred Deletion (Cancellation)**: To allow $O(1)$ timer cancellations, cancelled timer IDs are added to a hash set. When a timer is popped from the heap, if its ID is in the cancelled set, it is silently discarded without triggering its callback.

## Tracker Protocol and Announce Mechanics

The BitTorrent tracker is an HTTP server that keeps track of the peers participating in the torrent's swarm.

### HTTP GET Announce Request
To retrieve the list of peers, the client sends an HTTP GET request to the torrent's `announce` URL. The query parameters are:
- `info_hash`: 20-byte SHA-1 hash of the `info` dictionary, URL-encoded.
- `peer_id`: 20-byte unique ID of the client, URL-encoded.
- `port`: The TCP port the client is listening on (typically 6881-6889).
- `uploaded`: Number of bytes uploaded since the client sent the event.
- `downloaded`: Number of bytes downloaded.
- `left`: Number of bytes remaining to download.
- `compact`: Setting `compact=1` tells the tracker to return the peer list in compact format (preferred for reduced network bandwidth).
- `event`: (Optional) `started`, `completed`, `stopped`.

### URL Encoding Raw Binary Data
Since `info_hash` and `peer_id` are raw 20-byte digests, characters outside the RFC 3986 unreserved set (`a-z`, `A-Z`, `0-9`, `-`, `_`, `.`, `~`) must be percent-encoded as `%XX`, where `XX` is the hexadecimal representation of the byte.

### Tracker Response Formatting
The response is Bencoded.
- **Failure**: If the dictionary contains `failure reason`, it indicates the request failed.
- **Compact Peers Format**: The `peers` key contains a single string of length multiple of 6. Each 6-byte group represents one peer:
  - 4 bytes: IPv4 Address (network byte order).
  - 2 bytes: Port number (network byte order).
- **List Peers Format**: The `peers` key contains a list of dictionaries, each having keys `ip`, `port`, and optionally `peer id`.

## Peer ID Generation
Clients generate a unique 20-byte identifier at startup. The standard convention is the Azureus-style format:
- **Prefix**: 8 bytes representing the client identifier (e.g., `-DT0001-` for this client).
- **Suffix**: 12 random alphanumeric characters to guarantee uniqueness per run.

## Peer Wire Protocol Message Serialization

Peers communicate over TCP using a custom binary wire protocol consisting of standard length-prefixed messages and a special connection handshake.

### Handshake Message
The handshake is a fixed-length 68-byte message containing:
- `pstrlen` (1 byte): Length of the protocol identifier string. Must be 19 (`0x13`).
- `pstr` (19 bytes): The protocol identifier string. Must be `"BitTorrent protocol"`.
- `reserved` (8 bytes): Extension protocol flags (all set to `0x00` by default).
- `info_hash` (20 bytes): SHA-1 digest of the torrent's `info` dictionary.
- `peer_id` (20 bytes): Unique identifier of the sending peer.

### Wire Messages
All other messages follow a 4-byte length-prefixed structure:
- `Length` (4 bytes): Big-endian integer specifying the remaining message size in bytes (excluding these 4 prefix bytes).
- `Message ID` (1 byte, omitted if length = 0): Indicates the type of message.
- `Payload` (Variable length): Type-specific parameters.

Supported wire messages:
1. **Keep-Alive** (length = 0): Omit ID and payload. Prevents connection timeouts.
2. **Choke** (length = 1, ID = 0): Informs the remote peer that we will not service their requests.
3. **Unchoke** (length = 1, ID = 1): Informs the remote peer they can start sending requests.
4. **Interested** (length = 1, ID = 2): Declares interest in downloading pieces from the peer.
5. **Not Interested** (length = 1, ID = 3): Declares lack of interest in the peer's pieces.
6. **Have** (length = 5, ID = 4): Payload is a 4-byte big-endian piece index that has been downloaded and verified.
7. **Bitfield** (length = 1 + X, ID = 5): Payload is a compact bitmask representing all pieces currently held.
8. **Request** (length = 13, ID = 6): Asks for a block of data. Payload contains:
   - 4-byte piece index (big-endian)
   - 4-byte block byte-offset (big-endian)
   - 4-byte block length (big-endian)
9. **Piece** (length = 9 + X, ID = 7): Delivers block payload. Contains:
   - 4-byte piece index (big-endian)
   - 4-byte block byte-offset (big-endian)
   - X bytes of block data
10. **Cancel** (length = 13, ID = 8): Cancels a pending request. Payload contains index, begin, and length (same as Request).
11. **Port** (length = 3, ID = 9): DHT port number (2 bytes).

### Big-Endian Conversion
Since network communications use network byte order (big-endian), multi-byte integers (such as lengths, indices, offsets, and ports) must be converted using standard helpers (`ntohl`, `htonl`, `ntohs`, `htons`) to ensure compatibility with other clients running on varying system architectures (e.g. little-endian x86/ARM).

## Peer Connection State Machine

In BitTorrent, each peer connection involves active state management comprising TCP-level state tracking and application-level choking/interest states.

### Connection States
- **Idle**: The socket is not yet allocated or connected.
- **Connecting**: The asynchronous TCP handshake (`connect_async`) is underway. Interest is registered on `EPOLLOUT` to catch completion.
- **Handshaking**: The TCP link is active. The client transmits its 68-byte handshake and awaits the remote peer's handshake response.
- **Active**: Handshakes match. The client can now exchange standard wire messages.
- **Disconnected**: The socket has been closed (either via local shutdown, remote closure `EPOLLRDHUP`/0-byte `recv`, or socket error).

### Choking and Interest Flags
Each side of the active connection maintains two independent state flags, resulting in four flags total per connection:
1. `am_choking` (default: `true`): We are choking the peer. We will not service their `Request` messages.
2. `am_interested` (default: `false`): We are interested in pieces this peer has. We will send `Request` messages once they unchoke us.
3. `peer_choking` (default: `true`): The peer is choking us. We cannot request data from them.
4. `peer_interested` (default: `false`): The peer is interested in our pieces. They will request blocks once we unchoke them.

### Buffer Management and Non-blocking I/O
- **Receive Buffering**: Received bytes are appended to a contiguous dynamic buffer (`rx_buffer`). The parser drains complete handshakes and wire messages from the front of the buffer. If only partial data is received, parsing defers until more data arrives.
- **Transmit Queue & Dynamic Epoll**: Outgoing messages are serialized into `tx_buffer`. A non-blocking `send` loop drains the buffer. If the send queue is fully drained, the event loop removes the `EPOLLOUT` flag to avoid busy-waiting. If the socket blocks (`EAGAIN`/`EWOULDBLOCK`), the remaining data remains in `tx_buffer` and `EPOLLOUT` monitoring remains active.
- **Lifetime Safety**: To ensure callback safety and prevent use-after-free bugs when socket events fire asynchronously after a peer connection is destroyed, lambda callbacks capture a `std::weak_ptr<PeerConnection>` which is checked/locked before processing events.

## Bitfield Representation

The Bitfield message is used by a BitTorrent client to inform remote peers of the pieces it has downloaded and verified.

### Byte and Bit Ordering (MSB-First)
- A bitfield is represented as a sequence of bytes.
- Each byte contains 8 bits, corresponding to 8 pieces in ascending order.
- The highest-order bit (Most Significant Bit - MSB, `0x80`) of the first byte (index 0) represents piece 0.
- The lowest-order bit (Least Significant Bit - LSB, `0x01`) of the first byte represents piece 7.
- Piece 8 starts at the MSB of the second byte (index 1), and so on.

### Trailing Padding Bits
- If the total number of pieces in a torrent is not a multiple of 8, the remaining bits in the last byte are unused/padding.
- For example, if a torrent contains 10 pieces, the bitfield size is 2 bytes (16 bits total). The last 6 bits of the second byte are padding bits.
- To prevent garbage bytes from leaking into protocol exchanges or causing checksum mismatch, any padding bits in the last byte must be explicitly masked out to `0`.

## Peer Manager

The `PeerManager` is responsible for orchestrating the lifecycle of all peer connections in the client.

### Core Responsibilities
- **Peer List Intake**: Processes `PeerInfo` records returned by the tracker client.
- **Connection Rate Limiting**: Maintains a hard cap on active connections (defaulting to 128) to prevent socket depletion and process exhaustion.
- **Lifetime Tracking**: Safely tracks and handles connecting, active, and disconnected peers.
- **Security & Blacklisting**: Maintains a blacklist of misbehaving, corrupt, or duplicate peer addresses. If a piece fails SHA-1 validation, the `DownloadCoordinator` triggers an `on_peer_corrupted` callback to the main orchestrator, which instantly blacklists the offending peer in the `PeerManager`, drops the TCP connection, and blocks any future connections from that IP.
- **Reference Cycle Avoidance**: Registers event hooks (such as handshake and disconnection callbacks) using `std::weak_ptr<PeerConnection>` captures to prevent reference cycles and ensure deterministic resource reclamation when a connection is dropped.

## Piece Manager

The `PieceManager` manages download progression at the block and piece granularity, ensuring byte buffer integrity.

### Block-Level Chunking
- Standard protocol dictates that pieces are requested and transferred in smaller blocks (typically 16 KiB / 16384 bytes).
- The `PieceManager` decomposes pieces into blocks, handling edge cases where the piece size (e.g. the final piece of the torrent or a non-multiple of 16 KiB) is not a multiple of block size.
- Individual blocks track their state: `Pending`, `Requested`, or `Received`.

### Piece Assembly and Validation
- Received block data is copied directly into an allocated contiguous buffer corresponding to the target piece.
- When all blocks are successfully received, the piece is marked complete and its data buffer is validated by hashing it via the `SHA1Hasher` and comparing the result to the 20-byte torrent metadata hash.
- **Success Handling**: On hash matching, the client sets the corresponding bit in its local `Bitfield` and clears the temporary block-tracking buffer.
- **Failure Handling**: On mismatch, the piece is discarded, and all blocks are returned to `Pending` so that they can be re-downloaded from alternate peers.

## Piece Picker (Rarest-First)

The `PiecePicker` controls piece selection strategies, maximizing block distribution efficiency and minimizing download bottleneck risk.

### Availability Histogram
- To identify which pieces are the rarest in the swarm, the `PiecePicker` maintains an availability histogram using a thread-safe `std::vector<std::atomic<uint32_t>>`.
- When a peer connection completes handshaking and sends a `bitfield` (or subsequently sends `have` messages), the picker increments the availability count of the matching pieces.
- Symmetrically, when a peer disconnects, its bitfield contribution is subtracted from the histogram, ensuring the tracking data accurately reflects active swarm status.

### Bootstrap Selection Rule (First 4 Pieces)
- During startup, the client has completed very few pieces (< 4).
- If rarest-first were enforced during this initial phase, the client would request the absolute rarest pieces from the few peers that have them. These peers might be slow or choked, stalling the bootstrap.
- To bootstrap quickly, the picker selects random pieces among those available from the peer. Obtaining a few random pieces quickly allows the client to unchoke itself and start exchanging pieces sooner.

### Rarest-First Selection Rule
- Once the bootstrap phase is complete (>= 4 pieces obtained), the client switches to strict rarest-first.
- It finds candidate piece indices that the peer possesses and the client needs, then filters for those with the lowest positive value in the availability histogram.
- If multiple candidates tie for lowest availability, the picker chooses one randomly. This load-balances requests across different pieces to avoid multiple connections attempting to fetch the exact same blocks.

## Download Coordinator (Request Pipeline)

The `DownloadCoordinator` integrates peer connections, the piece manager, and the piece picker to drive the asynchronous download loop.

### Asynchronous Pipelining
- To hide network latency, the coordinator pipelines up to 5 concurrent block requests per unchoked peer.
- The `fill_request_pipeline` helper keeps sending block request messages (`REQUEST`) to a peer until 5 requests are outstanding.
- Request selection is driven by asking the `PiecePicker` for a piece, then extracting the next unrequested block from `PieceManager`.

### Message Handling & Error Recovery
- **Unchoke**: Triggers the request filling loop to kick off downloading from the peer.
- **Choke**: Erases the peer's outstanding requests from the coordinator, and flags their corresponding blocks as `Pending` in the `PieceManager` so they can be re-allocated to other peers.
- **Bitfield/Have**: Updates the piece availability histogram in `PiecePicker` and updates our interest status (`INTERESTED` message).
- **Piece**: Receives a block payload, writes it to the piece manager, and tops off the pipeline.
- **Cancel on Completion**: When a piece finishes validation successfully, the coordinator broadcasts a `CANCEL` message for all pending blocks of that piece to all peers, preventing duplicate downloads and freeing up request pipeline capacity.

## Disk I/O Manager

The `DiskIOManager` handles the physical storage layout on disk, managing file system paths, allocations, file-span mapping, and raw piece read/writes.

### Directory and File Pre-allocation
- Before beginning a download, BitTorrent clients pre-allocate all output files to their expected sizes. This:
  - Ensures sufficient disk space is available upfront, preventing middle-of-download write failures.
  - Minimizes filesystem fragmentation, optimizing sequential write throughput.
- We implement this by recursively creating parent directories via `std::filesystem::create_directories` and using C++17 `std::filesystem::resize_file` to instantly size files to their exact bytes.

### Piece-to-File Boundary Mapping
- In BitTorrent, the files in a multi-file torrent are treated as a single concatenated virtual byte space.
- A single piece index represents a contiguous range of bytes in this virtual space:
  - `global_offset = piece_idx * piece_length`
- When a piece is written, it may span across file boundaries.
- To handle this, `DiskIOManager` calculates the overlap between the piece's range `[global_offset, global_offset + piece_size)` and each file's virtual range `[file.offset, file.offset + file.length)`:
  - If they overlap, it calculates the relative offsets (`offset_in_file` and `offset_in_data`) and writes/reads only that specific overlapping slice using binary file streams (`std::fstream`).
  - This robust design supports single-file torrents, normal multi-file torrents, and pieces spanning multiple files.

### SHA-1 Re-verification
- To guarantee that files written to disk are free of corruptions (e.g. from disk write errors or bad memory sectors), the `DiskIOManager` provides a `verify_piece` method.
- This reads the piece data back from the filesystem, hashes it using `SHA1Hasher`, and verifies it matches the original torrent metadata hash.

## CLI Argument Parsing

The CLI argument parser configures application parameters from command-line arguments.

### Configuration Parameters
- **Torrent File**: Positional argument specifying the path to the `.torrent` metadata file.
- **Output Directory**: `--output-dir=<dir>` option where downloaded files will be stored (defaults to `.`).
- **Log Level**: `--log-level=<level>` setting logging severity (`debug`, `info`, `warn`, `error`, defaults to `info`).
- **Max Peers**: `--max-peers=<n>` defining maximum concurrent peer connections (defaults to `50`).
- **Help**: `--help` or `-h` displaying option usage instructions.

### Parser Robustness & Validations
- **Prefix Matching**: Correctly parses key-value pairs formatted with an `=` sign (e.g. `--max-peers=128`).
- **Uniqueness**: Ensures that exactly one positional torrent file is specified.
- **Strict Typing**: Validates that `--max-peers` contains a valid positive integer.
- **File Verification**: Verifies that the targeted torrent file actually exists on the filesystem prior to running.

## Main Download Orchestration

The `main.cpp` entrypoint wires all components together into a single, cohesive executable.

### Azureus-style Peer ID Generation
- We generate a unique 20-byte Peer ID consisting of a client prefix (`-DT0001-`) and 12 random characters selected from an alphanumeric alphabet.
- This conforms to the unofficial BitTorrent client standard, enabling trackers and other clients to identify this engine.

### Signal Handling and Graceful Shutdown
- To handle `Ctrl+C` (`SIGINT`) gracefully:
  - We register a signal handler that sets a volatile `sig_atomic_t` flag.
  - A recurring 250ms timer is registered on the `EventLoop`. When the timer fires and detects the shutdown flag, it stops the event loop using `EventLoop::shutdown()`.
  - This guarantees that sockets, files, and resources are closed and cleaned up cleanly without leaving corrupted states or leaked descriptors.

## Progress Display and Statistics

Real-time feedback is printed to `stdout` in-place.

### Dynamic Rendering
- We print update lines using the carriage return (`\r`) character without a trailing newline (`\n`). This resets the cursor to the beginning of the current console line, allowing us to overwrite it on the next update.
- We flush `stdout` using `std::fflush(stdout)` to ensure the line is immediately rendered on terminals even without newline feeds.

### Statistic Calculations
1. **Download Progress & Speed**:
   - We query `PieceManager::bitfield()` to count the number of completed pieces.
   - We calculate downloaded bytes by summing the size of completed pieces.
   - Every second, we measure the delta between the current downloaded bytes and the previous second's downloaded bytes, giving us the current download speed (in MB/s).
2. **Estimated Time to Completion (ETA)**:
   - Remaining bytes is calculated as `total_length - downloaded_bytes`.
   - Dividing remaining bytes by the current speed yields the remaining seconds, formatted as `Xm Ys`.
3. **Progress Bar**:
   - A 20-character progress bar dynamically fills based on the completion percentage. Completed portions are drawn using `█`, and remaining portions are drawn using `░`.

## Command Line Custom Peer Override

To facilitate local testing, debugging, and offline operations, the client supports manually specifying a peer connection.

### Custom Peer Argument
- **Option**: `--peer=<ip>:<port>` (e.g. `--peer=127.0.0.1:6881`).
- **Behavior**:
  - If specified, the engine schedules an outgoing TCP connection to the designated peer directly via `PeerManager::connect_to_peer`.
  - When the primary tracker is unreachable or returns a failure response, the engine ignores the tracker failure and proceeds to download from the manually specified peer instead of exiting.
  - The progress bar displays the count of fully established/active peer connections (i.e. where the BitTorrent handshake has succeeded) rather than just connecting/idle peer connection objects.

## Local End-to-End Testing Harness

An automated end-to-end testing script `tests/e2e_seeder.py` verifies the entire pipeline:
1. **Dynamic Torrent Creation**: Automatically generates a 1MB test file containing random bytes, partitions it into 4 pieces of 256KB, computes their SHA-1 hashes, and outputs a valid bencoded `.torrent` file.
2. **Mock Seeder Protocol Server**: Hosts a local TCP server that implements the BitTorrent peer wire protocol:
  - Completes the handshake and validates the client's `info_hash`.
  - Sends a Bitfield message indicating it has 100% of the pieces (`\xf0`).
  - Sends an Unchoke message to allow the client to request data.
  - Responds to incoming block `request` messages with the correct chunk of bytes.
  - Processes client `HAVE` messages.
3. **Execution & Validation**: Invokes the C++ engine binary using Python's `subprocess` API, verifies that the download is completed, runs a SHA-1 validation check comparing the output file to the original, and prints the result before cleaning up temporary files.