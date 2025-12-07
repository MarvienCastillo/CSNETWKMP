import csv

class Pokemon:
    def __init__(self, name, type1, type2, hp, attack, defense, sp_attack, sp_defense):
        self.name = name
        self.type1 = type1
        self.type2 = type2
        self.hp = int(hp)
        self.attack = int(attack)
        self.defense = int(defense)
        self.sp_attack = int(sp_attack)
        self.sp_defense = int(sp_defense)

def load_pokemon_csv(path):
    pokemons = {}
    with open(path, newline='', encoding='utf-8') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            p = Pokemon(
                row['name'],
                row['type1'],
                row['type2'],
                row['hp'],
                row['attack'],
                row['defense'],
                row['sp_attack'],
                row['sp_defense']
            )
            pokemons[p.name.lower()] = p
    return pokemons

def get_pokemon_by_name(pokemons, name):
    return pokemons.get(name.lower())
