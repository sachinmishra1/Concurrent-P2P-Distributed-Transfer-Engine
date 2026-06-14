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
- **Security & Blacklisting**: Maintains a blacklist of misbehaving, corrupt, or duplicate peer addresses. Blacklisted peers are prevented from reconnecting, and any existing connection is severed immediately.
- **Reference Cycle Avoidance**: Registers event hooks (such as handshake and disconnection callbacks) using `std::weak_ptr<PeerConnection>` captures to prevent reference cycles and ensure deterministic resource reclamation when a connection is dropped.