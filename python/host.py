import random
import threading
import time

from network import ReliableUDP
from messages import serialize_message, deserialize_message
from pokemon_data import load_pokemon_csv, get_pokemon
from battle_manager import BattleManager
from chat import create_text_message, create_sticker_message

from chat import create_text_message, create_sticker_message
from messages import serialize_message
peer_address = ("127.0.0.1", 9002)  # Joiner IP for host; host IP for joiner

sequence_number = 0

def input_loop():
    global sequence_number
    while True:
        user_input = input()
        if user_input.startswith("/chat "):
            msg = create_text_message("Me", user_input[6:], sequence_number)
        elif user_input.startswith("/sticker "):
            msg = create_sticker_message("Me", user_input[9:], sequence_number)
        else:
            print("Use /chat <message> or /sticker <filepath>")
            continue

        ReliableUDP.send_message(serialize_message(msg), peer_address)
        sequence_number += 1

threading.Thread(target=input_loop, daemon=True).start()

from chat import save_sticker_message
from messages import deserialize_message

def receive_loop():
    while True:
        data, addr = ReliableUDP.receive_message()
        msg = deserialize_message(data)
        if msg["message_type"] == "CHAT_MESSAGE":
            if msg["content_type"] == "TEXT":
                print(f"[CHAT {msg['sender_name']}] {msg['message_text']}")
            elif msg["content_type"] == "STICKER":
                path = save_sticker_message(msg)
                print(f"[CHAT {msg['sender_name']}] Sent a sticker! Saved as {path}")
        else:
            print(f"[RECEIVED] {msg}")

threading.Thread(target=receive_loop, daemon=True).start()

# ==============================
# Configuration
# ==============================
HOST_IP = "0.0.0.0"
HOST_PORT = 9002
VERBOSE = True
POKEMON_CSV = "pokemon.csv"  # your CSV file path

load_pokemon_csv(POKEMON_CSV)

# ==============================
# Networking setup
# ==============================
host = ReliableUDP(HOST_IP, HOST_PORT, verbose=VERBOSE)
print(f"[HOST] Listening on {HOST_IP}:{HOST_PORT}")

# ==============================
# Handshake with Joiner
# ==============================
data, addr = host.receive_message()
msg = deserialize_message(data)
if msg['message_type'] == "HANDSHAKE_REQUEST":
    seed = random.randint(1, 999999)
    handshake_response = serialize_message({
        "message_type": "HANDSHAKE_RESPONSE",
        "seed": seed
    })
    host.send_message(handshake_response, addr)
    print(f"[HOST] Handshake completed with {addr}, seed={seed}")
else:
    print("[HOST] Unexpected message during handshake")
    exit(1)

# ==============================
# BATTLE_SETUP
# ==============================
my_pokemon_name = input("Choose your Pokémon: ")
my_pokemon = get_pokemon(my_pokemon_name)
my_boosts = {"special_attack_uses": 5, "special_defense_uses": 5}

battle_setup = serialize_message({
    "message_type": "BATTLE_SETUP",
    "communication_mode": "P2P",
    "pokemon_name": my_pokemon_name,
    "stat_boosts": my_boosts
})
host.send_message(battle_setup, addr)

# Wait for Joiner's setup
data, _ = host.receive_message()
joiner_setup = deserialize_message(data)
joiner_pokemon_name = joiner_setup['pokemon_name']
joiner_pokemon = get_pokemon(joiner_pokemon_name)
joiner_boosts = joiner_setup['stat_boosts']

# ==============================
# Initialize Battle Manager
# ==============================
battle = BattleManager(my_pokemon, joiner_pokemon, my_boosts, joiner_boosts, seed=seed)
turn_owner = "host"  # host goes first

# ==============================
# Async chat listener
# ==============================
def listen_for_chat():
    while True:
        data, _ = host.receive_message()
        msg = deserialize_message(data)
        if msg['message_type'] == "CHAT_MESSAGE":
            if msg['content_type'] == "TEXT":
                print(f"[CHAT {msg['sender_name']}] {msg['message_text']}")
            elif msg['content_type'] == "STICKER":
                print(f"[CHAT {msg['sender_name']}] Sent a sticker! (length={len(msg['sticker_data'])} bytes)")

