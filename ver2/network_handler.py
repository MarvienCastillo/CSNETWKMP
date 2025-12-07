import socket
import threading
import json

class NetworkHandler:
    def __init__(self, bind_addr, battle_manager, on_message_callback=None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(bind_addr)
        self.battle_manager = battle_manager
        self.on_message_callback = on_message_callback
        self.peers = {}  # addr -> {"role": "joiner"|"spectator"}

        self.listener_thread = threading.Thread(target=self.listen, daemon=True)
        self.listener_thread.start()

    def listen(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(4096)
                msg = data.decode('utf-8').strip()
                if not msg:
                    continue
                if self.on_message_callback:
                    self.on_message_callback(msg, addr)
            except Exception as e:
                print(f"[ERROR] Listening: {e}")

    def send_message(self, addr, msg_dict):
        msg = "\n".join([f"{k}: {v}" for k, v in msg_dict.items()]) + "\n"
        self.sock.sendto(msg.encode('utf-8'), addr)

    def broadcast(self, msg_dict):
        for addr in self.peers.keys():
            self.send_message(addr, msg_dict)
