#include "BattleManager.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "pokemon_data.h"
static bool pokemon_data_is_loaded = false;


// --- Utility functions ---

// Remove newline from a string
void clean_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

// Extract value from a key in a message
static void extract_value(char *msg, const char *key, char *output) {
    char *start = strstr(msg, key);
    if (start) {
        start += strlen(key);
        sscanf(start, "%s", output);
    } else {
        output[0] = '\0';
    }
}

// Extract value from a key in a message
static void extract(const char *msg, const char *key, char *output, size_t output_size) {
    char *start = strstr(msg, key);
    
    if (start) {
        // 1. Advance the pointer past the key string
        start += strlen(key);
        
        // 2. Trim any leading whitespace (spaces or tabs) after the key
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        
        // 3. Find the length of the content (the value) until the first newline or null terminator.
        // strcspn finds the length of the initial segment of 'start' which consists 
        // entirely of characters NOT in the string "\n\0" (the newline or end of string).
        size_t value_len = strcspn(start, "\n\0");

        // 4. Safely copy the extracted value into the output buffer
        // We use the lesser of value_len or (output_size - 1) for safety.
        if (value_len > 0) {
            size_t copy_len = (value_len < output_size) ? value_len : (output_size - 1);
            strncpy(output, start, copy_len);
            output[copy_len] = '\0'; // Ensure null termination
        } else {
            // Value was empty or only whitespace before newline
            output[0] = '\0';
        }
    } else {
        // Key not found
        output[0] = '\0';
    }
}


static void display_game_over(const char *winner, const char *loser, int seq) {
    printf("\n===============================\n");
    printf("          GAME OVER           \n");
    printf("===============================\n");
    printf("Winner: %s\n", winner);
    printf("Loser : %s\n", loser);
    printf("Sequence Number: %d\n", seq);
    printf("===============================\n\n");
}
void handle_game_over(BattleManager *bm, const char *msg) {
    char winner[64], loser[64], seq_buf[16];
    extract_value((char *)msg, "winner", winner);
    extract_value((char *)msg, "loser", loser);
    extract_value((char *)msg, "sequence_number", seq_buf);

    int seq = atoi(seq_buf);
    display_game_over(winner, loser, seq);

    bm->ctx.currentState = STATE_GAME_OVER;
}

// Sending a GAME_OVER message
void BattleManager_TriggerGameOver(BattleManager *bm, const char *winner, const char *loser) {
    char msg[BM_MAX_MSG_SIZE];

    snprintf(msg, BM_MAX_MSG_SIZE,
        "message_type: \"GAME_OVER\"\n"
        "winner: %s\n"
        "loser: %s\n"
        "sequence_number: %d",
        winner,
        loser,
        ++bm->ctx.currentSequenceNum
    );

    handle_game_over(bm, msg);
}

// --- Handlers ---
void handle_attack_announce(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;
    // Extract opponent's move
    extract(msg, "move_name: ", ctx->lastMoveUsed, sizeof(ctx->lastMoveUsed));
    Pokemon p;
    if(ctx->isMyTurn == 1){
        p = ctx->myPokemon;
    }
    else{
        p = ctx->oppPokemon;
    }
    if(getMoveByName(ctx->lastMoveUsed,p.name))
        printf("[GAME] Opponent used %s! Prepare your move...\n", ctx->lastMoveUsed);
    else{
        printf("Invalid move");
    }

    // Prepare DEFENSE_ANNOUNCE
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
        "message_type: DEFENSE_ANNOUNCE\n"
        "sequence_number: %d\n",
        ++ctx->currentSequenceNum);

    // Set turn to you
    ctx->isMyTurn = 1;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
}

void handle_calculation_report(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;

    printf("[GAME] Received damage report. Verifying...\n");

    // Extract values from peer's report 
    char peerMove[64];
    int peerDamage, peerRemainingHP;
    char buffer[16];
    char attacker[64];

    extract_value((char *)msg, "move_used", peerMove);
    extract_value((char *)msg, "damage_dealt", buffer); peerDamage = atoi(buffer);
    extract_value((char *)msg, "defender_hp_remaining", buffer); peerRemainingHP = atoi(buffer);
    extract_value((char *)msg, "attacker", attacker);

    // Check for discrepancy
    bool match = (strcmp(peerMove, ctx->lastMoveUsed) == 0) &&
                (peerDamage == ctx->lastDamage) &&
                (peerRemainingHP == ctx->lastRemainingHP);

    if (match) {
        snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
            "message_type: CALCULATION_CONFIRM\n"
            "sequence_number: %d\n",
            ++ctx->currentSequenceNum);

        // Flip turn
        ctx->isMyTurn = 1;
        ctx->currentState = STATE_WAITING_FOR_MOVE;
        printf("[GAME] Turn done. %s\n", ctx->isMyTurn ? "[YOUR TURN]" : "Waiting for opponent...");

    } else {
        //  send RESOLUTION_REQUEST on mismatch
        printf("[GAME] Discrepancy detected! Sending RESOLUTION_REQUEST...\n");

        snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
            "message_type: RESOLUTION_REQUEST\n"
            "attacker: %s\n"
            "move_used: %s\n"
            "damage_dealt: %d\n"
            "defender_hp_remaining: %d\n"
            "sequence_number: %d\n",
            ctx->myPokemon.name,
            ctx->lastMoveUsed,
            ctx->lastDamage,
            ctx->lastRemainingHP,
            ++ctx->currentSequenceNum);

        ctx->currentState = STATE_WAITING_FOR_RESOLUTION;
    }
}

