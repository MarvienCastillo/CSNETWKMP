# host.py
import socket
import threading
import json
import random
import os
import base64

from pokemon_data import load_pokemon_csv, get_pokemon_by_name, MOVES, get_type_effectiveness

HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

joiners = {}       # addr -> info
spectators = {}    # addr -> info
battle_ready = {}  # addr -> bool
allow_attack_input = False
sequence_numbers = {}  # addr or "HOST" -> last seq
verbose = True
MAX_UDP_SIZE = 60000
received_stickers = {}  # filename -> list of chunks

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', HOST_PORT))
print(f"[HOST] Listening on port {HOST_PORT}...")

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}

    def setup_battle(self, key, pokemon, boosts, mode):
        self.battles[key] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "hp": 50,
            "turn": 0,
            "current_player": "HOST" if key == "HOST" else "JOINER",
            "battle_over": False,
            "special_attack_uses": boosts.get("special_attack_uses", 4),
            "special_defense_uses": boosts.get("special_defense_uses", 3)
        }

    def show_battle(self, key):
        battle = self.battles.get(key)
        if battle:
            print(f"[BATTLE][{key}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
                  f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def calculate_damage(self, attacker, defender, move_name):
        move = MOVES.get(move_name.lower(), {"power": 1.0, "type": "Normal", "category": "physical"})
        if move["category"] == "physical":
            atk_stat = attacker["pokemon"].attack
            def_stat = defender["pokemon"].defense
        else:
            atk_stat = attacker["pokemon"].sp_attack
            def_stat = defender["pokemon"].sp_defense
        base_power = move.get("power", 1.0)
        type1_effect = get_type_effectiveness(move["type"], defender["pokemon"].type1)
        type2_effect = 1.0
        if defender["pokemon"].type2:
            type2_effect = get_type_effectiveness(move["type"], defender["pokemon"].type2)
        type_multiplier = type1_effect * type2_effect
        damage = max(1, int((atk_stat / max(1, def_stat)) * base_power * type_multiplier))
        status_message = f"{attacker['pokemon'].name} used {move_name}!"
        if type_multiplier > 1.0:
            status_message += " It was super effective!"
        elif 0 < type_multiplier < 1.0:
            status_message += " It was not very effective."
        elif type_multiplier == 0:
            status_message += " It had no effect!"
        return damage, status_message

    def process_attack(self, attacker_key, defender_key, move_name):
        attacker = self.battles.get(attacker_key)
        defender = self.battles.get(defender_key)
        if not attacker or not defender or attacker["battle_over"] or defender["battle_over"]:
            return 0, "Battle is over"
        damage, status_message = self.calculate_damage(attacker, defender, move_name)
        defender["hp"] -= damage
        attacker["turn"] += 1
        attacker["current_player"] = "JOINER" if attacker["current_player"] == "HOST" else "HOST"
        defender["current_player"] = "JOINER" if defender["current_player"] == "HOST" else "HOST"
        if defender["hp"] <= 0:
            defender["hp"] = 0
            defender["battle_over"] = True
            status_message += f" {defender['pokemon'].name} fainted! {attacker['pokemon'].name} wins!"
        return damage, status_message

battle_manager = BattleManager()

# ------------------------
# Utilities
# ------------------------
def toggle_verbose(cmd):
    global verbose
    verbose = cmd == "VERBOSE_ON"
    print(f"[HOST] Verbose mode {'ON' if verbose else 'OFF'}")

def send_ack(addr, seq):
    ack_msg = json.dumps({"message_type": "ACK", "sequence_number": seq})
    sock.sendto(ack_msg.encode(), addr)
    if verbose:
        print(f"[ACK SENT] seq={seq} to {addr}")

def handle_sticker_message(msg):
    fname = msg["filename"]
    chunk_no = msg["chunk_number"]
    total = msg["total_chunks"]
    data = msg["sticker_data"]
    if fname not in received_stickers:
        received_stickers[fname] = [None]*total
    received_stickers[fname][chunk_no-1] = data
    if all(received_stickers[fname]):
        os.makedirs("stickers", exist_ok=True)
        full_data = "".join(received_stickers[fname])
        with open(f"stickers/{fname}", "wb") as f:
            f.write(base64.b64decode(full_data))
        print(f"[CHAT] Sticker saved as stickers/{fname}")
        del received_stickers[fname]

# ------------------------
# Listener
# ------------------------
def listen():
    global allow_attack_input
    while True:
        try:
            data, addr = sock.recvfrom(65536)
            msg_raw = data.decode('utf-8').strip()
            if not msg_raw: continue
            process_message(msg_raw, addr)
        except Exception as e:
            print(f"[ERROR] Listening: {e}")

def process_message(msg_raw, addr):
    global allow_attack_input, sequence_numbers
    try:
        msg = json.loads(msg_raw)
    except:
        lines = msg_raw.splitlines()
        msg_type = next((l.split(":",1)[1].strip() for l in lines if l.startswith("message_type:")), None)
        msg = {"message_type": msg_type, **{l.split(":",1)[0]: l.split(":",1)[1].strip() for l in lines if ":" in l}}

    msg_type = msg.get("message_type")
    if not msg_type: return

    # initialize seq tracking
    if addr not in sequence_numbers:
        sequence_numbers[addr] = 0
    sequence_numbers["HOST"] = sequence_numbers.get("HOST", 0)

    if msg_type == "HANDSHAKE_REQUEST":
        seed = random.randint(0, 999999)
        response = json.dumps({"message_type": "HANDSHAKE_RESPONSE", "seed": seed})
        sock.sendto(response.encode(), addr)
        joiners[addr] = joiners.get(addr, {})
        joiners[addr].update({"seed": seed, "battle_setup_done": False})
        sequence_numbers[addr] = 0
        print(f"[HOST] Handshake completed with {addr}, seed={seed}")
        return

    elif msg_type == "SPECTATOR_REQUEST":
        spectators[addr] = {"registered": True}
        response = json.dumps({"message_type": "SPECTATOR_RESPONSE"})
        sock.sendto(response.encode(), addr)
        print(f"[HOST] Spectator registered: {addr}")
        return

    elif msg_type == "BATTLE_SETUP":
        pokemon_name = msg.get("pokemon_name")
        comm_mode = msg.get("communication_mode")
        boosts = json.loads(msg.get("stat_boosts", "{}"))
        pokemon = get_pokemon_by_name(pokemons, pokemon_name)
        joiners[addr] = joiners.get(addr, {})
        joiners[addr].update({"pokemon": pokemon, "boosts": boosts, "battle_setup_done": True, "communication_mode": comm_mode})
        battle_manager.setup_battle(addr, pokemon, boosts, comm_mode)
        battle_ready[addr] = True
        print(f"[from JOINER] Battle setup from {addr}: Pokémon={pokemon.name}, mode={comm_mode}")
        if "HOST" in battle_manager.battles and all(battle_ready.values()):
            allow_attack_input = True
            print(f"[HOST] Both battle setups done. It's HOST's turn.")
        return

    elif msg_type == "ATTACK_ANNOUNCE":
        move_name = msg.get("move_type")
        print(f"[BATTLE] ATTACK_ANNOUNCE {move_name} from {addr}")
        damage, status_message = battle_manager.process_attack(addr, "HOST", move_name)
        allow_attack_input = True
        sequence_numbers["HOST"] += 1
        report = {
            "message_type": "CALCULATION_REPORT",
            "attacker": battle_manager.battles[addr]["pokemon"].name,
            "move_used": move_name,
            "remaining_health": battle_manager.battles[addr]["hp"],
            "damage_dealt": damage,
            "defender_hp_remaining": battle_manager.battles["HOST"]["hp"],
            "status_message": status_message,
            "sequence_number": sequence_numbers["HOST"]
        }
        sock.sendto(json.dumps(report).encode(), addr)
        return

    elif msg_type == "CALCULATION_REPORT":
        print(f"[CALCULATION REPORT from {addr}]\n{msg}")
        seq = msg.get("sequence_number")
        if seq:
            send_ack(addr, seq)

    elif msg_type == "CHAT_MESSAGE":
        if msg.get("content_type") == "STICKER":
            handle_sticker_message(msg)
        else:
            sender = msg.get("sender_name")
            text = msg.get("message_text")
            print(f"[CHAT from {sender}]: {text}")
        seq = msg.get("sequence_number")
        if seq:
            send_ack(addr, seq)

    elif msg_type == "ACK" and verbose:
        print(f"[ACK RECEIVED] seq={msg.get('sequence_number')} from {addr}")

    else:
        print(f"[HOST] Unhandled message_type={msg_type} from {addr}")

# ------------------------
# User input
# ------------------------
def prompt_boosts():
    while True:
        try:
            sa = int(input("special_attack_uses: ").strip())
            sd = int(input("special_defense_uses: ").strip())
            if sa < 0 or sd < 0: raise ValueError()
            return {"special_attack_uses": sa, "special_defense_uses": sd}
        except ValueError:
            print("Invalid input. Enter non-negative integers.")

def send_sticker(addr, filename):
    if not os.path.isfile(filename):
        print(f"[HOST] Sticker file not found: {filename}")
        return
    with open(filename, "rb") as f:
        data = f.read()
    b64 = base64.b64encode(data).decode()
    chunks = [b64[i:i+MAX_UDP_SIZE] for i in range(0, len(b64), MAX_UDP_SIZE)]
    total_chunks = len(chunks)
    for idx, chunk in enumerate(chunks,1):
        sequence_numbers["HOST"] += 1
        msg = {
            "message_type": "CHAT_MESSAGE",
            "sender_name": "HOST",
            "content_type": "STICKER",
            "sticker_data": chunk,
            "chunk_number": idx,
            "total_chunks": total_chunks,
            "filename": os.path.basename(filename),
            "sequence_number": sequence_numbers["HOST"]
        }
        sock.sendto(json.dumps(msg).encode(), addr)
        if verbose:
            print(f"[STICKER SENT] {filename} chunk {idx}/{total_chunks}")

def user_input_loop():
    global allow_attack_input, sequence_numbers, verbose
    print(f"[HOST] Waiting for Handshake Request on port {HOST_PORT}...")
    while True:
        cmdline = input("message_type: ").strip()
        if not cmdline: continue
        parts = cmdline.split()
        cmd = parts[0]

        if cmd in ["VERBOSE_ON","VERBOSE_OFF"]:
            toggle_verbose(cmd)
            continue

        if cmd == "CHAT_MESSAGE":
            sequence_numbers["HOST"] += 1
            content_type = input("content_type (TEXT/STICKER): ").strip().upper()

            msg = {"message_type": "CHAT_MESSAGE",
                "sender_name": "HOST",
                "sequence_number": sequence_numbers["HOST"]}

            if content_type == "TEXT":
                text = input("message_text: ").strip()
                msg.update({
                    "content_type": "TEXT",
                    "message_text": text
                })
            elif content_type == "STICKER":
                sticker_path = input("sticker file path: ").strip()
                if not os.path.isfile(sticker_path):
                    print("[JOINER] File not found")
                    continue
                with open(sticker_path, "rb") as f:
                    data = f.read()
                b64_data = base64.b64encode(data).decode()
                # split into chunks
                chunks = [b64_data[i:i+MAX_UDP_SIZE] for i in range(0, len(b64_data), MAX_UDP_SIZE)]
                total_chunks = len(chunks)
                for idx, chunk in enumerate(chunks, 1):
                    sequence_number += 1
                    msg_chunk = {
                        "message_type": "CHAT_MESSAGE",
                        "sender_name": "JOINER",
                        "content_type": "STICKER",
                        "sticker_data": chunk,
                        "chunk_number": idx,
                        "total_chunks": total_chunks,
                        "filename": os.path.basename(sticker_path),
                        "sequence_number": sequence_number
                    }
                    sock.sendto(json.dumps(msg_chunk).encode(), addr)
                    if verbose:
                        print(f"[STICKER SENT] {sticker_path} chunk {idx}/{total_chunks}")

            else:
                print("[HOST] Invalid content_type")
                continue

            for addr in list(joiners.keys()) + list(spectators.keys()):
                sock.sendto(json.dumps(msg).encode(), addr)
            print(f"[HOST] {content_type} message sent. Seq={sequence_numbers['HOST']}")
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
            msg = {
                "message_type": "BATTLE_SETUP",
                "communication_mode": comm_mode,
                "pokemon_name": pokemon.name,
                "stat_boosts": json.dumps(boosts)
            }
            for addr in joiners.keys():
                sock.sendto(json.dumps(msg).encode(), addr)
                if joiners[addr].get("battle_setup_done"):
                    battle_ready[addr] = True
            if all(battle_ready.values()) if battle_ready else False:
                allow_attack_input = True
                print("[HOST] Both battle setups done. You may attack when ready.")
            continue

        if cmd == "ATTACK_ANNOUNCE":
            if not allow_attack_input:
                print("[HOST] Not your turn or battle not ready.")
                continue
            if len(parts) < 2:
                print("Usage: ATTACK_ANNOUNCE <move_name>")
                continue
            move_name = parts[1]
            opponent_addr = next(iter(joiners.keys()))
            sequence_numbers["HOST"] += 1
            msg = {
                "message_type": "ATTACK_ANNOUNCE",
                "move_type": move_name,
                "sequence_number": sequence_numbers["HOST"]
            }
            sock.sendto(json.dumps(msg).encode(), opponent_addr)
            battle_manager.process_attack("HOST", opponent_addr, move_name)
            allow_attack_input = False
            continue

        if cmd == "exit":
            print("[HOST] Exiting...")
            break

        print("[HOST] Unknown command.")

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
