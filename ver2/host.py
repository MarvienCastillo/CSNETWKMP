# host.py
import socket
import threading
import json
import random
from pokemon_data import load_pokemon_csv, get_pokemon_by_name

HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

joiners = {}       # addr -> info
spectators = {}    # addr -> info
battle_ready = {}  # addr -> bool
allow_attack_input = False

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', HOST_PORT))
print(f"[HOST] Listening on port {HOST_PORT}...")

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}  # addr -> battle state

    def setup_battle(self, addr, pokemon, boosts, mode):
        self.battles[addr] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "mode": mode,
            "turn": 0,
            "current_player": "HOST",
            "battle_over": False,
            "hp": pokemon.hp,
            "special_attack_uses": boosts.get("special_attack_uses",4),
            "special_defense_uses": boosts.get("special_defense_uses",3)
        }

    def show_battle(self, addr):
        battle = self.battles.get(addr)
        if not battle:
            print(f"[BATTLE] No battle for {addr}")
            return
        print(f"[BATTLE][{addr}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
              f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def process_attack(self, attacker_addr, defender_addr, move_type):
        battle = self.battles.get(defender_addr)
        if not battle or battle["battle_over"]:
            return 0

        damage = 0
        attacker = self.battles[attacker_addr]
        if move_type == "ATTACK":
            damage = max(1, attacker["pokemon"].attack - battle["pokemon"].defense)
        elif move_type == "SPECIAL_ATTACK" and attacker["special_attack_uses"] > 0:
            damage = max(1, attacker["pokemon"].sp_attack - battle["pokemon"].sp_defense)
            attacker["special_attack_uses"] -= 1
        elif move_type == "SPECIAL_DEFENSE" and attacker["special_defense_uses"] > 0:
            heal = min(attacker["pokemon"].sp_defense, attacker["pokemon"].hp - attacker["hp"])
            attacker["hp"] += heal
            attacker["special_defense_uses"] -= 1
            print(f"[BATTLE][HOST] {attacker['pokemon'].name} healed {heal} HP")
            return 0

        battle["hp"] -= damage
        print(f"[BATTLE][HOST] {attacker['pokemon'].name} {move_type} -> {battle['pokemon'].name} damage={damage}, HP={battle['hp']}")
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
# Message processing
# ------------------------
def process_message(msg, addr):
    lines = msg.splitlines()
    msg_type = None
    for line in lines:
        if line.startswith("message_type:"):
            msg_type = line.split(":",1)[1].strip()
            break
    if not msg_type:
        print(f"[HOST] Unknown message from {addr}: {msg}")
        return
    
    if msg_type == "HANDSHAKE_REQUEST":
        seed = random.randint(0, 999999)
        response = f"message_type: HANDSHAKE_RESPONSE\nseed:{seed}\n"
        sock.sendto(response.encode('utf-8'), addr)
        joiners[addr] = joiners.get(addr, {})
        joiners[addr].update({"seed": seed, "battle_setup_done": False})
        print(f"[HOST] Handshake completed with {addr}, seed={seed}")
        return

    elif msg_type == "SPECTATOR_REQUEST":
        spectators[addr] = {"registered": True}
        response = "message_type: SPECTATOR_RESPONSE\n"
        sock.sendto(response.encode('utf-8'), addr)
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

            joiners[addr] = joiners.get(addr, {})
            joiners[addr].update({
                "pokemon": pokemon,
                "boosts": boosts,
                "communication_mode": comm_mode,
                "battle_setup_done": True
            })

            # Setup battle
            battle_manager.setup_battle(addr, pokemon, boosts, comm_mode)
            print(f"[HOST] Battle setup from {addr}: Pokémon={pokemon.name}, mode={comm_mode}")
            battle_manager.show_battle(addr)

            joiners[addr]["battle_setup_done"] = True
            battle_ready[addr] = True

            # Check if host's battle is also setup
            if "HOST" in battle_manager.battles and all(battle_ready.values()):
                print(f"[HOST] Both battle setups done. It's your turn to attack. Use ATTACK_ANNOUNCE/DEFENSE_ANNOUNCE/SPECIAL_ATTACK/SPECIAL_DEFENSE commands.")
                # block other non-chat inputs until an attack is made
                allow_attack_input = True

        except Exception as e:
            print(f"[HOST] Failed to parse BATTLE_SETUP from {addr}: {e}")

    elif msg_type == "ATTACK_ANNOUNCE":
        move_type = next(line.split(":",1)[1].strip() for line in lines if line.startswith("move_type:"))
        print(f"[BATTLE] Received ATTACK_ANNOUNCE {move_type} from {addr}")
        # Respond with DEFENSE_ANNOUNCE
        sock.sendto("message_type: DEFENSE_ANNOUNCE\n".encode('utf-8'), addr)

        # Apply attack: joiner -> host
        damage = battle_manager.process_attack(addr, "HOST", move_type)

        # Broadcast update to everyone
        msg_update = f"message_type: BATTLE_UPDATE\nplayer:JOINER\nmove:{move_type}\ndamage:{damage}\nhp_host:{battle_manager.battles['HOST']['hp']}\nhp_joiner:{battle_manager.battles[addr]['hp']}\n"
        for t in list(joiners.keys()) + list(spectators.keys()):
            sock.sendto(msg_update.encode('utf-8'), t)

        if battle_manager.battles["HOST"]["battle_over"]:
            print(f"[BATTLE] Battle over. Winner: {addr}")

    elif msg_type == "CHAT_MESSAGE":
        sender_name = next((line.split(":",1)[1].strip() for line in lines if line.startswith("sender_name:")), "Unknown")
        text = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_text:")), "")
        print(f"[CHAT from {sender_name}]: {text}")

# ------------------------
# User input
# ------------------------
def user_input_loop():
    print(f"[HOST] Waiting for Handshake Request on port {HOST_PORT}...")
    while True:
        cmd = input("message_type: ").strip()
        if cmd.startswith("CHAT_MESSAGE"):
            text = cmd[len("CHAT_MESSAGE"):]
            msg = f"message_type: CHAT_MESSAGE\nsender_name: HOST\ncontent_type: TEXT\nmessage_text: {text}\n"
            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg.encode('utf-8'), addr)

        elif cmd == "BATTLE_SETUP":
            name = input("pokemon_name: ").strip()
            pokemon = get_pokemon_by_name(pokemons, name)
            if not pokemon:
                print("[HOST] Unknown Pokémon")
                continue
            boosts = {"special_attack_uses":4, "special_defense_uses":3}
            comm_mode = input("communication_mode (P2P/BROADCAST): ").strip()
            # Setup locally
            battle_manager.setup_battle("HOST", pokemon, boosts, comm_mode)
            print("[HOST] Battle setup done")
            battle_manager.show_battle("HOST")
            # Send to joiner
            msg = f"message_type: BATTLE_SETUP\ncommunication_mode:{comm_mode}\npokemon_name:{pokemon.name}\nstat_boosts:{json.dumps(boosts)}\n"
            for addr in joiners.keys():
                sock.sendto(msg.encode('utf-8'), addr)

        elif not allow_attack_input:
            print("[HOST] Wait for your turn to attack before issuing battle commands.")
            continue

        elif cmd == "ATTACK_ANNOUNCE":

        elif cmd == "DEFENSE_ANNOUNCE":
        
        elif cmd == "CALCULATION_REPORT":

        elif cmd == " CALCULATION_CONFIRM":

        elif cmd == "RESOLUTION_REQUEST":

        elif cmd == "exit":
            print("[HOST] Exiting...")
            break

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    threading.Thread(target=user_input_loop).start()

if __name__ == "__main__":
    main()