void handle_resolution_request(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;

    char reqMove[64];
    int reqDamage, reqRemainingHP;
    char attacker[64];
    char buffer[16]; // temp buffer for numeric parsing

    extract_value((char*)msg, "move_used", reqMove);
    extract_value((char*)msg, "damage_dealt", buffer); reqDamage = atoi(buffer);
    extract_value((char*)msg, "defender_hp_remaining", buffer); reqRemainingHP = atoi(buffer);
    extract_value((char*)msg, "attacker", attacker);

    printf("[GAME] RESOLUTION_REQUEST received from opponent.\n");

    bool agree = (strcmp(reqMove, ctx->lastMoveUsed) == 0) &&
                (reqDamage == ctx->lastDamage) &&
                (reqRemainingHP == ctx->lastRemainingHP);

    if (agree) {
        // Send ACK
        snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
            "message_type: ACK\n"
            "sequence_number: %d\n",
            ++ctx->currentSequenceNum);

        ctx->currentState = STATE_WAITING_FOR_MOVE;
        ctx->isMyTurn = !ctx->isMyTurn;
        printf("[GAME] RESOLUTION_REQUEST matched. State updated.\n");
    } else {
        printf("[ERROR] Discrepancy could not be resolved. Terminating battle.\n");
        ctx->currentState = STATE_GAME_OVER;
    }
}

void handle_calculation_confirm(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;
    printf("[GAME] CALCULATION_CONFIRM received.\n");

    // Flip turn
    ctx->isMyTurn = 1;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
}


// --- Public API ---
void BattleManager_Init(BattleManager *bm, int isHost, const char *myPokeName) {
    if (!pokemon_data_is_loaded) {
        int loaded_count = loadPokemonCSV("pokemon.csv");
        
        if (loaded_count > 0) {
            pokemon_data_is_loaded = true;
            printf("[GAME INIT] Pokemon data loaded successfully (%d records).\n", loaded_count);
        } else {
            printf("[GAME INIT] CRITICAL ERROR: Failed to load pokemon.csv. Exiting.\n");
            exit(1); // Stop the application if data is critical
        }
    }
    memset(bm, 0, sizeof(BattleManager));
    init_battle(&bm->ctx, isHost, (char*)myPokeName);
}


void BattleManager_HandleUserInput(BattleManager *bm, const char *input) {
    BattleContext *ctx = &bm->ctx;
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);

    // Special case: user types "GAME_OVER"
    if (strcmp(input, "GAME_OVER") == 0) {
        BattleManager_TriggerGameOver(bm, ctx->myPokemon.name, ctx->oppPokemon.name);
        return;
    }

    // Normal move handling
    if (ctx->currentState == STATE_WAITING_FOR_MOVE && ctx->isMyTurn) {
        strcpy(ctx->lastMoveUsed, input);
        snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
            "message_type: ATTACK_ANNOUNCE\n"
            "move_name: %s\n"
            "sequence_number: %d\n",
            input,
            ++ctx->currentSequenceNum);
        printf("[GAME] Sending attack: %s\n", input);
    } else {
        printf("[GAME] Not your turn or wrong state!\n");
    }
}

int BattleManager_CheckWinLoss(BattleManager *bm) {
    if (bm->ctx.myPokemon.hp <= 0) return -1; // lost
    if (bm->ctx.oppPokemon.hp <= 0) return 1; // won
    return 0; // ongoing
}


const char* BattleManager_GetOutgoingMessage(BattleManager *bm) {
    return bm->outgoingBuffer;
}