threading.Thread(target=listen_for_chat, daemon=True).start()

# ==============================
# Turn-based battle loop
# ==============================
sequence_number = 1
while battle.p1.hp > 0 and battle.p2.hp > 0:
    if turn_owner == "host":
        move_name = input("Your move: ")
        move_power = int(input("Move power: "))
        move_category = input("Move category (physical/special): ")

        # ATTACK_ANNOUNCE
        attack_msg = serialize_message({
            "message_type": "ATTACK_ANNOUNCE",
            "move_name": move_name,
            "sequence_number": sequence_number
        })
        host.send_message(attack_msg, addr)
        sequence_number += 1

        # DEFENSE_ANNOUNCE
        data, _ = host.receive_message()
        defense_msg = deserialize_message(data)
        if defense_msg['message_type'] != "DEFENSE_ANNOUNCE":
            print("[ERROR] Expected DEFENSE_ANNOUNCE")
            exit(1)

        # PROCESS TURN
        dmg = battle.apply_attack(battle.p1, battle.p2, move_power, move_category, battle.boosts1)

        # CALCULATION_REPORT
        report_msg = serialize_message({
            "message_type": "CALCULATION_REPORT",
            "attacker": battle.p1.name,
            "move_used": move_name,
            "remaining_health": battle.p1.hp,
            "damage_dealt": dmg,
            "defender_hp_remaining": battle.p2.hp,
            "status_message": f"{battle.p1.name} used {move_name}!",
            "sequence_number": sequence_number
        })
        host.send_message(report_msg, addr)
        sequence_number += 1

        # Wait for CALCULATION_CONFIRM
        data, _ = host.receive_message()
        confirm_msg = deserialize_message(data)
        if confirm_msg['message_type'] != "CALCULATION_CONFIRM":
            print("[ERROR] Expected CALCULATION_CONFIRM")
            exit(1)

        turn_owner = "joiner"
    else:
        # WAIT FOR JOINER ATTACK
        data, _ = host.receive_message()
        attack_msg = deserialize_message(data)
        if attack_msg['message_type'] != "ATTACK_ANNOUNCE":
            print("[ERROR] Expected ATTACK_ANNOUNCE")
            exit(1)

        # Send DEFENSE_ANNOUNCE
        defense_msg = serialize_message({
            "message_type": "DEFENSE_ANNOUNCE",
            "sequence_number": sequence_number
        })
        host.send_message(defense_msg, addr)
        sequence_number += 1

        # PROCESS TURN
        move_name = attack_msg['move_name']
        move_power = int(input(f"Opponent move power (for testing, enter 1-100): "))
        move_category = input(f"Opponent move category (physical/special): ")
        dmg = battle.apply_attack(battle.p2, battle.p1, move_power, move_category, battle.boosts2)

        # CALCULATION_REPORT
        report_msg = serialize_message({
            "message_type": "CALCULATION_REPORT",
            "attacker": battle.p2.name,
            "move_used": move_name,
            "remaining_health": battle.p2.hp,
            "damage_dealt": dmg,
            "defender_hp_remaining": battle.p1.hp,
            "status_message": f"{battle.p2.name} used {move_name}!",
            "sequence_number": sequence_number
        })
        host.send_message(report_msg, addr)
        sequence_number += 1

        # Send CALCULATION_CONFIRM
        confirm_msg = serialize_message({
            "message_type": "CALCULATION_CONFIRM",
            "sequence_number": sequence_number
        })
        host.send_message(confirm_msg, addr)
        sequence_number += 1

        turn_owner = "host"

# ==============================
# GAME_OVER
# ==============================
if battle.p1.hp <= 0:
    winner, loser = battle.p2.name, battle.p1.name
else:
    winner, loser = battle.p1.name, battle.p2.name

game_over_msg = serialize_message({
    "message_type": "GAME_OVER",
    "winner": winner,
    "loser": loser,
    "sequence_number": sequence_number
})
host.send_message(game_over_msg, addr)
print(f"[GAME OVER] Winner: {winner}, Loser: {loser}")