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

MOVES = {
    'thunderbolt': {'power': 90, 'type': 'Electric', 'category': 'special'},
    'flamethrower': {'power': 90, 'type': 'Fire', 'category': 'special'},
    'water_gun': {'power': 40, 'type': 'Water', 'category': 'special'},
    'tackle': {'power': 40, 'type': 'Normal', 'category': 'physical'},
    'vine_whip': {'power': 45, 'type': 'Grass', 'category': 'physical'},
    'ember': {'power': 40, 'type': 'Fire', 'category': 'special'},
    'thunder_shock': {'power': 40, 'type': 'Electric', 'category': 'special'},
    'bite': {'power': 60, 'type': 'Dark', 'category': 'physical'}
}

TYPE_EFFECTIVENESS = {
    'Normal': {'Rock': 0.5, 'Ghost': 0, 'Steel': 0.5},
    'Fire': {'Fire': 0.5, 'Water': 0.5, 'Grass': 2.0, 'Ice': 2.0, 'Bug': 2.0, 'Rock': 0.5, 'Dragon': 0.5, 'Steel': 2.0},
    'Water': {'Fire': 2.0, 'Water': 0.5, 'Grass': 0.5, 'Ground': 2.0, 'Rock': 2.0, 'Dragon': 0.5},
    'Electric': {'Water': 2.0, 'Electric': 0.5, 'Grass': 0.5, 'Ground': 0, 'Flying': 2.0, 'Dragon': 0.5},
    'Grass': {'Fire': 0.5, 'Water': 2.0, 'Grass': 0.5, 'Poison': 0.5, 'Ground': 2.0, 'Flying': 0.5, 'Bug': 0.5, 'Rock': 2.0, 'Dragon': 0.5, 'Steel': 0.5},
    'Ice': {'Fire': 0.5, 'Water': 0.5, 'Grass': 2.0, 'Ice': 0.5, 'Ground': 2.0, 'Flying': 2.0, 'Dragon': 2.0, 'Steel': 0.5},
    'Fighting': {'Normal': 2.0, 'Ice': 2.0, 'Poison': 0.5, 'Flying': 0.5, 'Psychic': 0.5, 'Bug': 0.5, 'Rock': 2.0, 'Ghost': 0, 'Dark': 2.0, 'Steel': 2.0, 'Fairy': 0.5},
    'Poison': {'Grass': 2.0, 'Poison': 0.5, 'Ground': 0.5, 'Rock': 0.5, 'Ghost': 0.5, 'Steel': 0, 'Fairy': 2.0},
    'Ground': {'Fire': 2.0, 'Electric': 2.0, 'Grass': 0.5, 'Poison': 2.0, 'Flying': 0, 'Bug': 0.5, 'Rock': 2.0, 'Steel': 2.0},
    'Flying': {'Electric': 0.5, 'Grass': 2.0, 'Fighting': 2.0, 'Bug': 2.0, 'Rock': 0.5, 'Steel': 0.5},
    'Psychic': {'Fighting': 2.0, 'Poison': 2.0, 'Psychic': 0.5, 'Dark': 0, 'Steel': 0.5},
    'Bug': {'Fire': 0.5, 'Grass': 2.0, 'Fighting': 0.5, 'Poison': 0.5, 'Flying': 0.5, 'Psychic': 2.0, 'Ghost': 0.5, 'Dark': 2.0, 'Steel': 0.5, 'Fairy': 0.5},
    'Rock': {'Fire': 2.0, 'Ice': 2.0, 'Fighting': 0.5, 'Ground': 0.5, 'Flying': 2.0, 'Bug': 2.0, 'Steel': 0.5},
    'Ghost': {'Normal': 0, 'Psychic': 2.0, 'Ghost': 2.0, 'Dark': 0.5},
    'Dragon': {'Dragon': 2.0, 'Steel': 0.5, 'Fairy': 0},
    'Dark': {'Fighting': 0.5, 'Psychic': 2.0, 'Ghost': 2.0, 'Dark': 0.5, 'Fairy': 0.5},
    'Steel': {'Fire': 0.5, 'Water': 0.5, 'Electric': 0.5, 'Ice': 2.0, 'Rock': 2.0, 'Steel': 0.5, 'Fairy': 2.0},
    'Fairy': {'Fire': 0.5, 'Fighting': 2.0, 'Poison': 0.5, 'Dragon': 2.0, 'Dark': 2.0, 'Steel': 0.5}
}

def get_type_effectiveness(move_type, defender_types):
    """Calculate type effectiveness multiplier"""
    effectiveness = 1.0
    for defender_type in defender_types:
        if move_type in TYPE_EFFECTIVENESS and defender_type in TYPE_EFFECTIVENESS[move_type]:
            effectiveness *= TYPE_EFFECTIVENESS[move_type][defender_type]
    return effectiveness
