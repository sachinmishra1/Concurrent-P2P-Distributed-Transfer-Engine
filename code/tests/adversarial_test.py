import socket
import threading
import sys
import os
import hashlib
import struct
import subprocess
import time

def bencode_dict(d):
    res = b"d"
    for k in sorted(d.keys()):
        res += f"{len(k)}:".encode('ascii') + k.encode('ascii')
        v = d[k]
        if isinstance(v, int):
            res += f"i{v}e".encode('ascii')
        elif isinstance(v, bytes):
            res += f"{len(v)}:".encode('ascii') + v
        elif isinstance(v, str):
            res += f"{len(v)}:".encode('ascii') + v.encode('ascii')
        else:
            raise TypeError("Unsupported type for bencoding")
    res += b"e"
    return res

# ==========================================
# Scenario 1: Corrupt Peer Seeder
# ==========================================
def run_corrupt_seeder(ip, port, info_hash, file_content, piece_length, ready_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((ip, port))
        server.listen(1)
        ready_event.set()
    except Exception as e:
        print(f"[Corrupt Seeder] Bind failed: {e}")
        ready_event.set()
        return

    try:
        server.settimeout(5.0)
        conn, addr = server.accept()
        conn.settimeout(5.0)

        # Handshake
        handshake = conn.recv(68)
        server_id = b"-pyseed-corrupt12345"
        server_handshake = struct.pack(">B19s8s20s20s", 19, b"BitTorrent protocol", b"\x00"*8, info_hash, server_id)
        conn.sendall(server_handshake)

        # Bitfield & Unchoke
        bitfield_msg = struct.pack(">IBB", 2, 5, 0xf0) # 4 pieces
        conn.sendall(bitfield_msg)
        unchoke_msg = struct.pack(">IB", 1, 1)
        conn.sendall(unchoke_msg)

        # Request loop
        while True:
            len_bytes = conn.recv(4)
            if not len_bytes:
                break
            msg_len = struct.unpack(">I", len_bytes)[0]
            if msg_len == 0:
                continue
            msg_id = conn.recv(1)[0]
            
            payload_len = msg_len - 1
            payload = b""
            while len(payload) < payload_len:
                chunk = conn.recv(payload_len - len(payload))
                if not chunk:
                    break
                payload += chunk

            if msg_id == 6: # Request
                index, begin, length = struct.unpack(">III", payload[:12])
                
                # Send corrupt payload for index == 2
                if index == 2:
                    block_data = b"\x00" * length # Corrupt data
                else:
                    file_offset = index * piece_length + begin
                    block_data = file_content[file_offset : file_offset + length]
                
                piece_msg = struct.pack(">IBII", 9 + len(block_data), 7, index, begin) + block_data
                conn.sendall(piece_msg)
    except Exception as e:
        print(f"[Corrupt Seeder] Connection handled: {e}")
    finally:
        server.close()

# ==========================================
# Scenario 2: Disconnect Mid-Transfer Seeder
# ==========================================
def run_disconnect_seeder(ip, port, info_hash, file_content, piece_length, ready_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((ip, port))
        server.listen(1)
        ready_event.set()
    except Exception as e:
        print(f"[Disconnect Seeder] Bind failed: {e}")
        ready_event.set()
        return

    try:
        server.settimeout(5.0)
        conn, addr = server.accept()
        conn.settimeout(5.0)

        # Handshake
        handshake = conn.recv(68)
        server_id = b"-pyseed-disconn12345"
        server_handshake = struct.pack(">B19s8s20s20s", 19, b"BitTorrent protocol", b"\x00"*8, info_hash, server_id)
        conn.sendall(server_handshake)

        # Bitfield & Unchoke
        bitfield_msg = struct.pack(">IBB", 2, 5, 0xf0)
        conn.sendall(bitfield_msg)
        unchoke_msg = struct.pack(">IB", 1, 1)
        conn.sendall(unchoke_msg)

        request_count = 0
        while True:
            len_bytes = conn.recv(4)
            if not len_bytes:
                break
            msg_len = struct.unpack(">I", len_bytes)[0]
            if msg_len == 0:
                continue
            msg_id = conn.recv(1)[0]
            
            payload_len = msg_len - 1
            payload = b""
            while len(payload) < payload_len:
                chunk = conn.recv(payload_len - len(payload))
                if not chunk:
                    break
                payload += chunk

            if msg_id == 6: # Request
                request_count += 1
                if request_count >= 5:
                    # Violently close mid-transfer
                    print("[Disconnect Seeder] Simulating sudden network failure/disconnection...")
                    break
                
                index, begin, length = struct.unpack(">III", payload[:12])
                file_offset = index * piece_length + begin
                block_data = file_content[file_offset : file_offset + length]
                
                piece_msg = struct.pack(">IBII", 9 + len(block_data), 7, index, begin) + block_data
                conn.sendall(piece_msg)
    except Exception as e:
        print(f"[Disconnect Seeder] Handled: {e}")
    finally:
        server.close()

# ==========================================
# Scenario 3: Normal Seeder (for fallbacks/reconnects)
# ==========================================
def run_normal_seeder(ip, port, info_hash, file_content, piece_length, ready_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((ip, port))
        server.listen(1)
        ready_event.set()
    except Exception as e:
        ready_event.set()
        return

    try:
        server.settimeout(10.0)
        conn, addr = server.accept()
        conn.settimeout(5.0)

        # Handshake
        handshake = conn.recv(68)
        server_id = b"-pyseed-normal123456"
        server_handshake = struct.pack(">B19s8s20s20s", 19, b"BitTorrent protocol", b"\x00"*8, info_hash, server_id)
        conn.sendall(server_handshake)

        # Bitfield & Unchoke
        bitfield_msg = struct.pack(">IBB", 2, 5, 0xf0)
        conn.sendall(bitfield_msg)
        unchoke_msg = struct.pack(">IB", 1, 1)
        conn.sendall(unchoke_msg)

        while True:
            len_bytes = conn.recv(4)
            if not len_bytes:
                break
            msg_len = struct.unpack(">I", len_bytes)[0]
            if msg_len == 0:
                continue
            msg_id = conn.recv(1)[0]
            
            payload_len = msg_len - 1
            payload = b""
            while len(payload) < payload_len:
                chunk = conn.recv(payload_len - len(payload))
                if not chunk:
                    break
                payload += chunk

            if msg_id == 6: # Request
                index, begin, length = struct.unpack(">III", payload[:12])
                file_offset = index * piece_length + begin
                block_data = file_content[file_offset : file_offset + length]
                
                piece_msg = struct.pack(">IBII", 9 + len(block_data), 7, index, begin) + block_data
                conn.sendall(piece_msg)
    except Exception:
        pass
    finally:
        server.close()

def main():
    # Setup test file & torrent (1MB)
    file_size = 1048576
    piece_length = 262144
    num_pieces = file_size // piece_length
    dummy_data = os.urandom(file_size)
    
    piece_hashes = b""
    for i in range(num_pieces):
        slice_data = dummy_data[i * piece_length : (i + 1) * piece_length]
        piece_hashes += hashlib.sha1(slice_data).digest()
    
    info_dict = {
        "length": file_size,
        "name": "adversarial_test.bin",
        "piece length": piece_length,
        "pieces": piece_hashes
    }
    info_encoded = bencode_dict(info_dict)
    info_hash = hashlib.sha1(info_encoded).digest()
    
    # We point announce to a dummy/offline tracker url to verify offline tracker fallback
    torrent_encoded = b"d8:announce38:http://127.0.0.1:9999/announce-offline4:info" + info_encoded + b"e"
    torrent_path = "adversarial.torrent"
    with open(torrent_path, "wb") as f:
        f.write(torrent_encoded)
    
    print("\n" + "="*50)
    print("      STARTING ADVERSARIAL STRESS TESTS")
    print("="*50)

    # ----------------------------------------------------
    # TEST 1: Corrupt Peer Blacklisting
    # ----------------------------------------------------
    print("\n[TEST 1] Testing Corrupt Peer Blacklisting...")
    ready1 = threading.Event()
    t1 = threading.Thread(target=run_corrupt_seeder, args=("127.0.0.1", 6884, info_hash, dummy_data, piece_length, ready1))
    t1.daemon = True
    t1.start()
    ready1.wait()
    time.sleep(0.1)

    os.makedirs("adv_out", exist_ok=True)
    engine_path = "./build/engine"
    
    cmd1 = [engine_path, torrent_path, "--output-dir=adv_out", "--peer=127.0.0.1:6884", "--log-level=debug"]
    proc1 = subprocess.Popen(cmd1, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Let it run for 4 seconds, then check if it terminated or blacklisted
    time.sleep(4.0)
    proc1.kill()
    stdout1_bytes, stderr1_bytes = proc1.communicate()
    stdout1 = stdout1_bytes.decode('utf-8', errors='ignore')
    stderr1 = stderr1_bytes.decode('utf-8', errors='ignore')

    # Verify blacklisting log messages in output
    all_output = stdout1 + stderr1
    corrupt_success = "corrupt" in all_output.lower() or "blacklist" in all_output.lower() or "validation failed" in all_output.lower()
    if corrupt_success:
        print("-> [TEST 1 PASS] Client correctly identified piece corruption and flagged peer.")
    else:
        print("-> [TEST 1 FAIL] Client did not flag or blacklist the corrupt peer.")
        print(all_output)
        sys.exit(1)

    # ----------------------------------------------------
    # TEST 2 & 3: Sudden Disconnection & Offline Tracker Fallback
    # ----------------------------------------------------
    print("\n[TEST 2 & 3] Testing Sudden Disconnect & Offline Tracker Fallback...")
    
    # We launch a seeder that disconnects mid-transfer, and a normal seeder to reconnect to and finish the download
    ready2 = threading.Event()
    t2 = threading.Thread(target=run_disconnect_seeder, args=("127.0.0.1", 6885, info_hash, dummy_data, piece_length, ready2))
    t2.daemon = True
    t2.start()
    ready2.wait()
    
    ready3 = threading.Event()
    t3 = threading.Thread(target=run_normal_seeder, args=("127.0.0.1", 6886, info_hash, dummy_data, piece_length, ready3))
    t3.daemon = True
    t3.start()
    ready3.wait()
    time.sleep(0.1)

    # Note: the torrent file lists a broken tracker URL, so testing fallback is active.
    # We pass both peers. The client will connect to both or try them.
    cmd2 = [
        engine_path, 
        torrent_path, 
        "--output-dir=adv_out", 
        "--peer=127.0.0.1:6885", 
        "--peer=127.0.0.1:6886", 
        "--log-level=debug"
    ]
    
    proc2 = subprocess.Popen(cmd2, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        stdout2_bytes, stderr2_bytes = proc2.communicate(timeout=10.0)
        stdout2 = stdout2_bytes.decode('utf-8', errors='ignore')
        stderr2 = stderr2_bytes.decode('utf-8', errors='ignore')
    except subprocess.TimeoutExpired:
        print("-> [TEST 2/3 FAIL] Engine timed out or hung under network disconnection/tracker failure.")
        proc2.kill()
        sys.exit(1)

    all_output2 = stdout2 + stderr2
    
    # Verify that the download successfully completed despite broken tracker and peer disconnect
    dest_file = "adv_out/adversarial_test.bin"
    if os.path.exists(dest_file):
        with open(dest_file, "rb") as f:
            downloaded = f.read()
        if downloaded == dummy_data:
            print("-> [TEST 2/3 PASS] Client bypassed offline tracker, recovered from mid-transfer disconnect, and successfully downloaded complete file.")
        else:
            print("-> [TEST 2/3 FAIL] Byte content mismatch on final output.")
            sys.exit(1)
    else:
        print("-> [TEST 2/3 FAIL] Download did not complete (output file not found).")
        print(all_output2)
        sys.exit(1)

    # Cleanup
    try:
        os.remove(torrent_path)
        os.remove(dest_file)
        os.rmdir("adv_out")
    except Exception as e:
        print(f"Cleanup warning: {e}")

    print("\n" + "="*50)
    print("      ALL ADVERSARIAL STRESS TESTS PASSED!")
    print("="*50)
    sys.exit(0)

if __name__ == "__main__":
    main()
