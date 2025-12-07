# joiner.py
import socket
import threading
import json
import sys
from pokemon_data import load_pokemon_csv, get_pokemon_by_name, MOVES, get_type_effectiveness

HOST_IP = "127.0.0.1"
HOST_PORT = 9002
pokemons = load_pokemon_csv("pokemon.csv")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 0))
host_addr = (HOST_IP, HOST_PORT)

handshake_done = False
battle_setup_done = False
battle_mode = None
sequence_number = 0
allow_attack_input = False

# ------------------------
# BattleManager
# ------------------------
class BattleManager:
    def __init__(self):
        self.battles = {}  # "HOST" or "JOINER"

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

        # Apply damage
        defender["hp"] -= damage
        attacker["turn"] += 1

        # Switch turn
        attacker["current_player"] = "JOINER" if attacker["current_player"] == "HOST" else "HOST"
        defender["current_player"] = "JOINER" if defender["current_player"] == "HOST" else "HOST"

        # Battle over check
        if defender["hp"] <= 0:
            defender["hp"] = 0
            defender["battle_over"] = True
            status_message += f" {defender['pokemon'].name} fainted! {attacker['pokemon'].name} wins!"

        return damage, status_message

battle_manager = BattleManager()

# ------------------------
# Listener
# ------------------------
def listen():
    global handshake_done, battle_setup_done, battle_mode, allow_attack_input, sequence_number
    while True:
        try:
            data, addr = sock.recvfrom(65536)
            msg = data.decode('utf-8').strip()
            if not msg: continue
            process_message(msg, addr)
        except Exception as e:
            print(f"[ERROR] Listening: {e}")

def process_message(msg, addr):
    global handshake_done, battle_setup_done, battle_mode, allow_attack_input, sequence_number
    lines = msg.splitlines()
    msg_type = next((l.split(":",1)[1].strip() for l in lines if l.startswith("message_type:")), None)
    if not msg_type: return

    if msg_type == "HANDSHAKE_RESPONSE":
        seed = int(next(l.split(":",1)[1].strip() for l in lines if l.startswith("seed:")))
        handshake_done = True
        print(f"[from HOST: HANDSHAKE_RESPONSE] Handshake done, seed={seed}")

    elif msg_type == "BATTLE_SETUP":
        comm_mode = next(l.split(":",1)[1].strip() for l in lines if l.startswith("communication_mode:"))
        pokemon_name = next(l.split(":",1)[1].strip() for l in lines if l.startswith("pokemon_name:"))
        boosts = json.loads(next(l.split(":",1)[1].strip() for l in lines if l.startswith("stat_boosts:")))
        pokemon = get_pokemon_by_name(pokemons, pokemon_name)
        battle_manager.setup_battle("HOST", pokemon, boosts, comm_mode)
        battle_mode = comm_mode
        battle_setup_done = True
        allow_attack_input = True
        print(f"[from HOST] Received host's setup: Pokémon={pokemon.name}, mode={comm_mode}")

    elif msg_type == "ATTACK_ANNOUNCE":
        move_type = next(l.split(":",1)[1].strip() for l in lines if l.startswith("move_type:"))
        print(f"[BATTLE] ATTACK_ANNOUNCE from host: {move_type}")
        sock.sendto("message_type: DEFENSE_ANNOUNCE\n".encode(), host_addr)

        if "JOINER" in battle_manager.battles:
            damage, status_message = battle_manager.process_attack("HOST","JOINER",move_type)

            # Send CALCULATION_REPORT
            sequence_number += 1
            report = (
                f"message_type: CALCULATION_REPORT\n"
                f"attacker: {battle_manager.battles['HOST']['pokemon'].name}\n"
                f"move_used: {move_type}\n"
                f"remaining_health: {battle_manager.battles['HOST']['hp']}\n"
                f"damage_dealt: {damage}\n"
                f"defender_hp_remaining: {battle_manager.battles['JOINER']['hp']}\n"
                f"status_message: {status_message}\n"
                f"sequence_number: {sequence_number}\n"
            )
            sock.sendto(report.encode(), host_addr)

            if battle_manager.battles["JOINER"]["battle_over"]:
                print("[JOINER] Battle over! Winner:", battle_manager.battles["HOST"]["pokemon"].name)
                allow_attack_input = False
                threading.Timer(5.0, lambda: sys.exit(0)).start()

    elif msg_type == "CALCULATION_REPORT":
        # Confirm calculation
        sequence_number += 1
        confirm = f"message_type: CALCULATION_CONFIRM\nsequence_number: {sequence_number}\n"
        sock.sendto(confirm.encode(), host_addr)
        print(f"[CALCULATION REPORT from HOST]\n{msg}")

    elif msg_type == "RESOLUTION_REQUEST":
        print(f"[RESOLUTION REQUEST from HOST]\n{msg}")

    elif msg_type == "CHAT_MESSAGE":
        sender = next(l.split(":",1)[1].strip() for l in lines if l.startswith("sender_name:"))
        text = next(l.split(":",1)[1].strip() for l in lines if l.startswith("message_text:"))
        print(f"[CHAT from {sender}]: {text}")

    else:
        print(f"[JOINER] Unhandled message_type={msg_type}")

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

