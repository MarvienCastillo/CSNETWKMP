import csv

# --- Helper Function to Parse Abilities ---
def parse_abilities_string(abilities_str):
    """
    Parses a string like "['Overgrow':'Chlorophyll']" into a list of strings.
    It handles colons or commas as separators, and removes brackets/quotes.
    """
    if not abilities_str:
        return []
    
    # Remove leading/trailing brackets, and quotes
    clean_str = abilities_str.strip().strip('[').strip(']').strip("'")
    
    # Use colon (':') as the primary separator, falling back to comma (',')
    if ':' in clean_str:
        separators = ':'
    else:
        separators = ','
        
    # Split the string and strip any remaining quotes/whitespace from each item
    abilities = [
        ability.strip().strip("'") 
        for ability in clean_str.split(separators) 
        if ability.strip() # ensures empty strings are not included
    ]
    
    return abilities

# --- Pokemon Class ---
class Pokemon:
    def __init__(self, name, type1, type2, hp, attack, defense, sp_attack, sp_defense, abilities, movelist):
        self.name = name
        self.type1 = type1
        self.type2 = type2
        self.hp = int(hp)
        self.attack = int(attack)
        self.defense = int(defense)
        self.sp_attack = int(sp_attack)
        self.sp_defense = int(sp_defense)
        self.abilities = abilities    # <<< NEW: Stored as a Python list

# --- CSV Loader Function ---
def load_pokemon_csv(path):
    pokemons = {}
    with open(path, newline='', encoding='utf-8') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            
            # --- NEW ABILITIES PARSING ---
            abilities_raw = row['abilities']
            abilities_list = parse_abilities_string(abilities_raw)
            # -----------------------------
            
            p = Pokemon(
                row['name'],
                row['type1'],
                row['type2'],
                row['hp'],
                row['attack'],
                row['defense'],
                row['sp_attack'],
                row['sp_defense'],
                abilities_list, # <<< PASSES THE PARSED LIST
            )
            pokemons[p.name.lower()] = p
    return pokemons

def get_pokemon_by_name(pokemons, name):
    return pokemons.get(name.lower())

def get_pokemon_moves(pokemons, move_name, pokemon_name):
    """
    Checks if a specific move is available to a given Pokémon.

    :param pokemons: The dictionary of all loaded Pokémon objects.
    :param move_name: The name of the move to search for (e.g., "Tackle").
    :param pokemon_name: The name of the Pokémon to check (e.g., "Pikachu").
    :return: The move name (string) if found, otherwise None.
    """
    pokemon = get_pokemon_by_name(pokemons, pokemon_name)

    if pokemon: 
        # Iterate through the correct attribute: pokemon.movelists
        for move in pokemon.movelists:
            if move.lower() == move_name.lower(): # Use .lower() for case-insensitive matching
                return move # Return the move string if found
        
        # If the loop completes without finding the move
        print(f"Move '{move_name}' not found for {pokemon.name}")
        return None
    else:
        print(f"Pokemon '{pokemon_name}' not found.")
        return None
