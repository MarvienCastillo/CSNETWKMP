#include "game_logic.h"

int get_msg_type(char *msg, char *type_str) {
    return (strstr(msg, type_str) != NULL);
}

void extract_value(char *msg, char *key, char *dest) {
    char search[50];
    sprintf(search, "%s: ", key);
    char *start = strstr(msg, search);
    if (start) {
        start += strlen(search);
        int i = 0;
        while (start[i] != '\n' && start[i] != '\0' && start[i] != '\r') {
            dest[i] = start[i];
            i++;
        }
        dest[i] = '\0';
    }
}

void init_battle(BattleContext *ctx, int isHost, char *myPokeName) {
    ctx->isHost = isHost;
    ctx->currentState = STATE_WAITING_FOR_MOVE;
    ctx->currentSequenceNum = 1;
    strcpy(ctx->lastMoveUsed, "Tackle"); 
    
    strcpy(ctx->myPokemon.name, myPokeName);
    ctx->myPokemon.hp = 100; // Default Health
    
    strcpy(ctx->oppPokemon.name, "Opponent"); 
    ctx->oppPokemon.hp = 100;

    if (isHost) {
        ctx->isMyTurn = 1;
        printf("[GAME] You are HOST. You go first!\n");
    } else {
        ctx->isMyTurn = 0;
        printf("[GAME] You are JOINER. Waiting for Host...\n");
    }
}

void process_incoming_packet(BattleContext *ctx, char *msg, char *response_buffer) {
    
    memset(response_buffer, 0, MAX_MSG_SIZE);

    //ATTACK_ANNOUNCE HANDLING 
    if (get_msg_type(msg, "ATTACK_ANNOUNCE")) {
        if (ctx->currentState == STATE_WAITING_FOR_MOVE && !ctx->isMyTurn) {
            
            extract_value(msg, "move_name", ctx->lastMoveUsed);
            printf("[GAME] Opponent used %s! Preparing defense...\n", ctx->lastMoveUsed);

            sprintf(response_buffer, 
                "message_type: DEFENSE_ANNOUNCE\n"
                "sequence_number: %d\n", 
                ++ctx->currentSequenceNum);

            ctx->currentState = STATE_PROCESSING_TURN;
        }
    }

    // DEFENSE_ANNOUNCE HANDLING
    else if (get_msg_type(msg, "DEFENSE_ANNOUNCE")) {
        if (ctx->currentState == STATE_WAITING_FOR_MOVE && ctx->isMyTurn) {
            printf("[GAME] Opponent is ready. Calculating damage...\n");
            
            ctx->currentState = STATE_PROCESSING_TURN;
            
            int dmg = 20; 
            int remaining_hp = ctx->oppPokemon.hp - dmg;
            if(remaining_hp < 0) remaining_hp = 0;

            sprintf(response_buffer, 
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
                ctx->myPokemon.name, ctx->lastMoveUsed, 
                ++ctx->currentSequenceNum);
                
            ctx->oppPokemon.hp = remaining_hp;
        }
    }

    // CALCULATION_REPORT HANDLING 
    else if (get_msg_type(msg, "CALCULATION_REPORT")) {
        if (ctx->currentState == STATE_PROCESSING_TURN) {
            printf("[GAME] Received damage report. Verifying...\n");
            
            sprintf(response_buffer, 
                "message_type: CALCULATION_CONFIRM\n"
                "sequence_number: %d\n", 
                ++ctx->currentSequenceNum);
            
            // Flip Turn
            if (ctx->isMyTurn) {
                ctx->isMyTurn = 0;
                printf("[GAME] Turn done. Waiting for opponent...\n");
            } else {
                ctx->isMyTurn = 1;
                printf("[GAME] Turn done. YOUR TURN!\n");
            }
            
            ctx->currentState = STATE_WAITING_FOR_MOVE;
        }
    }

    else if (get_msg_type(msg, "RESOLUTION_REQUEST")) {
        printf("[GAME] Discrepancy detected! (Resolution Logic)\n");
    }
}

// ATTACK_ANNOUNCE Construction
void process_user_input(BattleContext *ctx, char *input, char *output_buffer) {
    memset(output_buffer, 0, MAX_MSG_SIZE);

    if (ctx->currentState == STATE_WAITING_FOR_MOVE && ctx->isMyTurn) {
        strcpy(ctx->lastMoveUsed, input);

        sprintf(output_buffer,
            "message_type: ATTACK_ANNOUNCE\n"
            "move_name: %s\n"
            "sequence_number: %d\n", 
            input, ++ctx->currentSequenceNum);
            
        printf("[GAME] Sending Attack: %s\n", input);
    } else {
        printf("[GAME] Not your turn or wrong state!\n");
    }
}