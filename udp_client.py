import asyncio
import sys
import uuid
import time
from typing import Dict, Tuple, Optional, Any
import socket

# --- CONFIGURATION ---
HOST_IP = '127.0.0.1'
HOST_PORT = 9002
# MY_PORT will be set by the command line args
global MY_PORT
MY_PORT = 0 

# --- GLOBAL GAME STATE ---
class BattleState:
    def __init__(self):
        self.role = "JOINER" # Default, overridden in __main__
        self.is_spectator = False
        self.is_connected = False
        self.my_session_id = str(uuid.uuid4())
        self.host_address = (HOST_IP, HOST_PORT)
        
        # Address storage for peers (equivalent to C global structs)
        self.joiner_addr: Optional[Tuple[str, int]] = None 
        self.spectator_addrs: Dict[Tuple[str, int], float] = {} 
        
        self.seed = None
        self.running = True
        self.sequence_num = 0

STATE = BattleState()
# --- END GLOBAL STATE ---

# --- PROTOCOL UTILITIES ---

def get_message_type(message: str) -> Optional[str]:
    """Extracts the message_type from the plain text protocol format."""
    try:
        if not message:
            return None
        for line in message.split('\n'):
            if line.startswith("message_type:"):
                return line[len("message_type:"):].strip()
        return None
    except Exception:
        return None

def serialize_message(data: Dict) -> bytes:
    """Converts a Python dict into the PokeProtocol newline-separated plain text."""
    # Simplified sequence number logic for demonstration
    if 'sequence_number' not in data and data.get('message_type') not in ['ACK', 'SPECTATOR_REQUEST', 'HANDSHAKE_REQUEST']:
        STATE.sequence_num += 1
        data['sequence_number'] = STATE.sequence_num
    
    # Simple serialization: key: value\n
    message_str = ""
    for key, value in data.items():
        message_str += f"{key}: {value}\n"
        
    return message_str.encode('utf-8')

def deserialize_message(data: bytes) -> Dict:
    """Converts the raw bytes back into a Python dict."""
    message = data.decode('utf-8').strip()
    result = {}
    
    for line in message.split('\n'):
        if ':' in line:
            key, value = line.split(':', 1)
            result[key.strip()] = value.strip()
    return result

# --- UDP PROTOCOL CLASS (Network Layer) ---

class PokeProtocol(asyncio.DatagramProtocol):
    
    def __init__(self, queue: asyncio.Queue):
        self.transport = None
        self.message_queue = queue 

    def connection_made(self, transport):
        self.transport = transport
        sockname = self.transport.get_extra_info('sockname')
        print(f"UDP Socket ready. Listening on {sockname}")
        
        if STATE.role != "HOST":
            self.send_initial_request()

    def datagram_received(self, data, addr):
        try:
            message_data = deserialize_message(data)
            self.message_queue.put_nowait((message_data, addr))
        except Exception as e:
            print(f"[ERROR] Failed to deserialize message: {e}")

    def send_data(self, data: Dict, addr: Tuple):
        """Helper to serialize and send data."""
        if self.transport:
            packet = serialize_message(data)
            self.transport.sendto(packet, addr)

    def send_initial_request(self):
        """Sends HANDSHAKE_REQUEST or SPECTATOR_REQUEST."""
        if STATE.is_spectator:
            msg = {'message_type': 'SPECTATOR_REQUEST', 'session_id': STATE.my_session_id}
        else:
            msg = {'message_type': 'HANDSHAKE_REQUEST', 'session_id': STATE.my_session_id}
            
        print(f"Sending initial request: {msg['message_type']} to {STATE.host_address}")
        self.send_data(msg, STATE.host_address)
    
    def spectator_update(self, update_message: str):
        """Equivalent to C's spectatorUpdate() function, sending update to all spectators."""
        if not STATE.spectator_addrs:
            return
            
        update_packet = serialize_message({'message_type': 'UPDATE_RELAY', 'data': update_message})
        
        # Send update to all currently connected spectators
        for addr in STATE.spectator_addrs.keys():
            if self.transport:
                self.transport.sendto(update_packet, addr)
                print(f"[HOST] Update sent to spectator at {addr}.")

