# CSNETWKMP
CSNETWKMP – Pokémon-Themed UDP Multiplayer Battle System

This project implements a multiplayer Pokémon-style battle system using UDP networking in C, designed for the LSNP PokeProtocol implementation. One machine acts as a Host, while another acts as a Joiner, enabling two players to engage in turn-based combat following an RFC-like protocol.

The system includes Pokémon data parsing, battle sequencing, core network logic, a reliability layer with ACKs and retransmission, and extended features such as chat and sticker messaging.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------

How to compile the code:
  gcc <filename> BattleManager.c -o host.exe -lws2_32
  gcc <filename> BattleManager.c -o joiner.exe -lws2_32
**Note: Change the filename to the main file which is in this case udp_host.c and udp_joiner.c**
To run the code, please run it in this order:
1. Host
2. Joiner

**Important Note: If you are playing the game, kindly send BATTLE_SETUP from joiner first before sending from HOST to detect the pokemon**


--------------------------------------------------------------------------------------------------------------------------------------------------------------------

PROJECT STRUCTURE

1. battle_setup.c:         # Pokémon data loading & setup utilities
  
2. battle_setup.h
   
3. BattleManager.c         # Turn-based battle flow, damage handling, win/loss logic

4. BattleManager.h

5. pokemon_data.h          # Pokémon structs and constants

6. pokemon.csv             # Pokémon stats database

7. udp_host.c              # Host UDP logic

8. udp_host1.c             # Alternate UDP host implementation

9. udp_joiner.c            # Joiner UDP logic
10. udp_joiner1.c           # Alternate UDP joiner implementation

11. joiner.exe              # Precompiled Windows client
12. host.exe                # Precompiled Windows host

13. pikachu.png             # Image asset

14. compileAndrunTutorial.txt

15. .vscode/                # VSCode task and settings configuration

--------------------------------------------------------------------------------------------------------------------------------------------------------------------

Project Overview
1. Pokémon Data Loading:
battle_setup.c parses pokemon.csv and constructs Pokémon structures used throughout the battle.

2. Battle Flow (BattleManager):
Handles:
  Turn order
  Attack/defense announcement steps
  Damage calculation
  State syncing
  Win/Loss determination

3. UDP Networking
Two peers communicate using plain-text newline-delimited key:value messages.

Files:
  udp_host.c, udp_joiner.c
  Optional alternates: udp_host1.c, udp_joiner1.c

4. Reliability Layer
Adds:
  Sequence numbers
  ACK messaging
  Retransmission with timeout and retry counter
  Connection failure handling

5. Features
Includes:
  Verbose mode
  Asynchronous chat
  Base64 sticker sending
  Spectator mode
  Optional broadcast discovery

Team Task Distribution & Project Plan (PokeProtocol – LSNP)
A detailed report of the tasks implemented by each team member is documented below. If uneven participation is suspected, instructors may request individual explanations. Teams are allowed to drop any non-participating members during submission.

| Member                         | Primary Focus Area          | Key Responsibilities                                                 | Rubric Alignment  
| ------------------------------ | --------------------------- | -------------------------------------------------------------------- | ----------------------------
| Marvien Angel C. Castillo      | Network Core & Setup        | UDP socket setup, handshake logic, message parsing & serialization   | UDP Sockets & Handshake     
| Rob Nigel V. Rogacion          | Reliability Layer           | Sequence numbering, ACK logic, retransmission, connection health     | Sequence Numbers, ACKs, Retransmission
| Betina Heart R. Rodrigo        | Game State & Turn Flow      | Turn-based state machine, 4-step flow, syncing, discrepancy handling | Turn Sequence, Sync, Resolution        
| Nikki Benedict E. Maningas     | Features, UI & Calculations | Damage calculation, chat system, sticker handling, verbose mode      | Damage Calc, Chat, UI, Verbose Mode 

--------------------------------------------------------------------------------------------------------------------------------------------------------------------

AI Usage Section
Artificial Intelligence tools were used responsibly as part of the development workflow for this project.
AI assistance contributed to:
  Generating boilerplate code structures
  Helping debug segmentation faults, malformed packet-handling logic, and message-parsing issues
  Improving readability and refactoring portions of the code
  Suggesting fixes for pointer handling, array bounds, and Base64 errors
  Automatically generating or cleaning up documentation sections
  Formatting this README, task matrix, and project outline

Important Notes:
  All AI-assisted outputs were manually reviewed, edited, and validated by the team.
  No confidential, personal, or private data was provided to AI tools.
  AI was used strictly as a development aid — the final implementation, debugging, and validation were performed by the team.
