import random

class BattleManager:
    def __init__(self, pokemon1, pokemon2, stat_boosts1, stat_boosts2, seed=None):
        self.p1 = pokemon1
        self.p2 = pokemon2
        self.boosts1 = stat_boosts1
        self.boosts2 = stat_boosts2
        self.turn = 0
        if seed:
            random.seed(seed)

    def damage_calc(self, attacker, defender, move_power, move_category, boosts):
        if move_category == "physical":
            att_stat = attacker.attack
            def_stat = defender.defense
        else:
            att_stat = attacker.sp_attack
            def_stat = defender.sp_defense

        # Apply stat boosts if available
        att_stat += boosts.get('special_attack_uses', 0)
        def_stat += boosts.get('special_defense_uses', 0)

        base_damage = move_power * (att_stat / max(1, def_stat))
        # For simplicity, TypeEffectiveness is 1.0 here (can add type chart)
        return max(0, int(base_damage))

    def apply_attack(self, attacker, defender, move_power, move_category, boosts):
        dmg = self.damage_calc(attacker, defender, move_power, move_category, boosts)
        defender.hp -= dmg
        return dmg
