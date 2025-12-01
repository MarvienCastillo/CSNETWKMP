#include "BattleManager.h"
#include <stdio.h>
#include <string.h>
#include "pokemon_data.h"
static bool pokemon_data_is_loaded = false;

static void handle_attack_announce(BattleManager *bm, const char *msg);
static void handle_defense_announce(BattleManager *bm, const char *msg);
static void handle_calculation_report(BattleManager *bm, const char *msg);
static void handle_calculation_confirm(BattleManager *bm, const char *msg);  
static void handle_game_over(BattleManager *bm, const char *msg);

static int get_msg_type(const char *msg, const char *type_str) {
    return strstr(msg, type_str) != NULL;
}

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

// Hardcoded fallback Move (replace later with CSV logic)
static Move* getMoveData(const char *moveName) {
    static Move fallback = {"Tackle", "PHYSICAL", 40, "Normal"};
    return &fallback;
}

// Simple damage calculation
static int calculate_damage(Pokemon *attacker, Pokemon *defender, Move *move) {
    int dmg = move->power + (attacker->attack - defender->defense) / 2;
    if (dmg < 1) dmg = 1;
    return dmg;
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
static void handle_game_over(BattleManager *bm, const char *msg) {
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
static void handle_attack_announce(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;

    // Extract opponent's move
    extract_value((char*)msg, "move_name", ctx->lastMoveUsed);
    printf("[GAME] Opponent used %s! Prepare your move...\n", ctx->lastMoveUsed);

    // Prepare DEFENSE_ANNOUNCE
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
        "message_type: DEFENSE_ANNOUNCE\n"
        "sequence_number: %d\n",
        ++ctx->currentSequenceNum);

    // Set turn to you
    ctx->isMyTurn = 1;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
}

static void handle_defense_announce(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;

    printf("[GAME] Opponent ready. Calculating damage...\n");

    Move *mv = getMoveData(ctx->lastMoveUsed);
    if (!mv) {
        static Move fallback = {"Tackle", "PHYSICAL", 40, "Normal"};
        mv = &fallback;
    }

    int dmg = calculate_damage(&ctx->myPokemon, &ctx->oppPokemon, mv);
    int remaining_hp = ctx->oppPokemon.hp - dmg;
    if (remaining_hp < 0) remaining_hp = 0;

    ctx->oppPokemon.hp = remaining_hp;

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
        "status_message: %s used %s! It was super effective!\n"
        "sequence_number: %d\n",
        ctx->myPokemon.name,
        ctx->lastMoveUsed,
        ctx->myPokemon.hp,
        dmg,
        remaining_hp,
        ctx->myPokemon.name,
        ctx->lastMoveUsed,
        ++ctx->currentSequenceNum);

    ctx->oppPokemon.hp = remaining_hp;

    // Keep turn state
    ctx->currentState = STATE_PROCESSING_TURN;
}

static void handle_calculation_report(BattleManager *bm, const char *msg) {
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
static void handle_resolution_request(BattleManager *bm, const char *msg) {
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

static void handle_calculation_confirm(BattleManager *bm, const char *msg) {
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
            printf("[GAME INIT] Pokémon data loaded successfully (%d records).\n", loaded_count);
        } else {
            printf("[GAME INIT] CRITICAL ERROR: Failed to load pokemon.csv. Exiting.\n");
            exit(1); // Stop the application if data is critical
        }
    }
    memset(bm, 0, sizeof(BattleManager));
    init_battle(&bm->ctx, isHost, (char*)myPokeName);
}

void BattleManager_HandleIncoming(BattleManager *bm, const char *msg) {
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);

    if (get_msg_type(msg, "ATTACK_ANNOUNCE")) handle_attack_announce(bm, msg);
    else if (get_msg_type(msg, "DEFENSE_ANNOUNCE")) handle_defense_announce(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_REPORT")) handle_calculation_report(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_CONFIRM")) handle_calculation_confirm(bm, msg);
    else if (get_msg_type(msg, "RESOLUTION_REQUEST")) handle_resolution_request(bm, msg);
    else if (get_msg_type(msg, "GAME_OVER")) handle_game_over(bm, msg);
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
