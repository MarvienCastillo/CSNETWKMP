import socket
import threading
import json
import random
from pokemon_data import load_pokemon_csv, get_pokemon_by_name

HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

joiners = {}       # addr -> info
spectators = {}    # addr -> info

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', HOST_PORT))
print(f"[HOST] Listening on port {HOST_PORT}...")

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}  # addr -> battle_state

    def setup_battle(self, addr, pokemon, boosts, mode):
        self.battles[addr] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "mode": mode,
            "turn": 0
        }

    def process_action(self, addr, action):
        battle = self.battles.get(addr)
        if not battle:
            print(f"[BATTLE] No battle for {addr}")
            return
        # Example: increment turn
        battle["turn"] += 1
        print(f"[BATTLE][{addr}] Turn {battle['turn']}, Action={action}")

    def show_battle(self, addr):
        battle = self.battles.get(addr)
        if not battle:
            print(f"[BATTLE] No battle for {addr}")
            return
        print(f"[BATTLE][{addr}] Pokémon={battle['pokemon'].name}, Mode={battle['mode']}, Boosts={battle['boosts']}, Turn={battle['turn']}")

battle_manager = BattleManager()

# ------------------------
# Listener
# ------------------------
def listen():
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
# Process message
# ------------------------
def process_message(msg, addr):
    lines = msg.splitlines()
    msg_type = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_type:")), None)
    if not msg_type:
        print(f"[HOST] Unknown message from {addr}: {msg}")
        return

    if msg_type == "HANDSHAKE_REQUEST":
        seed = random.randint(0, 999999)
        sock.sendto(f"message_type: HANDSHAKE_RESPONSE\nseed: {seed}\n".encode('utf-8'), addr)
        joiners[addr] = {"seed": seed, "battle_setup_done": False,"addr":addr}
        print(f"[HOST] Handshake with {addr}, seed={seed}")

    elif msg_type == "SPECTATOR_REQUEST":
        spectators[addr] = {"registered": True}
        sock.sendto("message_type: SPECTATOR_RESPONSE\n".encode('utf-8'), addr)
        print(f"[HOST] Spectator registered: {addr}")

    elif msg_type == "BATTLE_SETUP":
        try:
            comm_mode = next(line.split(":",1)[1].strip() for line in lines if line.startswith("communication_mode:"))
            pokemon_name = next(line.split(":",1)[1].strip() for line in lines if line.startswith("pokemon_name:"))
            boosts = json.loads(next(line.split(":",1)[1].strip() for line in lines if line.startswith("stat_boosts:")))

            pokemon = get_pokemon_by_name(pokemons, pokemon_name)
            if not pokemon:
                print(f"[HOST] Unknown Pokémon {pokemon_name} from {addr}")
                return

            joiners[addr].update({
                "battle_setup_done": True,
                "pokemon": pokemon,
                "communication_mode": comm_mode,
                "boosts": boosts
            })

            battle_manager.setup_battle(addr, pokemon, boosts, comm_mode)
            print(f"[HOST] Battle setup from {addr}: Pokémon={pokemon.name}, mode={comm_mode}, boosts={boosts}")
            battle_manager.show_battle(addr)
        except Exception as e:
            print(f"[HOST] Failed to parse BATTLE_SETUP: {e}")

    elif msg_type == "BATTLE_ACTION":
        try:
            action_line = next(line.split(":",1)[1].strip() for line in lines if line.startswith("action:"))
            battle_manager.process_action(addr, action_line)
        except:
            print(f"[HOST] Failed to parse BATTLE_ACTION from {addr}")

    elif msg_type == "CHAT_MESSAGE":
        sender_name = next((line.split(":",1)[1].strip() for line in lines if line.startswith("sender_name:")), "Unknown")
        text = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_text:")), "")
        print(f"[CHAT][{sender_name}]: {text}")

    else:
        print(f"[HOST] Unknown message_type {msg_type} from {addr}")

# ------------------------
# User input
# ------------------------
def user_input_loop():
    while True:
        cmd = input("> ").strip()
        if cmd.startswith("CHAT "):
            text = cmd[len("CHAT "):]
            msg = f"message_type: CHAT_MESSAGE\nsender_name: HOST\ncontent_type: TEXT\nmessage_text: {text}\n"
            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg.encode('utf-8'), addr)

        elif cmd == "BATTLE_SETUP":
            # Let host choose Pokémon and mode
            pokemon_name = input("pokemon_name: ").strip()
            pokemon = get_pokemon_by_name(pokemons, pokemon_name)
            if not pokemon:
                print("[HOST] Pokémon not found")
                continue
            mode = input("communication_mode (P2P/BROADCAST): ").strip()
            try:
                atk_uses = int(input("special_attack_uses: "))
                def_uses = int(input("special_defense_uses: "))
            except:
                atk_uses, def_uses = 4,3
            boosts = {"special_attack_uses": atk_uses, "special_defense_uses": def_uses}

            # Setup own battle
            host_addr = ("HOST", 0)
            battle_manager.setup_battle(host_addr, pokemon, boosts, mode)
            print(f"[HOST] Battle setup complete: Pokémon={pokemon.name}, mode={mode}, boosts={boosts}")
            msg = (
                f"message_type: BATTLE_SETUP\n"
                f"communication_mode: {mode}\n"
                f"pokemon_name: {pokemon.name}\n"
                f"stat_boosts: {json.dumps(boosts)}\n"
            )
            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg.encode('utf-8'), addr)
            print(f"[JOINER] BATTLE_SETUP sent for {pokemon.name}")
        elif cmd.startswith("battle_action "):
            action = cmd[len("battle_action "):]
            host_addr = ("HOST", 0)
            battle_manager.process_action(host_addr, action)
            # Broadcast to joiners/spectators
            msg = f"message_type: BATTLE_ACTION\naction: {action}\n"
            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg.encode('utf-8'), addr)

        elif cmd == "list_joiners":
            print(f"[HOST] Joiners: {joiners}")
        elif cmd == "list_spectators":
            print(f"[HOST] Spectators: {spectators}")
        elif cmd.startswith("battle_show "):
            ip, port = cmd[len("battle_show "):].split(":")
            addr = (ip.strip(), int(port.strip()))
            battle_manager.show_battle(addr)
        elif cmd == "exit":
            break
        else:
            print("[HOST] Unknown command")

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    threading.Thread(target=user_input_loop).start()

if __name__ == "__main__":
    main()
