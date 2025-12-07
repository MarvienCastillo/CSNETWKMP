#include "BattleManager.h"
#include "pokemon_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Utility function (placeholder, usually defined elsewhere)
void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

// Placeholder for message type checking (must be implemented)
bool get_msg_type(const char *msg, const char *type) {
    char search_str[128];
    snprintf(search_str, sizeof(search_str), "message_type: %s", type);
    return strstr(msg, search_str) != NULL;
}

// Placeholder function to find the move in the Pokémon's moveset
Move* find_move_in_moveset(Pokemon *poke, const char *move_name) {
    for (int i = 0; i < poke->num_moves && i < 4; i++) {
        if (strcasecmp(poke->moveset[i].name, move_name) == 0) {
            return &poke->moveset[i];
        }
    }
    return NULL;
}

/* ------------------------------------------
   Core Logic Functions
------------------------------------------- */

void init_battle(BattleContext *ctx, int isHost, const char *myPokeName) {
    memset(ctx, 0, sizeof(BattleContext));

    // Look up host's chosen Pokémon data
    Pokemon *my_data = getPokemonByName(myPokeName); 
    if (my_data) {
        // Copy base stats and initialize currentHP
        ctx->myPokemon = *my_data;
        ctx->myPokemon.currentHP = my_data->hp;
        // NOTE: Full moveset data (power, type, category) must be looked up 
        // and populated here or earlier by the network layer.
    } else {
        printf("[ERROR] My Pokemon not found: %s\n", myPokeName);
    }
    
    // Host goes first
    ctx->isMyTurn = isHost;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
    ctx->currentSequenceNum = 0;
}

void BattleManager_Init(BattleManager *bm, int isHost, const char *myPokeName) {
    loadPokemonCSV("pokemon.csv");
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);
    init_battle(&bm->ctx, isHost, myPokeName);
    printf("[BATTLE MANAGER] Initialized. My Pokemon: %s\n", bm->ctx.myPokemon.name);
}


const char* BattleManager_GetOutgoingMessage(BattleManager *bm) {
    return bm->outgoingBuffer;
}

void BattleManager_ClearOutgoingMessage(BattleManager *bm) {
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);
}

// Placeholder handler functions (must be implemented to update state and buffer)
void handle_attack_announce(BattleManager *bm, const char *msg) {
    // 1. Check if we are ready (STATE_WAITING_FOR_MOVE)
    if (bm->ctx.currentState != STATE_WAITING_FOR_MOVE) return;

    // 2. Parse move name from msg
    // Example: (needs actual parsing code)
    char opp_move[64] = "PlaceholderMove"; 
    
    // 3. Calculate damage based on Move and Pokemon stats
    int damage = 10; // Placeholder calculation
    
    bm->ctx.oppPokemon.currentHP -= damage; 
    
    // 4. Update internal tracking variables
    bm->ctx.lastDamage = damage;
    strncpy(bm->ctx.lastMoveUsed, opp_move, sizeof(bm->ctx.lastMoveUsed));

    // 5. Change state and prepare response message (DEFENSE_ANNOUNCE)
    bm->ctx.currentState = STATE_PROCESSING_TURN;
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
             "message_type: DEFENSE_ANNOUNCE\n"
             "sequence_number: %d\n", ++bm->ctx.currentSequenceNum);
}

void handle_defense_announce(BattleManager *bm, const char *msg) {
    // This confirms the opponent received the ATTACK_ANNOUNCE.
    // Host/Joiner should typically reply with CALCULATION_REPORT next.
    // Logic for CALCULATION_REPORT generation goes here.
    bm->ctx.currentState = STATE_PROCESSING_TURN;
    
    // Example: Automatically generate CALCULATION_REPORT
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
             "message_type: CALCULATION_REPORT\n"
             "attacker: %s\n"
             // ... include damage calculation results ...
             "sequence_number: %d\n", bm->ctx.myPokemon.name, ++bm->ctx.currentSequenceNum);
}