void BattleManager_ClearOutgoingMessage(BattleManager *bm) {
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);
}   
void init_battle(BattleContext *ctx, int isHost, const char *myPokeName) {
    // Clear the BattleContext
    memset(ctx, 0, sizeof(BattleContext));

    ctx->currentState = STATE_WAITING_FOR_MOVE;
    ctx->isMyTurn = isHost;  // Host goes first
    // Initialize myPokemon
    Pokemon *p = getPokemonByName(myPokeName);
    if (p) {
        ctx->myPokemon = *p;
        printf("[GAME] Found Pokemon!\n");
    } else {
        printf("[ERROR] Pokemon not found: %s\n", myPokeName);
    }

    ctx->currentSequenceNum = 0;
}

static float get_type_multiplier(const char *moveType, const char *defType1, const char *defType2) {
    float mult = 1.0f;

    // EXAMPLE type chart (add more as needed)
    if (strcmp(moveType, "Fire") == 0) {
        if (strcmp(defType1, "Grass") == 0 || strcmp(defType2, "Grass") == 0) mult *= 2.0f;
        if (strcmp(defType1, "Water") == 0 || strcmp(defType2, "Water") == 0) mult *= 0.5f;
    }

    if (strcmp(moveType, "Water") == 0) {
        if (strcmp(defType1, "Fire") == 0 || strcmp(defType2, "Fire") == 0) mult *= 2.0f;
        if (strcmp(defType1, "Grass") == 0 || strcmp(defType2, "Grass") == 0) mult *= 0.5f;
    }

    if (strcmp(moveType, "Electric") == 0) {
        if (strcmp(defType1, "Water") == 0 || strcmp(defType2, "Water") == 0) mult *= 2.0f;
        if (strcmp(defType1, "Ground") == 0 || strcmp(defType2, "Ground") == 0) mult = 0.0f; // immune
    }

    return mult;
}



int calculate_damage(Pokemon *attacker, Pokemon *defender, Move *move) {
    if (move->power <= 0)
        return 0; // status move / invalid

    // PHYSICAL vs SPECIAL
    int atk = (strcmp(move->category, "Physical") == 0)
                ? attacker->attack
                : attacker->sp_attack;

    int def = (strcmp(move->category, "Physical") == 0)
                ? defender->defense
                : defender->sp_defense;

    // BASE DAMAGE (Pokémon-style simplified)
    float base = (((2.0f * 50 / 5) + 2) * move->power * (float)atk / (float)def) / 50.0f + 2;

    // STAB: Same-Type Attack Bonus
    float stab = (
        strcmp(attacker->type1, move->type) == 0 ||
        strcmp(attacker->type2, move->type) == 0
    ) ? 1.5f : 1.0f;

    // TYPE EFFECTIVENESS
    float typeMult = get_type_multiplier(move->type, defender->type1, defender->type2);

    // RANDOM VARIATION
    float randMult = (rand() % 16 + 85) / 100.0f; // 0.85 to 1.00

    // FINAL DAMAGE
    float dmg = base * stab * typeMult * randMult;

    if (dmg < 1) dmg = 1; // minimum damage rule

    return (int)dmg;
}

void handle_defense_announce(BattleManager *bm, const char *msg, char name[64]) {
    BattleContext *ctx = &bm->ctx;

    printf("[GAME] Opponent ready. Calculating damage...\n");

    Move *mv = getMoveByName(ctx->lastMoveUsed,name);

    int dmg = calculate_damage(&ctx->myPokemon, &ctx->oppPokemon, mv);
    
    ctx->oppPokemon.hp -= dmg;
    if (ctx->oppPokemon.hp < 0)
        ctx->oppPokemon.hp = 0;

    ctx->lastDamage = dmg;
    ctx->lastRemainingHP = ctx->oppPokemon.hp;

    if (ctx->oppPokemon.hp <= 0) {
    
        snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
            "message_type: GAME_OVER\n"
            "winner: %s\n"
            "loser: %s\n"
            "sequence_number: %d\n",
            ctx->myPokemon.name,
            ctx->oppPokemon.name,
            ++ctx->currentSequenceNum
        );

        ctx->currentState = STATE_GAME_OVER;
        printf("[GAME] Opponent fainted! GAME_OVER triggered.\n");
        return;
    }

    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
        "message_type: CALCULATION_REPORT\n"
        "attacker: %s\n"
        "move_used: %s\n"
        "remaining_health: %d\n"
        "damage_dealt: %d\n"
        "defender_hp_remaining: %d\n"
        "status_message: %s dealt %d damage with %s\n"
        "sequence_number: %d\n",
        ctx->myPokemon.name,
        ctx->lastMoveUsed,
        ctx->myPokemon.hp,
        dmg,
        ctx->oppPokemon.hp,
        ctx->myPokemon.name,
        dmg,
        ctx->lastMoveUsed,
        ++ctx->currentSequenceNum
    );

    // ctx->oppPokemon.hp = remaining_hp;

    // Keep turn state
    ctx->currentState = STATE_PROCESSING_TURN;
}