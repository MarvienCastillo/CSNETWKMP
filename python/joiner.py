import socket
import threading
import json
from pokemon_data import load_pokemon_csv, get_pokemon_by_name

HOST_IP = '127.0.0.1'
HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

handshake_done = False
battle_setup_done = False
is_spectator = False
host_addr = (HOST_IP, HOST_PORT)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 0))  # Let OS pick a free port

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battle_state = None  # {'pokemon':..., 'boosts':..., 'mode':..., 'turn':...}

    def setup_battle(self, pokemon, boosts, mode):
        self.battle_state = {
            'pokemon': pokemon,
            'boosts': boosts,
            'mode': mode,
            'turn': 0
        }

    def process_action(self, action):
        if not self.battle_state:
            print("[BATTLE] No battle yet.")
            return
        self.battle_state['turn'] += 1
        print(f"[BATTLE] Turn {self.battle_state['turn']}, Action={action}")

    def show_battle(self):
        if not self.battle_state:
            print("[BATTLE] No battle yet.")
            return
        b = self.battle_state
        print(f"[BATTLE] Pokémon={b['pokemon'].name}, Mode={b['mode']}, Boosts={b['boosts']}, Turn={b['turn']}")

battle_manager = BattleManager()

# ------------------------
# Listener thread
# ------------------------
def listen():
    global handshake_done, battle_setup_done, is_spectator
    while True:
        try:
            data, addr = sock.recvfrom(4096)
            msg = data.decode('utf-8').strip()
            if not msg:
                continue
            process_message(msg, addr)
        except Exception as e:
            print(f"[ERROR] Listening thread: {e}")

# ------------------------
# Process messages
# ------------------------
def process_message(msg, addr):
    global handshake_done, battle_setup_done, is_spectator
    lines = msg.splitlines()
    msg_type = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_type:")), None)
    if not msg_type:
        print(f"[JOINER] Unknown message from {addr}: {msg}")
        return

    if msg_type == "HANDSHAKE_RESPONSE":
        seed = int(next(line.split(":",1)[1].strip() for line in lines if line.startswith("seed:")))
        handshake_done = True
        print(f"\n[JOINER] Handshake completed! Seed={seed}\n> ", end="")

    elif msg_type == "SPECTATOR_RESPONSE":
        is_spectator = True
        print(f"\n[JOINER] Registered as spectator.\n> ", end="")

    elif msg_type == "BATTLE_SETUP":
        pokemon_name = next(line.split(":",1)[1].strip() for line in lines if line.startswith("pokemon_name:"))
        boosts = json.loads(next(line.split(":",1)[1].strip() for line in lines if line.startswith("stat_boosts:")))
        mode = next(line.split(":",1)[1].strip() for line in lines if line.startswith("communication_mode:"))

        pokemon = get_pokemon_by_name(pokemons, pokemon_name)
        if not pokemon:
            print(f"[JOINER] Unknown Pokémon {pokemon_name} in battle setup.")
            return

        battle_manager.setup_battle(pokemon, boosts, mode)
        battle_setup_done = True
        print(f"\n[JOINER] Battle setup received: Pokémon={pokemon.name}, Mode={mode}, Boosts={boosts}\n> ", end="")

    elif msg_type == "BATTLE_ACTION":
        action = next(line.split(":",1)[1].strip() for line in lines if line.startswith("action:"))
        battle_manager.process_action(action)

    elif msg_type == "CHAT_MESSAGE":
        sender_name = next((line.split(":",1)[1].strip() for line in lines if line.startswith("sender_name:")), "Unknown")
        text = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_text:")), "")
        print(f"\n[CHAT][{sender_name}]: {text}\n> ", end="")

    else:
        print(f"\n[JOINER] Unknown message_type {msg_type}\n> ", end="")

# ------------------------
# Sending functions
# ------------------------
def send_handshake():
    msg = "message_type: HANDSHAKE_REQUEST\n"
    sock.sendto(msg.encode('utf-8'), host_addr)
    print("[JOINER] HANDSHAKE_REQUEST sent")

def send_battle_setup():
    if not handshake_done:
        print("[JOINER] Cannot do BATTLE_SETUP before handshake.")
        return
    if is_spectator:
        print("[JOINER] Spectators cannot participate in battles.")
        return

    communication_mode = input("communication_mode (P2P/BROADCAST): ").strip()
    pokemon_name = input("pokemon_name: ").strip()
    pokemon = get_pokemon_by_name(pokemons, pokemon_name)
    if not pokemon:
        print(f"[JOINER] Pokémon '{pokemon_name}' not found.")
        return

    try:
        atk_uses = int(input("special_attack_uses: "))
        def_uses = int(input("special_defense_uses: "))
    except:
        atk_uses, def_uses = 4, 3

    boosts = {"special_attack_uses": atk_uses, "special_defense_uses": def_uses}

    # Setup local battle
    battle_manager.setup_battle(pokemon, boosts, communication_mode)
    global battle_setup_done
    battle_setup_done = True

    msg = (
        f"message_type: BATTLE_SETUP\n"
        f"communication_mode: {communication_mode}\n"
        f"pokemon_name: {pokemon.name}\n"
        f"stat_boosts: {json.dumps(boosts)}\n"
    )
    sock.sendto(msg.encode('utf-8'), host_addr)
    print(f"[JOINER] BATTLE_SETUP sent for {pokemon.name}")

def send_spectator_request():
    if not handshake_done:
        print("[JOINER] Cannot register as spectator before handshake.")
        return
    global is_spectator
    is_spectator = True
    msg = "message_type: SPECTATOR_REQUEST\n"
    sock.sendto(msg.encode('utf-8'), host_addr)
    print("[JOINER] SPECTATOR_REQUEST sent")

def send_battle_action():
    if not battle_setup_done or is_spectator:
        print("[JOINER] Cannot send action. Either battle not set or spectator.")
        return
    action = input("Enter action: ").strip()
    battle_manager.process_action(action)
    msg = f"message_type: BATTLE_ACTION\naction: {action}\n"
    sock.sendto(msg.encode('utf-8'), host_addr)

# ------------------------
# User input thread
# ------------------------
def user_input_loop():
    while True:
        cmd = input("> ").strip()
        if cmd == "HANDSHAKE_REQUEST":
            send_handshake()
        elif cmd == "BATTLE_SETUP":
            send_battle_setup()
        elif cmd == "SPECTATOR_REQUEST":
            send_spectator_request()
        elif cmd == "BATTLE_ACTION":
            send_battle_action()
        elif cmd.startswith("CHAT "):
            text = cmd[len("CHAT "):]
            msg = f"message_type: CHAT_MESSAGE\nsender_name: Joiner\ncontent_type: TEXT\nmessage_text: {text}\n"
            sock.sendto(msg.encode('utf-8'), host_addr)
        elif cmd == "battle_show":
            battle_manager.show_battle()
        elif cmd == "exit":
            break
        else:
            print("[JOINER] Unknown command. Available: HANDSHAKE_REQUEST, BATTLE_SETUP, SPECTATOR_REQUEST, BATTLE_ACTION, CHAT <text>, battle_show, exit")

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    threading.Thread(target=user_input_loop).start()

if __name__ == "__main__":
    main()