def user_input_loop():
    global handshake_done, battle_setup_done, battle_mode, allow_attack_input, sequence_number
    while True:
        cmdline = input("message_type: ").strip()
        if not cmdline: continue
        parts = cmdline.split()
        cmd = parts[0]

        if cmd == "HANDSHAKE_REQUEST":
            sock.sendto("message_type: HANDSHAKE_REQUEST\n".encode(), host_addr)
            continue

        if cmd == "BATTLE_SETUP":
            if not handshake_done:
                print("[JOINER] Cannot setup before handshake")
                continue
            name = input("pokemon_name: ").strip()
            pokemon = get_pokemon_by_name(pokemons, name)
            if not pokemon:
                print("[JOINER] Unknown Pokémon")
                continue
            comm_mode = input("communication_mode (P2P/BROADCAST): ").strip()
            boosts = prompt_boosts()
            battle_manager.setup_battle("JOINER", pokemon, boosts, comm_mode)
            battle_setup_done = True
            allow_attack_input = True
            msg = f"message_type: BATTLE_SETUP\ncommunication_mode: {comm_mode}\npokemon_name: {pokemon.name}\nstat_boosts: {json.dumps(boosts)}\n"
            sock.sendto(msg.encode(), host_addr)
            battle_manager.show_battle("JOINER")
            continue

        if cmd == "ATTACK_ANNOUNCE":
            if not battle_setup_done:
                print("[JOINER] Run BATTLE_SETUP first.")
                continue
            if not allow_attack_input:
                print("[JOINER] Not your turn.")
                continue
            if len(parts) < 2:
                print("Usage: ATTACK_ANNOUNCE <ATTACK|SPECIAL_ATTACK|SPECIAL_DEFENSE>")
                continue
            move_type = parts[1].upper()
            sequence_number += 1
            msg = f"message_type: ATTACK_ANNOUNCE\nmove_type: {move_type}\nsequence_number: {sequence_number}\n"
            sock.sendto(msg.encode(), host_addr)

            # locally apply JOINER -> HOST
            if "HOST" in battle_manager.battles:
                damage, status_message = battle_manager.process_attack("JOINER", "HOST", move_type)
                # send CALCULATION_REPORT
                sequence_number += 1
                report = (
                    f"message_type: CALCULATION_REPORT\n"
                    f"attacker: {battle_manager.battles['JOINER']['pokemon'].name}\n"
                    f"move_used: {move_type}\n"
                    f"remaining_health: {battle_manager.battles['JOINER']['hp']}\n"
                    f"damage_dealt: {damage}\n"
                    f"defender_hp_remaining: {battle_manager.battles['HOST']['hp']}\n"
                    f"status_message: {status_message}\n"
                    f"sequence_number: {sequence_number}\n"
                )
                sock.sendto(report.encode(), host_addr)
                allow_attack_input = False
            continue

        if cmd == "CHAT_MESSAGE":
            text = cmdline[len("CHAT_MESSAGE"):].strip()
            msg = f"message_type: CHAT_MESSAGE\nsender_name: JOINER\ncontent_type: TEXT\nmessage_text: {text}\n"
            sock.sendto(msg.encode(), host_addr)
            continue

        if cmd == "exit":
            print("[JOINER] Exiting...")
            sys.exit(0)

        print("[JOINER] Unknown command.")

# ------------------------
# Main
# ------------------------
def main():
    threading.Thread(target=listen, daemon=True).start()
    user_input_loop()  # run blocking in main thread

if __name__ == "__main__":
    main()
