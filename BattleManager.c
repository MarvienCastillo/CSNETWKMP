#include "BattleManager.h"
#include <stdio.h>
#include <string.h>
#include "pokemon_data.h"

static int get_msg_type(const char *msg, const char *type_str) {
    return strstr(msg, type_str) != NULL;
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

// --- Sending a GAME_OVER message ---
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

    // Prepare DEFENSE_ANNOUNCE following strict format
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

    // Strict CALCULATION_CONFIRM format
    snprintf(bm->outgoingBuffer, BM_MAX_MSG_SIZE,
        "message_type: CALCULATION_CONFIRM\n"
        "sequence_number: %d\n",
        ++ctx->currentSequenceNum);

    // Flip turn
    ctx->isMyTurn = 1;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
    printf("[GAME] Turn done. %s\n", ctx->isMyTurn ? "[YOUR TURN]" : "Waiting for opponent...");
}

static void handle_calculation_confirm(BattleManager *bm, const char *msg) {
    BattleContext *ctx = &bm->ctx;

    printf("[GAME] CALCULATION_CONFIRM received.\n");

    ctx->isMyTurn = 1;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
}

// --- Public API ---
void BattleManager_Init(BattleManager *bm, int isHost, const char *myPokeName) {
    memset(bm, 0, sizeof(BattleManager));
    init_battle(&bm->ctx, isHost, (char*)myPokeName);
}

void BattleManager_HandleIncoming(BattleManager *bm, const char *msg) {
    memset(bm->outgoingBuffer, 0, BM_MAX_MSG_SIZE);

    if (get_msg_type(msg, "ATTACK_ANNOUNCE")) handle_attack_announce(bm, msg);
    else if (get_msg_type(msg, "DEFENSE_ANNOUNCE")) handle_defense_announce(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_REPORT")) handle_calculation_report(bm, msg);
    else if (get_msg_type(msg, "CALCULATION_CONFIRM")) handle_calculation_confirm(bm, msg);
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
