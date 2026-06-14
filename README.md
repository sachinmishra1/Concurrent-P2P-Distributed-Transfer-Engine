# Concurrent P2P Distributed Transfer Engine

A high-performance, asynchronous, single-threaded BitTorrent-compliant peer-to-peer file transfer engine written in modern **C++20**. 

The engine uses a non-blocking I/O event loop multiplexed via Linux `epoll` combined with a lock-free piece scheduler to efficiently manage multiple peer connections and write downloaded blocks directly to pre-allocated disk layouts with zero-copy stream views.

---

## 🚀 Key Architectural Features

1. **Asynchronous Epoll Event Loop:** Avoids the thread-per-connection scaling bottleneck by multiplexing all socket reads, writes, and timer queues through a POSIX `epoll` controller in a single thread.
2. **Lock-Free Rarest-First Scheduler:** Computes swarm piece availability in real-time using `std::atomic<uint32_t>` histogram arrays with relaxed memory ordering, avoiding mutex contention on the hot path.
3. **Zero-Copy Wire Protocol Deserializer:** Maps incoming socket stream segments directly to memory spans (`std::span<const uint8_t>` and `std::string_view`) rather than copying payloads into temporary heap string structures.
4. **Zero-Trust Validation & Instant Blacklisting:** Routes assembled file pieces through an on-the-fly SHA-1 integrity hashing layer. Peers that transmit corrupt blocks are instantly flagged, disconnected, and permanently blacklisted in `PeerManager`.
5. **Graceful OS Interception:** Integrates clean OS signal handling (`Ctrl+C` / `SIGINT`), suppressing `SIGPIPE` crashes via `MSG_NOSIGNAL` flag validation, and cleanly flushing descriptor streams.

---

## 📂 Project Structure

```
├── code/
│   ├── include/          # Type-safe C++ headers (event loop, network machine, disk io)
│   ├── src/              # Source implementations (parsing, peer wire, bencode, scheduler)
│   ├── tests/            # C++ Google Tests & offline Python scripting
│   │   ├── test_bencode.cpp     # Unit tests for bencode parsing
│   │   ├── e2e_seeder.py        # Automated E2E local seeder validation
│   │   ├── benchmark.py         # Automated throughput benchmarking script
│   │   └── adversarial_test.py  # Hostile condition stress test (corrupt data, disconnects)
│   └── CMakeLists.txt    # Build definition file
├── docs/                 # Documentation & architectural design guides
│   ├── 01_high_level_design.md
│   ├── 02_low_level_design.md
│   ├── 03_task_breakdown.md
│   ├── Interview_Prep.md # Deep interview questions, tradeoffs, and debugging stories
│   ├── Notes.md          # Protocol specs and technical notes
│   └── benchmark_results.md # Loopback throughput performance data
├── torrent_files/        # Directory containing sample torrent files
├── .gitignore            # Clean git exclusion rules
└── README.md             # Project landing page
```

---

## 🛠️ Dependencies & Installation

To build and run the engine, you need:
- A Linux environment supporting `epoll`
- A C++20 compliant compiler (GCC 10+ or Clang 11+)
- **CMake** (v3.15+)
- **OpenSSL** / **Crypto++**
- **libcurl** (for HTTP tracker requests)
- **spdlog** (for thread-safe structured logging)

Install dependencies on Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libcurl4-openssl-dev libcrypto++-dev libspdlog-dev
```

---

## 🏗️ Building the Engine

Clone the repository and build using CMake:

```bash
# Generate Build files (Debug mode with AddressSanitizer enabled)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON

# Compile the target binaries
cmake --build build -j$(nproc)
```

### Build Options:
- `-DENABLE_ASAN=ON`: Compiles with AddressSanitizer to capture memory leaks, buffer overflows, or use-after-free bugs.
- `-DENABLE_TSAN=ON`: Compiles with ThreadSanitizer to isolate and report multi-threaded data races.

---

## 🧪 Testing & Verification

The project is backed by comprehensive offline unit tests and end-to-end integration environments:

### 1. Run Unit Tests
```bash
# Run tests compiled under the build folder
./build/tests_bin
```

### 2. Run Integration Test Suite
To verify the engine against a local python-based seeder simulating a real-world wire-protocol handshake, bitfield exchanges, and piece assemblies:
```bash
python3 code/tests/e2e_seeder.py
```

### 3. Run Adversarial Stress Test
To simulate adverse/hostile network conditions (corrupt piece packets, sudden peer disconnects, and offline trackers):
```bash
python3 code/tests/adversarial_test.py
```

### 4. Run Performance Benchmarking
To measure the peak throughput and packet handling capabilities of the client over the loopback interface:
```bash
python3 code/tests/benchmark.py
```
*Benchmark reports are automatically compiled and exported to `docs/benchmark_results.md`.*

---

## 📈 CLI Usage

```bash
./build/engine <path_to_torrent_file> [options]

Options:
  --output-dir=<dir>  Directory where downloaded files will be stored (default: .)
  --peer=<ip>:<port>  Manually specify a peer connection (bypasses tracker, useful for local testing)
  --log-level=<level> Logging severity: debug, info, warn, error (default: info)
  -h, --help          Show help options
```
