# test_battle.py
import threading
import time
import subprocess
import os

# Launch host.py and joiner.py as subprocesses
host_proc = subprocess.Popen(["python", "host.py"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
joiner_proc = subprocess.Popen(["python", "joiner.py"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

def read_output(proc, name):
    while True:
        line = proc.stdout.readline()
        if line:
            print(f"[{name}] {line.strip()}")

# Start threads to read outputs
threading.Thread(target=read_output, args=(host_proc, "HOST"), daemon=True).start()
threading.Thread(target=read_output, args=(joiner_proc, "JOINER"), daemon=True).start()

# Give some time for setup
time.sleep(2)

# Simulate handshake
joiner_proc.stdin.write("BATTLE_SETUP\n")
joiner_proc.stdin.flush()
joiner_proc.stdin.write("Pikachu\n")  # Pokémon name for joiner
joiner_proc.stdin.flush()
joiner_proc.stdin.write("P2P\n")      # communication mode
joiner_proc.stdin.flush()

host_proc.stdin.write("BATTLE_SETUP\n")
host_proc.stdin.flush()
host_proc.stdin.write("Bulbasaur\n")  # Pokémon name for host
host_proc.stdin.flush()
host_proc.stdin.write("P2P\n")
host_proc.stdin.flush()

time.sleep(2)

# Simulate some attacks
# Host attacks first
host_proc.stdin.write("ATTACK_ANNOUNCE Thunderbolt\n")
host_proc.stdin.flush()
time.sleep(1)

# Joiner attacks
joiner_proc.stdin.write("ATTACK_ANNOUNCE Tackle\n")
joiner_proc.stdin.flush()
time.sleep(1)

# Send a chat message from joiner
joiner_proc.stdin.write("CHAT_MESSAGE Hello Host!\n")
joiner_proc.stdin.flush()

# Send a chat message from host
host_proc.stdin.write("CHAT_MESSAGE Hello Joiner!\n")
host_proc.stdin.flush()

# Wait for some battle turns to complete
time.sleep(5)

# Exit both processes
host_proc.stdin.write("exit\n")
host_proc.stdin.flush()
joiner_proc.stdin.write("exit\n")
joiner_proc.stdin.flush()
