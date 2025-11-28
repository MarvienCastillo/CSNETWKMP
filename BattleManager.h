#ifndef BATTLE_MANAGER_H
#define BATTLE_MANAGER_H

#include "game_logic.h"

#define BM_MAX_MSG_SIZE 1024

typedef struct {
    BattleContext ctx;
    char outgoingBuffer[BM_MAX_MSG_SIZE]; // buffer for messages to send
} BattleManager;

// Initialize BattleManager (Host = 1, Joiner = 0)
void BattleManager_Init(BattleManager *bm, int isHost, const char *myPokeName);

// Handle incoming PokeProtocol messages
void BattleManager_HandleIncoming(BattleManager *bm, const char *msg);

// Handle user input (move names)
void BattleManager_HandleUserInput(BattleManager *bm, const char *input);

// Check if battle is over
int BattleManager_CheckWinLoss(BattleManager *bm);

// Get the next outgoing message (filled by handlers)
const char* BattleManager_GetOutgoingMessage(BattleManager *bm);

// Clear outgoing buffer after sending
void BattleManager_ClearOutgoingMessage(BattleManager *bm);

void BattleManager_TriggerGameOver(BattleManager *bm, const char *winner, const char *loser);

#endif