void handle_calculation_report(BattleManager *bm, const char *msg) {
    // 1. Parse opponent's damage report
    int opp_damage = 0; // Placeholder parsing
    int my_hp_remaining = 0; // Placeholder parsing

    // 2. Update my local HP (myPokemon.currentHP) based on the report
    bm->ctx.myPokemon.currentHP = my_hp_remaining;
    
    // 3. Check win/loss condition (BattleManager_CheckWinLoss)
    // 4. Reply with CALCULATION_CONFIRM
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
             "message_type: CALCULATION_CONFIRM\n"
             "sequence_number: %d\n", ++bm->ctx.currentSequenceNum);
}

void handle_calculation_confirm(BattleManager *bm, const char *msg) {
    // Opponent confirmed damage. Now we switch turns and reset state.
    bm->ctx.isMyTurn = !bm->ctx.isMyTurn;
    bm->ctx.currentState = STATE_WAITING_FOR_MOVE;
    printf("[BATTLE] Turn ended. Now waiting for next move.\n");
}

void handle_resolution_request(BattleManager *bm, const char *msg) {
    // Used to handle complex effects or status changes.
    // For now, this can be treated similarly to CALCULATION_CONFIRM.
    bm->ctx.currentState = STATE_WAITING_FOR_MOVE;
    bm->ctx.isMyTurn = !bm->ctx.isMyTurn;
}

void handle_game_over(BattleManager *bm, const char *msg) {
    bm->ctx.currentState = STATE_GAME_OVER;
    // Log game over message
    printf("[GAME OVER] Received game over message.\n");
}


void BattleManager_HandleIncoming(BattleManager *bm, const char *msg) {
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);

    if (get_msg_type(msg, "ATTACK_ANNOUNCE")) handle_attack_announce(bm, msg);
    else if (get_msg_type(msg, "DEFENSE_ANNOUNCE")) handle_defense_announce(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_REPORT")) handle_calculation_report(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_CONFIRM")) handle_calculation_confirm(bm, msg);
    else if (get_msg_type(msg, "RESOLUTION_REQUEST")) handle_resolution_request(bm, msg);
    else if (get_msg_type(msg, "GAME_OVER")) handle_game_over(bm, msg);
    // If receiving BATTLE_SETUP, populate oppPokemon data here
}


void BattleManager_HandleUserInput(BattleManager *bm, const char *input) {
    if (bm->ctx.currentState != STATE_WAITING_FOR_MOVE || !bm->ctx.isMyTurn) {
        printf("[BATTLE MANAGER] Not your turn or waiting for resolution.\n");
        return;
    }
    
    // 1. Look up the move by name
    Move *move = find_move_in_moveset(&bm->ctx.myPokemon, input);

    if (!move) {
        printf("[BATTLE MANAGER] Invalid move: %s\n", input);
        return;
    }

    // 2. Update battle context
    strncpy(bm->ctx.lastMoveUsed, move->name, sizeof(bm->ctx.lastMoveUsed));
    bm->ctx.isMyTurn = false;
    bm->ctx.currentState = STATE_PROCESSING_TURN;

    // 3. Prepare outgoing ATTACK_ANNOUNCE message
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
             "message_type: ATTACK_ANNOUNCE\n"
             "attacker: %s\n"
             "move_used: %s\n"
             "sequence_number: %d\n",
             bm->ctx.myPokemon.name,
             move->name,
             ++bm->ctx.currentSequenceNum);
             
    printf("[BATTLE MANAGER] Sending ATTACK_ANNOUNCE: %s\n", move->name);
}

int BattleManager_CheckWinLoss(BattleManager *bm) {
    if (bm->ctx.myPokemon.currentHP <= 0) {
        return -1; // Loss
    }
    if (bm->ctx.oppPokemon.currentHP <= 0) {
        return 1; // Win
    }
    return 0; // Ongoing
}

void BattleManager_TriggerGameOver(BattleManager *bm, const char *winner, const char *loser) {
    bm->ctx.currentState = STATE_GAME_OVER;
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
             "message_type: GAME_OVER\n"
             "winner: %s\n"
             "loser: %s\n",
             winner, loser);
}