# --- MAIN GAME THREAD (State Machine) ---

async def handle_host_handshake(data: Dict, addr: Tuple, protocol: PokeProtocol):
    """Handles connection requests when running as HOST (Host Peer Logic)."""
    msg_type = data.get('message_type')
    
    if msg_type in ['HANDSHAKE_REQUEST', 'SPECTATOR_REQUEST']:
        
        if msg_type == 'HANDSHAKE_REQUEST':
            # C Logic: Store JoinerADDR
            STATE.joiner_addr = addr
            STATE.seed = int(time.time() * 1000) % 100000 
            print(f"[HOST] Handshake Request received from {addr[0]}:{addr[1]}")
            
            # Send HANDSHAKE_RESPONSE
            response = {'message_type': 'HANDSHAKE_RESPONSE', 'seed': STATE.seed}
            protocol.send_data(response, addr)
            print(f"[HOST] A Handshake response has been sent with a seed of {STATE.seed}")
            
            # Notify spectator of new player
            protocol.spectator_update("A new challenger has joined the battle!", protocol)
            
            STATE.is_connected = True
            
        elif msg_type == 'SPECTATOR_REQUEST':
            # C Logic: Store SpectatorADDR
            STATE.spectator_addrs[addr] = time.time() # Store address for updates
            print(f"[HOST] Spectator request received from {addr[0]}:{addr[1]}")
            
            # Send SPECTATOR_RESPONSE
            response = {'message_type': 'HANDSHAKE_RESPONSE', 'is_spectator': 'True'}
            
            # Use the actual address (addr) for the reply
            protocol.send_data(response, addr)
            print("[HOST] A Spectator response has been sent!")
            
    else:
        # Example: Relay all other messages (e.g., CHAT_MESSAGE, ATTACK_ANNOUNCE)
        print(f"[HOST] Received non-handshake message from {addr}: {msg_type}")
        protocol.spectator_update(f"Received message: {msg_type}", protocol)

async def handle_joiner_handshake(data: Dict, addr: Tuple, protocol: PokeProtocol):
    """Handles responses when running as JOINER/SPECTATOR (Client Peer Logic)."""
    msg_type = data.get('message_type')
    
    if msg_type == 'HANDSHAKE_RESPONSE':
        
        if data.get('is_spectator') == 'True':
            # C Logic: isSpectator = true
            STATE.is_spectator = True
            print("[CLIENT] Confirmed role: SPECTATOR. Battle commands removed.")
        else:
            # C Logic: Extract seed using sscanf equivalent
            try:
                STATE.seed = int(data.get('seed'))
                print(f"[CLIENT] Handshake complete. Seed is {STATE.seed}")
            except (TypeError, ValueError):
                print("[ERROR] Failed to extract seed from HANDSHAKE_RESPONSE.")
            
        STATE.is_connected = True

    elif msg_type == 'UPDATE_RELAY':
        # Handles messages relayed by the Host (e.g., chat from the Joiner/Host)
        print(f"[HOST UPDATE]: {data.get('data')}")
    
    elif msg_type == 'BATTLE_SETUP':
        print(f"[CLIENT] Received opponent's setup: {data.get('pokemon_name')}. Ready to battle!")
    
    # ... Add handlers for ATTACK_ANNOUNCE, CALCULATION_REPORT, etc.

