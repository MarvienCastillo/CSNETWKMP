#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pokemon_data.h"

#define MAX_MSG_SIZE 1024
#define TIMEOUT_MS 500

// Game State Machine
typedef enum {
    STATE_SETUP,             
    STATE_WAITING_FOR_MOVE,  
    STATE_PROCESSING_TURN,   
    STATE_GAME_OVER          
} GameState;

typedef struct {
    int isHost;              
    int isMyTurn;            
    GameState currentState;
    
    Pokemon myPokemon;       
    Pokemon oppPokemon;


    char lastMoveUsed[64];

    int currentSequenceNum; 
} BattleContext;

typedef struct {
    char name[64];
    char damage_category[16];
    int base_power;
    char type[32];
} Move;

void init_battle(BattleContext *ctx, int isHost, char *myPokeName);
void process_incoming_packet(BattleContext *ctx, char *msg, char *response_buffer);
void process_user_input(BattleContext *ctx, char *input, char *output_buffer);

int calculate_damage (Pokemon *attacker, Pokemon *defender, Move *move);
float get_type_multiplier(const char *moveType, const char *defType);

#endif