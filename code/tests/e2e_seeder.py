import socket
import threading
import sys
import os
import hashlib
import struct
import subprocess
import time

def bencode_dict(d):
    # Sort keys lexicographically and encode
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

def run_seeder(ip, port, info_hash, file_content, piece_length, ready_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((ip, port))
        server.listen(1)
        ready_event.set()
    except Exception as e:
        print(f"[Seeder] Bind failed: {e}")
        ready_event.set()
        return

    print(f"[Seeder] Listening on {ip}:{port}")
    try:
        server.settimeout(10.0)
        conn, addr = server.accept()
        print(f"[Seeder] Connection accepted from {addr}")
        conn.settimeout(5.0)

        # 1. Read handshake (68 bytes)
        handshake = conn.recv(68)
        if len(handshake) < 68:
            print(f"[Seeder] Handshake too short: {len(handshake)} bytes")
            conn.close()
            return
        
        pstrlen = handshake[0]
        pstr = handshake[1:20]
        client_hash = handshake[28:48]
        if pstrlen != 19 or pstr != b"BitTorrent protocol" or client_hash != info_hash:
            print(f"[Seeder] Invalid handshake. pstr: {pstr}, hash match: {client_hash == info_hash}")
            conn.close()
            return
        
        # 2. Send handshake back
        server_id = b"-pyseed-123456789012"
        server_handshake = struct.pack(">B19s8s20s20s", 19, b"BitTorrent protocol", b"\x00"*8, info_hash, server_id)
        conn.sendall(server_handshake)
        print("[Seeder] Handshake exchanged")

        # 3. Send Bitfield indicating we have all 4 pieces (bitfield byte is 11110000 = 0xf0)
        # Msg length: 2 (1 byte ID, 1 byte payload)
        bitfield_msg = struct.pack(">IBB", 2, 5, 0xf0)
        conn.sendall(bitfield_msg)
        print("[Seeder] Sent Bitfield")

        # 4. Send Unchoke msg
        # Msg length: 1
        unchoke_msg = struct.pack(">IB", 1, 1)
        conn.sendall(unchoke_msg)
        print("[Seeder] Sent Unchoke")

        # 5. Service requests
        while True:
            try:
                len_bytes = conn.recv(4)
                if not len_bytes:
                    print("[Seeder] Connection closed by remote client")
                    break
                
                msg_len = struct.unpack(">I", len_bytes)[0]
                if msg_len == 0:
                    # Keep-Alive
                    continue
                
                msg_id = conn.recv(1)[0]
                payload_len = msg_len - 1
                payload = b""
                while len(payload) < payload_len:
                    chunk = conn.recv(payload_len - len(payload))
                    if not chunk:
                        break
                    payload += chunk
                
                if msg_id == 2:
                    print("[Seeder] Received Interested message")
                elif msg_id == 4: # Have
                    if len(payload) >= 4:
                        piece_idx = struct.unpack(">I", payload[:4])[0]
                        print(f"[Seeder] Received Have message for piece {piece_idx}")
                elif msg_id == 6: # Request
                    if len(payload) < 12:
                        print("[Seeder] Request payload too short")
                        break
                    index, begin, length = struct.unpack(">III", payload[:12])
                    
                    # Read block from mock file content
                    file_offset = index * piece_length + begin
                    block_data = file_content[file_offset : file_offset + length]
                    
                    # Construct piece message: len = 9 + block_data_len, ID = 7
                    piece_msg = struct.pack(">IBII", 9 + len(block_data), 7, index, begin) + block_data
                    conn.sendall(piece_msg)
                else:
                    print(f"[Seeder] Unknown message ID: {msg_id}")
            except socket.timeout:
                print("[Seeder] Connection timeout")
                break
            except Exception as e:
                print(f"[Seeder] Error in loop: {e}")
                break
        
        conn.close()
    except Exception as e:
        print(f"[Seeder] Server crash: {e}")
    finally:
        server.close()

def main():
    # 1. Generate 1MB dummy data
    print("Generating 1MB dummy data...")
    dummy_data = os.urandom(1048576) # Exactly 1MB
    
    # Calculate SHA-1 of the 4 pieces (256KB each)
    piece_length = 262144
    piece_hashes = b""
    for i in range(4):
        slice_data = dummy_data[i * piece_length : (i + 1) * piece_length]
        piece_hashes += hashlib.sha1(slice_data).digest()
    
    # 2. Build Bencoded Info Dictionary
    info_dict = {
        "length": 1048576,
        "name": "test_e2e.bin",
        "piece length": piece_length,
        "pieces": piece_hashes
    }
    info_encoded = bencode_dict(info_dict)
    info_hash = hashlib.sha1(info_encoded).digest()
    
    # Build Bencoded Root Dictionary manually to avoid double-string-encoding the pre-encoded info_encoded dictionary
    torrent_encoded = b"d8:announce30:http://127.0.0.1:8000/announce4:info" + info_encoded + b"e"
    
    # Write to test_e2e.torrent
    torrent_path = "test_e2e.torrent"
    with open(torrent_path, "wb") as f:
        f.write(torrent_encoded)
    print(f"Created {torrent_path} successfully.")
    
    # 3. Start seeder thread on port 6882
    ready_event = threading.Event()
    seeder_thread = threading.Thread(
        target=run_seeder, 
        args=("127.0.0.1", 6882, info_hash, dummy_data, piece_length, ready_event)
    )
    seeder_thread.daemon = True
    seeder_thread.start()
    
    # Wait for seeder socket to bind
    ready_event.wait()
    time.sleep(0.5)
    
    # 4. Launch engine binary
    # We save downloaded file in the directory 'e2e_out'
    os.makedirs("e2e_out", exist_ok=True)
    engine_path = "./build/engine"
    
    print("Launching Engine...")
    cmd = [
        engine_path, 
        torrent_path, 
        "--output-dir=e2e_out", 
        "--peer=127.0.0.1:6882", 
        "--log-level=debug"
    ]
    
    start_time = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for the process to complete with a 20-second timeout
    try:
        stdout_bytes, stderr_bytes = proc.communicate(timeout=20.0)
        stdout = stdout_bytes.decode('utf-8', errors='ignore')
        stderr = stderr_bytes.decode('utf-8', errors='ignore')
        print("Engine Output:")
        print(stdout)
        if stderr:
            print("Engine Errors:")
            print(stderr)
    except subprocess.TimeoutExpired:
        print("Engine timed out!")
        proc.kill()
        stdout_bytes, stderr_bytes = proc.communicate()
        print("Engine Output (timeout):")
        print(stdout_bytes.decode('utf-8', errors='ignore'))
        sys.exit(1)
        
    # 5. Verify results
    dest_file = "e2e_out/test_e2e.bin"
    if not os.path.exists(dest_file):
        print(f"Verification Failed: {dest_file} was not created!")
        sys.exit(1)
        
    with open(dest_file, "rb") as f:
        downloaded_data = f.read()
        
    if downloaded_data != dummy_data:
        print("Verification Failed: byte content mismatch!")
        sys.exit(1)
        
    print("E2E Test Passed Successfully!")
    
    # Cleanup files
    try:
        os.remove(torrent_path)
        os.remove(dest_file)
        os.rmdir("e2e_out")
    except Exception as e:
        print(f"Cleanup warning: {e}")
        
    sys.exit(0)

if __name__ == "__main__":
    main()
