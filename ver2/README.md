# CSNETWKMP
CSNETWKMP – Pokémon-Themed UDP Multiplayer Battle System

This project implements a multiplayer Pokémon-style battle system using UDP networking in C, designed for the LSNP PokeProtocol implementation. One machine acts as a Host, while another acts as a Joiner, enabling two players to engage in turn-based combat following an RFC-like protocol.

The system includes Pokémon data parsing, battle sequencing, core network logic, a reliability layer with ACKs and retransmission, and extended features such as chat and sticker messaging.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------

# Steps to run the game
How to run the code: <br>
```
python host.py
```
```
python joiner.py
```
--------------------------------------------------------------------------------------------------------------------------------------------------------------------_

# PROJECT STRUCTURE

1. network_handler.c - This is responsible for the Game Logic
2. battle_manager.py - The header files for the BattleManager.c
3. pokemon_data.py - This is responsible for the pokemon loader
4. pokemon.csv - The csv file or data of Pokemons
5. host.py - The UDP host logic main file
6. joiner.py - The UDP joiner logic main file


--------------------------------------------------------------------------------------------------------------------------------------------------------------------

# Project Overview
1. Pokémon Data Loading:
Parse pokemon.csv and construct Pokémon structures used throughout the battle.

2. Battle Flow (BattleManager + peer files):
Handles:
  Turn order
  Attack/defense announcement steps
  Damage calculation
  State syncing
  Win/Loss determination

3. UDP Networking
Two peers communicate using plain-text newline-delimited key:value messages.

Files:
  host.c, joiner.c

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

Team Task Distribution & Project Plan (PokeProtocol – LSNP)
A detailed report of the tasks implemented by each team member is documented below. If uneven participation is suspected, instructors may request individual explanations. Teams are allowed to drop any non-participating members during submission.

| Member                         | Primary Focus Area          | Key Responsibilities                                                 | Rubric Alignment  
| ------------------------------ | --------------------------- | -------------------------------------------------------------------- | ----------------------------
| Marvien Angel C. Castillo      | Network Core & Setup | UDP socket setup, handshake logic, message parsing & serialization | UDP Sockets & Handshake + Reliability & Error Handling   
| Nikki Benedict E. Maningas     | Features, Game Logic | Damage calculation, chat system, sticker handling, verbose mode, game logic     | Game Logic and State Handling Features

--------------------------------------------------------------------------------------------------------------------------------------------------------------------

# AI Usage Section
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
