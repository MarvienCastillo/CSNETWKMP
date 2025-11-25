#include "game_logic.h"
#include <string.h>
#include <ctype.h>

int get_msg_type(char *msg, char *type_str) {
    return (strstr(msg, type_str) != NULL);
}

static Move moveDatabase[] = {
    { "Tackle", "PHYSICAL", 40.0f, "Normal" },
    { "Thunderbolt", "SPECIAL",  90.0f, "Electric" },
    { "Flamethrower", "SPECIAL", 90.0f,  "Fire" },
    { "Quick Attack", "PHYSICAL", 40.0f, "Normal" },
    { "Water Gun", "SPECIAL",  40.0f,  "Water" },
    { "Vine Whip", "PHYSICAL", 45.0f, "Grass" }
};

static int moveCount = sizeof(moveDatabase) / sizeof(Move);

Move* getMoveData(const char* moveName) {
    for (int i = 0; i < moveCount; i++) {
        if (strcasecmp(moveDatabase[i].name, moveName) == 0)
            return &moveDatabase[i];
    }
    return NULL;
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
            
            /*int dmg = 20; 
            int remaining_hp = ctx->oppPokemon.hp - dmg;
            if(remaining_hp < 0) remaining_hp = 0;*/

            Move *mv = getMoveData(ctx -> lastMoveUsed);

            if (!mv) {
               printf("[ERROR] Move '%s' not found! Using default power.\n", ctx->lastMoveUsed);
                static Move fallback = {"Tackle", "PHYSICAL", 40, "normal"};
                mv = &fallback;
            }

            int dmg = calculate_damage(&ctx->myPokemon, &ctx->oppPokemon, mv);
            int remaining_hp = ctx->oppPokemon.hp - dmg;
            if (remaining_hp < 0) 
                remaining_hp = 0;

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

float get_type_multiplier(const char *moveType, const char *defType) {
    if (defType == NULL || strlen(defType) == 0)
        return 1.0f;
    
    int i;
    char m[32], d[32]; //convert lowercase for consistent comparison
    for (i = 0; moveType[i]; i++)
        m[i] = tolower(moveType[i]);
    
    m[strlen(moveType)] = '\0';

    for (i = 0; defType[i]; i++)
        d[i] = tolower(defType[i]);

    d[strlen(defType)] = '\0';

    if (strcmp(m, "fire") == 0 && 
        strcmp(d, "grass") == 0)
            return 2.0f;
    else if (strcmp(m, "fire") == 0 &&
             strcmp(d, "water") == 0)
            return 0.5f;
    else if (strcmp(m, "electric") == 0 &&
             strcmp(d, "water") == 0)
            return 2.0f;
    else if (strcmp(m, "electric") == 0 &&
             strcmp(d, "ground") == 0)
            return 0.0f;

    return 1.0f;
}

int calculate_damage(Pokemon *attacker, Pokemon *defender, Move *move) {
    int A = 0; //attack
    int D = 0; //defense

    if (strcmp(move->damage_category, "PHYSICAL") == 0) {
        A = attacker->attack;
        D = defender->defense;
    }
    else {
        A = attacker->sp_attack;
        D = defender->sp_defense;
    }

    if (A <= 0) 
        A = 1;
    if (D <= 0) 
        D = 1;

    float base = ((2 * 50 / 5 + 2) * move->base_power * (float)A / D) / 50 + 2;

    //type effectiveness applied
    float m1 = get_type_multiplier(move->type, defender->type1);
    float m2 = get_type_multiplier(move->type, defender->type2);

    float finalDamage = base * m1 * m2;

    if (finalDamage < 1.0f)
        finalDamage = 1.0f;

    return (int)finalDamage;
}