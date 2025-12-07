#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> 

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

// --- MOVE STRUCTURE (Remains required by BattleManager.h) ---
typedef struct {
    char name[64];
} Move;

// --- POKEMON STRUCTURE ---
typedef struct {
    char name[64];
    char type1[32];
    char type2[32];
    int hp;
    int attack;
    int defense;
    int sp_attack;
    int sp_defense;
    int currentHP; 
    
    // Moveset array now holds the combined 'Move/Abilities'
    Move moveset[10]; 
    int num_moves;
} Pokemon;

// --- GLOBAL DATA STORAGE ---
static Pokemon pokedex[1200];
static int pokemon_count = 0;
static Move move_list[512]; // Global list of all Moves/Abilities
static int total_moves = 0;

/* ------------------------------------------
    Utility and Lookups (trim_trailing_whitespace, getPokemonByName, loadMovesCSV)
    ... (These remain the same) ...
------------------------------------------- */
static void trim_trailing_whitespace(char *str) {
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

static Pokemon* getPokemonByName(const char* name) {
    for (int i = 0; i < pokemon_count; i++) {
        if (strcasecmp(pokedex[i].name, name) == 0)
            return &pokedex[i];
    }
    return NULL;
}

// Assumed function: Look up the full Move struct (Type, Power, Category) by name
static Move* getMoveByName(const char *name,const char* pokemon_name) {
    Pokemon *p = getPokemonByName(pokemon_name);
    for (int i = 0; i < p->num_moves; i++) {
        if (strcasecmp(p->moveset[i].name, name) == 0) {
            return &move_list[i];
        }
    }
    return NULL;
}
// Function to check if a character exists in the set of characters to be removed
static int char_in_set(char c, char *remove_set) {
    // strchr returns a pointer to the first occurrence of c in remove_set, or NULL if not found.
    return (strchr(remove_set, c) != NULL);
}

// Function that removes specified characters from a string in place
static void remove_chars(char *str,char *remove_set) {
    if (str == NULL || remove_set == NULL) {
        return;
    }

    char *read_ptr = str; // Pointer to read from the original string
    char *write_ptr = str; // Pointer to write to the new (modified) string

    // 1. Iterate through the string using the read_ptr
    while (*read_ptr != '\0') {
        
        // 2. Check if the current character should be kept
        if (!char_in_set(*read_ptr, remove_set)) {
            
            // 3. If the character is a keeper, copy it to the write_ptr location
            *write_ptr = *read_ptr;
            write_ptr++; // Advance the write pointer
        }
        
        read_ptr++; // Always advance the read pointer
    }

    // 4. Null-terminate the new, shorter string
    *write_ptr = '\0';
}
static int count_quoted_strings(char *str) {
    if (!str) return 0;

    int count = 0;
    int in_quote = 0;

    while (*str) {
        if (*str == '\'') { // found a quote
            if (!in_quote) {
                // starting a new quoted string
                count++;
                in_quote = 1;
            } else {
                // ending the quoted string
                in_quote = 0;
            }
        }
        str++;
    }

    return count;
}
/* ------------------------------------------
    parse_combined_moveset (REVISED)
    Parses ['Move1', 'Ability2', ...] and stores names in Pokemon.moveset
------------------------------------------- */
static void parse_combined_moveset(char *token, Pokemon *p) {
    if (token == NULL || strlen(token) < 3) return;
    int num = count_quoted_strings(token);
    char *end = token + strlen(token) - 1;
    if (*end == ']') *end = '\0'; // special character for strtok
    char *chars_to_remove = "[]'\"";
    remove_chars(token,chars_to_remove);
    
    const char delimeters[] = ":\t";
    // 2. Tokenize by comma
    char *move_token = strtok(token, delimeters);
    int i = 0;

    while (move_token != NULL && i < num) { 
        char *item_name = move_token;
        
        if (strlen(item_name) > 0) {
            // Store the cleaned NAME
            strncpy(p->moveset[i].name, item_name, sizeof(p->moveset[0].name) - 1);
            p->moveset[i].name[sizeof(p->moveset[0].name) - 1] = '\0';
            printf("Move's name: %s\tPokemon's Name: %s\n", p->moveset[i].name,p->name);
            i++;
        }

        move_token = strtok(NULL, delimeters);
    }
    p->num_moves = i;
}


/* ------------------------------------------
    loadPokemonCSV - Integrates moveset parsing
------------------------------------------- */
static int loadPokemonCSV(const char* filename) {

    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("[POKEMON LOADER] ERROR: Cannot open %s\n", filename);
        return 0;
    }
    else{
        printf("[POKEMON LOADER] Loading data...");
    }

    char line[4096];
    fgets(line, sizeof(line), f); // Skip header
    int diagnostic_row_count = 0;
    while (fgets(line, sizeof(line), f)) {
        
        line[strcspn(line, "\r\n")] = 0; 
        diagnostic_row_count++;
        char* token;
        int col = 0;

        Pokemon p;
        memset(&p, 0, sizeof(Pokemon));

        char temp_line[4096];
        strncpy(temp_line, line, sizeof(temp_line));
        temp_line[sizeof(temp_line) - 1] = '\0';

        char* moveset_column_data = NULL; // Pointer to the moveset/abilities array string

        token = strtok(temp_line, ","); 
        
        while (token != NULL) {
            if(col == 0){
                moveset_column_data = token;
            }
            switch (col) {
                case 19: p.attack = atoi(token);
                         break;
                case 25: p.defense = atoi(token); break;
                case 26: p.hp = atoi(token); break;
                case 30: 
                    trim_trailing_whitespace(token); 
                    strncpy(p.name, token, sizeof(p.name) - 1); 
                    p.name[sizeof(p.name) - 1] = '\0';
                    break;
                case 33: p.sp_attack = atoi(token); break;
                case 34: p.sp_defense = atoi(token); break;
                case 36: strncpy(p.type1, token, sizeof(p.type1) - 1); break;
                case 37: strncpy(p.type2, token, sizeof(p.type2) - 1); break;
            }

            token = strtok(NULL, ",");
            col++;
        }

        if (strlen(p.name) == 0) continue;
        
        // --- SECOND PASS: Parse the moveset/ability array after column tokenizing is done ---
        if (moveset_column_data) {
            parse_combined_moveset(moveset_column_data, &p);
        }
        

        p.currentHP = p.hp; 
    
        
        if (pokemon_count < 1200) {
             pokedex[pokemon_count++] = p;
        }
    }

    fclose(f);
    return pokemon_count;
}
// ----------------- loadMovesCSV implementation needed here --------------------
#endif