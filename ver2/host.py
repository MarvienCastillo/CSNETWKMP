# host.py
import socket
import threading
import json
import random
from pokemon_data import load_pokemon_csv, get_pokemon_by_name, MOVES, get_type_effectiveness

HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

joiners = {}       # addr -> info
spectators = {}    # addr -> info
battle_ready = {}  # addr -> bool

allow_attack_input = False
sequence_numbers = {}  # addr_or_HOST -> int

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', HOST_PORT))
print(f"[HOST] Listening on port {HOST_PORT}...")

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}  # key -> battle state ("HOST" or addr)

    def setup_battle(self, key, pokemon, boosts, mode):
        self.battles[key] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "mode": mode,
            "turn": 0,
            "current_player": "HOST",
            "battle_over": False,
            "hp": 100,
            "special_attack_uses": boosts.get("special_attack_uses", 4),
            "special_defense_uses": boosts.get("special_defense_uses", 3)
        }

    def show_battle(self, key):
        battle = self.battles.get(key)
        if not battle:
            print(f"[BATTLE] No battle for {key}")
            return
        print(f"[BATTLE][{key}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
              f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def calculate_damage(self, attacker_key, defender_key, move_name):
        attacker = self.battles.get(attacker_key)
        defender = self.battles.get(defender_key)
        if not attacker or not defender or defender["battle_over"]:
            return 0

        move = MOVES.get(move_name.lower())
        if not move:
            print(f"[BATTLE][ERROR] Unknown move {move_name}")
            return 0

        category = move["category"]
        base_power = move["power"]
        move_type = move["type"]

        if category == "physical":
            attacker_stat = attacker["pokemon"].attack
            defender_stat = defender["pokemon"].defense
        else:
            attacker_stat = attacker["pokemon"].sp_attack
            defender_stat = defender["pokemon"].sp_defense

        defender_types = [defender["pokemon"].type1]
        if defender["pokemon"].type2:
            defender_types.append(defender["pokemon"].type2)
        type_multiplier = get_type_effectiveness(move_type, defender_types)

        # Consume special attack uses
        if category == "special" and attacker["special_attack_uses"] > 0:
            attacker["special_attack_uses"] -= 1

        damage = max(1, int(((attacker_stat / max(1, defender_stat)) * base_power) * type_multiplier))

        defender["hp"] -= damage
        if defender["hp"] <= 0:
            defender["hp"] = 0
            defender["battle_over"] = True
        attacker["turn"] += 1

        # Toggle current player markers
        for k in ("HOST", defender_key):
            b = self.battles.get(k)
            if b:
                b["current_player"] = "JOINER" if b["current_player"] == "HOST" else "HOST"

        print(f"[BATTLE][HOST] {attacker['pokemon'].name} used {move_name} on {defender['pokemon'].name} "
              f"-> damage={damage}, HP remaining={defender['hp']}")
        if defender["battle_over"]:
            print(f"[BATTLE] {defender['pokemon'].name} fainted! Winner: {attacker['pokemon'].name}")
        return damage

battle_manager = BattleManager()

# ------------------------
# Listener
# ------------------------
def listen():
    global allow_attack_input
    while True:
        try:
            data, addr = sock.recvfrom(65536)
            msg = data.decode('utf-8').strip()
            if not msg:
                continue
            process_message(msg, addr)
        except Exception as e:
            print(f"[ERROR] Listening thread: {e}")

# ------------------------
# Message Processing
# ------------------------
def process_message(msg, addr):
    global allow_attack_input
    lines = msg.splitlines()
    msg_type = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_type:")), None)
    if not msg_type:
        print(f"[HOST] Unknown message from {addr}: {msg}")
        return

    # Ensure sequence number store exists
    if addr not in sequence_numbers:
        sequence_numbers[addr] = 0
    sequence_numbers["HOST"] = sequence_numbers.get("HOST", 0)

    if msg_type == "HANDSHAKE_REQUEST":
        seed = random.randint(0, 999999)
        response = f"message_type: HANDSHAKE_RESPONSE\nseed: {seed}\n"
        sock.sendto(response.encode('utf-8'), addr)
        joiners[addr] = joiners.get(addr, {})
        joiners[addr].update({"seed": seed, "battle_setup_done": False})
        sequence_numbers[addr] = 0
        print(f"[HOST] Handshake completed with {addr}, seed={seed}")
        return

    elif msg_type == "SPECTATOR_REQUEST":
        spectators[addr] = {"registered": True}
        response = "message_type: SPECTATOR_RESPONSE\n"
        sock.sendto(response.encode('utf-8'), addr)
        print(f"[HOST] Spectator registered: {addr}")
        return

    elif msg_type == "BATTLE_SETUP":
        try:
            comm_mode = next(line.split(":",1)[1].strip() for line in lines if line.startswith("communication_mode:"))
            pokemon_name = next(line.split(":",1)[1].strip() for line in lines if line.startswith("pokemon_name:"))
            boosts_json = next(line.split(":",1)[1].strip() for line in lines if line.startswith("stat_boosts:"))
            boosts = json.loads(boosts_json)

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

            # Setup battle for joiner
            battle_manager.setup_battle(addr, pokemon, boosts, comm_mode)
            if "HOST" not in battle_manager.battles:
                print("[HOST] You (HOST) must run BATTLE_SETUP to define your Pokémon and boosts.")
            print(f"[from JOINER] Battle setup from {addr}: Pokémon={pokemon.name}, mode={comm_mode}")

            joiners[addr]["battle_setup_done"] = True
            battle_ready[addr] = True

            # Enable host to attack if both sides ready
            if "HOST" in battle_manager.battles and all(battle_ready.values()):
                allow_attack_input = True
                print(f"[HOST] Both battle setups done. It's HOST's turn.")
        except Exception as e:
            print(f"[HOST] Failed to parse BATTLE_SETUP from {addr}: {e}")
        return

    elif msg_type == "ATTACK_ANNOUNCE":
        move_type = next((line.split(":",1)[1].strip() for line in lines if line.startswith("move_type:")), "ATTACK")
        print(f"[BATTLE] Received ATTACK_ANNOUNCE {move_type} from {addr}")

        # Apply attack: attacker=joiner addr, defender=HOST
        damage = battle_manager.calculate_damage(addr, "HOST", move_type)

        # Broadcast update
        msg_update = (
            f"message_type: BATTLE_UPDATE\n"
            f"player: JOINER\nmove: {move_type}\n"
            f"damage: {damage}\n"
            f"hp_host: {battle_manager.battles['HOST']['hp']}\n"
            f"hp_joiner: {battle_manager.battles[addr]['hp']}\n"
        )
        for t in list(joiners.keys()) + list(spectators.keys()):
            sock.sendto(msg_update.encode('utf-8'), t)

        if battle_manager.battles["HOST"]["battle_over"]:
            print(f"[BATTLE] Battle over. Winner: {addr}")
        allow_attack_input = True
        return

    elif msg_type == "CHAT_MESSAGE":
        sender_name = next((line.split(":",1)[1].strip() for line in lines if line.startswith("sender_name:")), "Unknown")
        text = next((line.split(":",1)[1].strip() for line in lines if line.startswith("message_text:")), "")
        print(f"[CHAT from {sender_name}]: {text}")
        return

    else:
        print(f"[HOST] Unhandled message_type={msg_type} from {addr}")

# ------------------------
# Utility
# ------------------------
def prompt_boosts():
    while True:
        try:
            sa = int(input("special_attack_uses: ").strip())
            sd = int(input("special_defense_uses: ").strip())
            if sa < 0 or sd < 0:
                raise ValueError()
            return {"special_attack_uses": sa, "special_defense_uses": sd}
        except ValueError:
            print("Invalid input. Enter non-negative integers.")

# ------------------------
# User input
# ------------------------
def user_input_loop():
    global allow_attack_input
    print(f"[HOST] Waiting for Handshake Request on port {HOST_PORT}...")
    while True:
        cmdline = input("message_type: ").strip()
        if not cmdline:
            continue
        parts = cmdline.split()
        cmd = parts[0]

        if cmd == "CHAT_MESSAGE":
            text = cmdline[len("CHAT_MESSAGE"):].strip()
            msg = f"message_type: CHAT_MESSAGE\nsender_name: HOST\ncontent_type: TEXT\nmessage_text: {text}\n"
            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg.encode('utf-8'), addr)
            continue

        if cmd == "BATTLE_SETUP":
            name = input("pokemon_name: ").strip()
            pokemon = get_pokemon_by_name(pokemons, name)
            if not pokemon:
                print("[HOST] Unknown Pokémon")
                continue
            comm_mode = input("communication_mode (P2P/BROADCAST): ").strip()
            boosts = prompt_boosts()
            battle_manager.setup_battle("HOST", pokemon, boosts, comm_mode)
            print("[HOST] Battle setup done")
            battle_manager.show_battle("HOST")
            # send to joiners
            msg = f"message_type: BATTLE_SETUP\ncommunication_mode: {comm_mode}\npokemon_name: {pokemon.name}\nstat_boosts: {json.dumps(boosts)}\n"
            for addr in joiners.keys():
                sock.sendto(msg.encode('utf-8'), addr)
            for a, info in joiners.items():
                if info.get("battle_setup_done"):
                    battle_ready[a] = True
            if all(battle_ready.values()) if battle_ready else False:
                allow_attack_input = True
                print("[HOST] Both battle setups done. You may attack.")
            continue

        if cmd == "ATTACK_ANNOUNCE":
            if not allow_attack_input:
                print("[HOST] Not your turn or battle not ready.")
                continue
            if len(parts) < 2:
                print("Usage: ATTACK_ANNOUNCE <move_name>")
                continue
            move_type = parts[1]
            if not joiners:
                print("[HOST] No joiner connected.")
                continue
            opponent_addr = next(iter(joiners.keys()))
            seq = sequence_numbers.get("HOST",0)+1
            sequence_numbers["HOST"] = seq
            msg = f"message_type: ATTACK_ANNOUNCE\nmove_type: {move_type}\nsequence_number: {seq}\n"
            sock.sendto(msg.encode('utf-8'), opponent_addr)
            damage = battle_manager.calculate_damage("HOST", opponent_addr, move_type)
            msg_update = (
                f"message_type: BATTLE_UPDATE\nplayer: HOST\nmove: {move_type}\n"
                f"damage: {damage}\n"
                f"hp_host: {battle_manager.battles['HOST']['hp']}\n"
                f"hp_joiner: {battle_manager.battles[opponent_addr]['hp']}\n"
            )
            for t in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(msg_update.encode('utf-8'), t)
            allow_attack_input = False
            continue

        if cmd == "exit":
            print("[HOST] Exiting...")
            break

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    threading.Thread(target=user_input_loop, daemon=True).start()
    while True:
        try:
            threading.Event().wait(1)
        except KeyboardInterrupt:
            break

if __name__ == "__main__":
    main()
