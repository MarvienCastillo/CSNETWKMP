# spectator.py
import threading
from network import ReliableUDP
from messages import deserialize_message
from chat import save_sticker_message

HOST_IP = "127.0.0.1"
HOST_PORT = 9002
SPECTATOR_PORT = 9004
VERBOSE = True

# Initialize UDP socket
spectator = ReliableUDP("0.0.0.0", SPECTATOR_PORT, verbose=VERBOSE)

# Send SPECTATOR_REQUEST to Host
from messages import serialize_message
req = serialize_message({"message_type": "SPECTATOR_REQUEST"})
spectator.send_message(req, (HOST_IP, HOST_PORT))
print(f"[SPECTATOR] Sent request to observe {HOST_IP}:{HOST_PORT}")

# Listener thread for battle & chat messages
def listen():
    while True:
        data, addr = spectator.receive_message()
        msg = deserialize_message(data)
        
        if msg["message_type"] == "CHAT_MESSAGE":
            if msg["content_type"] == "TEXT":
                print(f"[CHAT {msg['sender_name']}] {msg['message_text']}")
            elif msg["content_type"] == "STICKER":
                path = save_sticker_message(msg)
                print(f"[CHAT {msg['sender_name']}] Sent a sticker! Saved as {path}")
        
        elif msg["message_type"] == "ATTACK_ANNOUNCE":
            print(f"[BATTLE] {msg['move_name']} attack announced by {msg['attacker'] if 'attacker' in msg else 'Unknown'}")
        
        elif msg["message_type"] == "CALCULATION_REPORT":
            print(f"[BATTLE] {msg['status_message']}")
        
        elif msg["message_type"] == "GAME_OVER":
            print(f"[GAME OVER] Winner: {msg['winner']}, Loser: {msg['loser']}")
            break

threading.Thread(target=listen, daemon=True).start()

# Keep the main thread alive
try:
    while True:
        pass
except KeyboardInterrupt:
    print("[SPECTATOR] Exiting...")
