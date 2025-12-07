import socket
import threading
import time

TIMEOUT = 0.5
MAX_RETRIES = 3

class ReliableUDP:
    def __init__(self, ip, port, verbose=False):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((ip, port))
        self.sock.settimeout(TIMEOUT)
        self.verbose = verbose
        self.seq = 0
        self.acks = set()
        self.lock = threading.Lock()

    def send_message(self, msg_bytes, addr):
        retries = 0
        seq = self.seq
        while retries < MAX_RETRIES:
            if self.verbose:
                print(f"[SEND seq={seq}] {msg_bytes.decode()}")
            self.sock.sendto(msg_bytes, addr)
            try:
                data, _ = self.sock.recvfrom(1024)
                if data.decode().startswith(f"ACK:{seq}"):
                    break
            except socket.timeout:
                retries += 1
        self.seq += 1

    def receive_message(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(65536)
                lines = data.decode().splitlines()
                if lines[0].startswith("ACK:"):
                    ack_num = int(lines[0].split(":")[1])
                    self.acks.add(ack_num)
                    continue
                seq = None
                for line in lines:
                    if line.startswith("sequence_number:"):
                        seq = int(line.split(":")[1])
                        break
                if seq is not None:
                    ack_msg = f"ACK:{seq}".encode('utf-8')
                    self.sock.sendto(ack_msg, addr)
                return data, addr
            except socket.timeout:
                continue
