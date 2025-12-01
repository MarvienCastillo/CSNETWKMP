#ifndef BATTLE_MANAGER_H
#define BATTLE_MANAGER_H

#define BM_MAX_MSG_SIZE 1024
#define STATE_WAITING_FOR_MOVE 0
#define STATE_PROCESSING_TURN 1
#define STATE_WAITING_FOR_RESOLUTION 2
#define STATE_GAME_OVER 3
#include "pokemon_data.h"
#include <stdbool.h> 

typedef struct {
    char name[64];
    char type[32];
    int power;
    char category[16];  // PHYSICAL/SPECIAL/STATUS
} Move;

typedef struct {
    char attacker[64];
    char moveUsed[64];
    int remainingHP;
    int damageDealt;
    int defenderHP;
    char statusMessage[128];
   int damage;
} CalculationReport;

typedef struct {
    Pokemon myPokemon;
    Pokemon oppPokemon;

    int currentState;
    bool isMyTurn;
    int currentSequenceNum;

    char lastMoveUsed[64];
    int lastDamage;
    int lastRemainingHP;
} BattleContext;

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
// Initialize the battle context
void init_battle(BattleContext *ctx, int isHost, const char *myPokeName);


#endif