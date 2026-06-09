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
    - No leading zero. i03e is INVALID
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