async def game_loop(protocol: PokeProtocol, queue: asyncio.Queue):
    """The main game thread/task for processing network messages."""
    
    print(f"Running as: {STATE.role}. Session ID: {STATE.my_session_id[:8]}")
    
    while STATE.running:
        try:
            # AWAITING I/O: This pauses until a message arrives (Non-blocking recvfrom equivalent)
            data, addr = await asyncio.wait_for(queue.get(), timeout=0.1)
            
            # --- PROCESS INCOMING NETWORK MESSAGE ---
            if STATE.role == "HOST":
                await handle_host_handshake(data, addr, protocol)
            else:
                await handle_joiner_handshake(data, addr, protocol)
                
        except asyncio.TimeoutError:
            # Expected during idle times; allows the loop to check for console input
            pass
        except Exception as e:
            print(f"[CRITICAL ERROR in Game Loop]: {e}")
            STATE.running = False


async def console_input(protocol: PokeProtocol):
    """Reads user input asynchronously (Replaces blocking fgets())."""
    print("\n--- Console Input Ready ---")
    while STATE.running:
        try:
            # AWAITING I/O: Pauses until the user types a line and presses Enter.
            line = await asyncio.to_thread(sys.stdin.readline)
            command = line.strip()

            if not STATE.is_connected and command not in ['HANDSHAKE_REQUEST', 'SPECTATOR_REQUEST', 'exit']:
                print("Error: Not yet connected. Type HANDSHAKE_REQUEST or SPECTATOR_REQUEST.")
                continue

            if command.lower() == 'exit':
                STATE.running = False
                break
            
            if command == "":
                continue

            # C Logic: Serialization and Sendto
            sender_name = STATE.role
            dest_addr = STATE.host_address
            
            # Handling BATTLE_SETUP for demonstration
            if command.upper() == "BATTLE_SETUP":
                print("Please enter basic setup data (e.g., Pikachu, P2P):")
                pokemon_name = (await asyncio.to_thread(input, "Pokemon Name: ")).strip()
                comm_mode = (await asyncio.to_thread(input, "Comm Mode (P2P/BROADCAST): ")).strip()

                setup_msg = {
                    'message_type': 'BATTLE_SETUP', 
                    'pokemon_name': pokemon_name, 
                    'communication_mode': comm_mode or 'P2P'
                }
                protocol.send_data(setup_msg, dest_addr)
                print(f"[YOU] Sent BATTLE_SETUP for {pokemon_name}.")
                
            else:
                # Default to CHAT_MESSAGE
                chat_msg = {
                    'message_type': 'CHAT_MESSAGE', 
                    'sender_name': sender_name, 
                    'message_text': command
                }
                protocol.send_data(chat_msg, dest_addr)
                print(f"[YOU] Sent Chat: {command}")
                
        except Exception as e:
            print(f"[CRITICAL INPUT ERROR]: {e}")
            STATE.running = False

async def main():
    """Sets up the event loop and starts the parallel tasks."""
    loop = asyncio.get_running_loop()
    
    message_queue = asyncio.Queue()
    
    local_addr = (HOST_IP, MY_PORT if STATE.role == "HOST" else 0)
    
    try:
        transport, protocol = await loop.create_datagram_endpoint(
            lambda: PokeProtocol(message_queue),
            local_addr=local_addr
        )
        
        input_task = asyncio.create_task(console_input(protocol))
        game_task = asyncio.create_task(game_loop(protocol, message_queue))
        
        await asyncio.gather(input_task, game_task)

    finally:
        if 'transport' in locals():
            transport.close()
        print("\nApplication Shut Down.")

if __name__ == '__main__':
    # Command line argument setup
    if len(sys.argv) > 1:
        role_arg = sys.argv[1].lower()
        if role_arg == 'host':
            STATE.role = "HOST"
        elif role_arg == 'joiner':
            STATE.role = "JOINER"
        elif role_arg == 'spectator':
            STATE.role = "SPECTATOR"
            STATE.is_spectator = True
        else:
            STATE.role = "HOST" # Defaulting if unknown
    
    # Ensure correct port is set based on role
    if STATE.role == "HOST":
        MY_PORT = HOST_PORT
    elif STATE.role in ["JOINER", "SPECTATOR"]:
        MY_PORT = 0 # Ephemeral port

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nApplication stopped by user.")
    except Exception as e:
         print(f"An unexpected error occurred during execution: {e}")