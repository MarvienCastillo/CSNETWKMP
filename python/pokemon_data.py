# pokemon_data.py
import csv

class Move:
    def __init__(self, name="", move_type="", power=0, category=""):
        self.name = name
        self.type = move_type
        self.power = power
        self.category = category

class Pokemon:
    def __init__(self):
        self.name = ""
        self.type1 = ""
        self.type2 = ""
        self.hp = 0
        self.attack = 0
        self.defense = 0
        self.sp_attack = 0
        self.sp_defense = 0
        self.currentHP = 0
        self.moveset = []
        self.num_moves = 0

# Global pokedex
pokedex = []

def trim_trailing_whitespace(s):
    return s.rstrip()

def parse_combined_moveset(token):
    """
    Parse moves/abilities list from a string like ['Move1','Move2']
    Returns a list of Move objects with only names filled.
    """
    if not token or len(token) < 3:
        return []
    # Remove brackets, quotes
    cleaned = token.replace('[','').replace(']','').replace('"','').replace("'","")
    move_names = [m.strip() for m in cleaned.split(",") if m.strip()]
    moves = [Move(name=m) for m in move_names]
    return moves

def load_pokemon_csv(filename):
    global pokedex
    pokedex = []
    with open(filename, newline='', encoding='utf-8') as csvfile:
        reader = csv.reader(csvfile)
        headers = next(reader)  # skip header
        for row in reader:
            if len(row) < 38:
                continue  # skip invalid rows
            p = Pokemon()
            p.attack = int(row[19])
            p.defense = int(row[25])
            p.hp = int(row[26])
            p.name = trim_trailing_whitespace(row[30])
            p.sp_attack = int(row[33])
            p.sp_defense = int(row[34])
            p.type1 = row[36]
            p.type2 = row[37]
            # column 0 contains the moveset string
            p.moveset = parse_combined_moveset(row[0])
            p.num_moves = len(p.moveset)
            p.currentHP = p.hp
            pokedex.append(p)
    return len(pokedex)

def get_pokemon(name):
    for p in pokedex:
        if p.name.lower() == name.lower():
            return p
    return None

# Optional: For testing
if __name__ == "__main__":
    count = load_pokemon_csv("pokemon.csv")
    print(f"Loaded {count} Pokémon")
    p = get_pokemon("Pikachu")
    if p:
        print(f"{p.name}: HP={p.hp}, Moves={[m.name for m in p.moveset]}")