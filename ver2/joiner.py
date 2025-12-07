# joiner.py
import socket
import threading
import json
from pokemon_data import load_pokemon_csv, get_pokemon_by_name

HOST_IP = "127.0.0.1"
HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 0))  # random free port
host_addr = (HOST_IP, HOST_PORT)

handshake_done = False
battle_setup_done = False
battle_mode = None

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}  # "HOST" or "JOINER_ADDR" -> battle state

    def setup_battle(self, key, pokemon, boosts, mode):
        self.battles[key] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "hp": pokemon.hp,
            "turn": 0,
            "current_player": "HOST" if key=="HOST" else "JOINER",
            "battle_over": False,
            "special_attack_uses": boosts.get("special_attack_uses",4),
            "special_defense_uses": boosts.get("special_defense_uses",3)
        }

    def show_battle(self, key):
        battle = self.battles.get(key)
        if not battle:
            print(f"[BATTLE] No battle for {key}")
            return
        print(f"[BATTLE][{key}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
              f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def process_attack(self, attacker_key, defender_key, move_type):
        battle = self.battles.get(defender_key)
        attacker = self.battles.get(attacker_key)
        if not battle or battle["battle_over"]:
            return 0

        damage = 0
        if move_type == "ATTACK":
            damage = max(1, attacker["pokemon"].attack - battle["pokemon"].defense)
        elif move_type == "SPECIAL_ATTACK" and attacker["special_attack_uses"] > 0:
            damage = max(1, attacker["pokemon"].sp_attack - battle["pokemon"].sp_defense)
            attacker["special_attack_uses"] -= 1
        elif move_type == "SPECIAL_DEFENSE" and attacker["special_defense_uses"] > 0:
            heal = min(attacker["pokemon"].sp_defense, attacker["pokemon"].hp - attacker["hp"])
            attacker["hp"] += heal
            attacker["special_defense_uses"] -= 1
            print(f"[BATTLE] {attacker['pokemon'].name} healed {heal} HP")
            return 0

        battle["hp"] -= damage
        print(f"[BATTLE] {attacker['pokemon'].name} {move_type} -> {battle['pokemon'].name} damage={damage}, HP={battle['hp']}")
        attacker["turn"] += 1
        # Switch current player
        if attacker["current_player"] == "HOST":
            attacker["current_player"] = "JOINER"
        else:
            attacker["current_player"] = "HOST"

        if battle["hp"] <= 0:
            battle["battle_over"] = True
            print(f"[BATTLE] {battle['pokemon'].name} fainted! Winner: {attacker['pokemon'].name}")

        return damage

battle_manager = BattleManager()

# ------------------------
# Listener
# ------------------------
def listen():
    global handshake_done, battle_setup_done, battle_mode
    while True:
        try:
            data, addr = sock.recvfrom(4096)
            msg = data.decode('utf-8').strip()
            if not msg:
                continue
            process_message(msg, addr)
        except Exception as e:
            print(f"[ERROR] Listening thread: {e}")

def process_message(msg, addr):
    global handshake_done, battle_setup_done, battle_mode
    lines = msg.splitlines()
    msg_type = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_type:")), None)
    if not msg_type:
        print(f"[JOINER] Unknown message: {msg}")
        return

    if msg_type == "HANDSHAKE_RESPONSE":
        seed = int(next(line.split(":",1)[1].strip() for line in lines if line.startswith("seed:")))
        handshake_done = True
        print(f"[from HOST: HANDSHAKE_RESPONSE] Handshake done, seed={seed}")

    elif msg_type == "BATTLE_SETUP":
        comm_mode = next(line.split(":",1)[1].strip() for line in lines if line.startswith("communication_mode:"))
        pokemon_name = next(line.split(":",1)[1].strip() for line in lines if line.startswith("pokemon_name:"))
        boosts = json.loads(next(line.split(":",1)[1].strip() for line in lines if line.startswith("stat_boosts:")))
        pokemon = get_pokemon_by_name(pokemons, pokemon_name)
        battle_manager.setup_battle("HOST", pokemon, boosts, comm_mode)
        print(f"[from HOST] Received host's battle setup: Pokémon={pokemon.name}, mode={comm_mode}")
        battle_mode = comm_mode
        battle_setup_done = True

    elif msg_type == "ATTACK_ANNOUNCE":
        move_type = next(line.split(":",1)[1].strip() for line in lines if line.startswith("move_type:"))
        print(f"[BATTLE] ATTACK_ANNOUNCE from host: {move_type}")
        # Send DEFENSE_ANNOUNCE back
        sock.sendto("message_type: DEFENSE_ANNOUNCE\n".encode('utf-8'), host_addr)
        # Apply attack locally
        battle_manager.process_attack("HOST", "JOINER", move_type)

    elif msg_type == "DEFENSE_ANNOUNCE":
        print("[BATTLE] DEFENSE_ANNOUNCE received from host")

    elif msg_type == "BATTLE_UPDATE":
        print(f"[BATTLE UPDATE] {msg}")

    elif msg_type == "CHAT_MESSAGE":
        sender = next(line.split(":",1)[1].strip() for line in lines if line.startswith("sender_name:"))
        text = next(line.split(":",1)[1].strip() for line in lines if line.startswith("message_text:"))
        print(f"[CHAT from {sender}]: {text}")

    elif msg_type == "SPECTATOR_RESPONSE":
        print("[JOINER] Registered as spectator.")

# ------------------------
# User input
# ------------------------
def user_input_loop():
    global handshake_done, battle_setup_done, battle_mode
    while True:
        cmd = input("message_type: ").strip()
        if cmd == "HANDSHAKE_REQUEST":
            sock.sendto("message_type: HANDSHAKE_REQUEST\n".encode('utf-8'), host_addr)

        elif cmd == "BATTLE_SETUP":
            if not handshake_done:
                print("[JOINER] Cannot setup battle before handshake")
                continue
            name = input("pokemon_name: ").strip()
            pokemon = get_pokemon_by_name(pokemons, name)
            if not pokemon:
                print("[JOINER] Unknown Pokémon")
                continue
            boosts = {"special_attack_uses":4, "special_defense_uses":3}
            comm_mode = input("communication_mode (P2P/BROADCAST): ").strip()
            # Setup locally
            battle_manager.setup_battle("JOINER", pokemon, boosts, comm_mode)
            print("[JOINER] Battle setup done")
            battle_manager.show_battle("JOINER")
            # Send to host
            msg = f"message_type: BATTLE_SETUP\ncommunication_mode:{comm_mode}\npokemon_name:{pokemon.name}\nstat_boosts:{json.dumps(boosts)}\n"
            sock.sendto(msg.encode('utf-8'), host_addr)
            battle_setup_done = True
            battle_mode = comm_mode

        elif cmd.startswith("CHAT_MESSAGE"):
            text = cmd[len("CHAT_MESSAGE"):]
            msg = f"message_type: CHAT_MESSAGE\nsender_name: JOINER\ncontent_type: TEXT\nmessage_text: {text}\n"
            sock.sendto(msg.encode('utf-8'), host_addr)

        elif cmd.startswith("ATTACK_ANNOUNCE ") or cmd.startswith("SPECIAL_ATTACK ") or cmd.startswith("SPECIAL_DEFENSE "):
            move_type = cmd.split()[0]
            msg = f"message_type: ATTACK_ANNOUNCE\nmove_type:{move_type}\n"
            sock.sendto(msg.encode('utf-8'), host_addr)
            #battle_manager.process_attack("JOINER", "HOST", move_type)
        
        elif cmd == "DEFENSE_ANNOUNCE":
        
        elif cmd == "CALCULATION_REPORT":

        elif cmd == " CALCULATION_CONFIRM":
            
        elif cmd == "RESOLUTION_REQUEST":

        elif cmd == "exit":
            break

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    threading.Thread(target=user_input_loop).start()

if __name__ == "__main__":
    main()
