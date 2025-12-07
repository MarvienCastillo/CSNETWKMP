# battle_manager.py
from pokemon_data import get_type_effectiveness

class BattleManager:
    def __init__(self):
        self.battles = {}  # "HOST" or "JOINER" -> battle state
        self.pending_attacks = {}  # key -> {"HOST": None, "JOINER": None}

    def setup_battle(self, key, pokemon, boosts, mode):
        self.battles[key] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "hp": 100,  # start at 100
            "turn": 0,
            "current_player": "HOST" if key == "HOST" else "JOINER",
            "battle_over": False,
            "special_attack_uses": boosts.get("special_attack_uses", 4),
            "special_defense_uses": boosts.get("special_defense_uses", 3),
        }
        if key not in self.pending_attacks:
            self.pending_attacks[key] = {"HOST": None, "JOINER": None}

    def show_battle(self, key):
        battle = self.battles.get(key)
        if not battle:
            print(f"[BATTLE] No battle for {key}")
            return
        print(f"[BATTLE][{key}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
              f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def queue_attack(self, key, player_key, move_name, move_type="ATTACK"):
        """Store attack until both moves are queued, then resolve turn"""
        if key not in self.pending_attacks:
            self.pending_attacks[key] = {"HOST": None, "JOINER": None}
        self.pending_attacks[key][player_key] = {"move_name": move_name, "move_type": move_type}
        self.resolve_turn(key)  # will only apply if both moves are queued

    def calculate_damage(self, attacker, defender, move_type):
        """Damage formula using Pokémon stats, boosts, and type effectiveness"""
        base_power = 1.0
        # Use attack stats depending on move_type
        if move_type == "ATTACK":
            attacker_stat = attacker["pokemon"].attack
            defender_stat = defender["pokemon"].defense
        elif move_type == "SPECIAL_ATTACK" and attacker["special_attack_uses"] > 0:
            attacker_stat = attacker["pokemon"].sp_attack
            defender_stat = defender["pokemon"].sp_defense
            attacker["special_attack_uses"] -= 1
        elif move_type == "SPECIAL_DEFENSE" and attacker["special_defense_uses"] > 0:
            heal = min(attacker["pokemon"].sp_defense, 100 - attacker["hp"])
            attacker["hp"] += heal
            attacker["special_defense_uses"] -= 1
            return 0  # healing move doesn't deal damage
        else:
            attacker_stat = attacker["pokemon"].attack
            defender_stat = defender["pokemon"].defense

        # Type effectiveness (simplified; assumes move type same as attacker type1)
        type_mult1 = get_type_effectiveness(attacker["pokemon"].type1, defender["pokemon"].type1)
        type_mult2 = 1.0
        if defender["pokemon"].type2:
            type_mult2 = get_type_effectiveness(attacker["pokemon"].type1, defender["pokemon"].type2)
        type_effectiveness = type_mult1 * type_mult2

        damage = max(1, int((attacker_stat / max(1, defender_stat)) * base_power * type_effectiveness))
        return damage

    def resolve_turn(self, key):
        """Apply attacks once both moves are queued"""
        pending = self.pending_attacks.get(key)
        if not pending:
            return
        if pending["HOST"] is None or pending["JOINER"] is None:
            return  # wait for both moves

        host_move = pending["HOST"]
        joiner_move = pending["JOINER"]

        # Apply host attack -> joiner
        damage_to_joiner = self.calculate_damage(self.battles["HOST"], self.battles["JOINER"], host_move["move_type"])
        self.battles["JOINER"]["hp"] -= damage_to_joiner

        # Apply joiner attack -> host
        damage_to_host = self.calculate_damage(self.battles["JOINER"], self.battles["HOST"], joiner_move["move_type"])
        self.battles["HOST"]["hp"] -= damage_to_host

        # Increment turns
        self.battles["HOST"]["turn"] += 1
        self.battles["JOINER"]["turn"] += 1

        # Show summary
        print(f"[TURN RESOLVE] HOST used {host_move['move_name']} -> damage {damage_to_joiner}, "
              f"JOINER HP={self.battles['JOINER']['hp']}")
        print(f"[TURN RESOLVE] JOINER used {joiner_move['move_name']} -> damage {damage_to_host}, "
              f"HOST HP={self.battles['HOST']['hp']}")

        # Check game over
        host_fainted = self.battles["HOST"]["hp"] <= 0
        joiner_fainted = self.battles["JOINER"]["hp"] <= 0
        if host_fainted and joiner_fainted:
            print("[BATTLE] Both Pokémon fainted! Draw!")
            self.battles["HOST"]["battle_over"] = True
            self.battles["JOINER"]["battle_over"] = True
        elif host_fainted:
            print("[BATTLE] HOST Pokémon fainted! JOINER wins!")
            self.battles["HOST"]["battle_over"] = True
            self.battles["JOINER"]["battle_over"] = True
        elif joiner_fainted:
            print("[BATTLE] JOINER Pokémon fainted! HOST wins!")
            self.battles["HOST"]["battle_over"] = True
            self.battles["JOINER"]["battle_over"] = True

        # Reset pending attacks
        self.pending_attacks[key] = {"HOST": None, "JOINER": None}
