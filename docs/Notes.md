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