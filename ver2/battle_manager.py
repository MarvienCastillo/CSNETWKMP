import json

class BattleManager:
    def __init__(self):
        self.battles = {}  # peer -> battle_state
        self.sequence_numbers = {}  # peer -> seq number
        self.pending_turn = {}  # attacker -> defender

    def increment_seq(self, peer):
        self.sequence_numbers[peer] += 1
        return self.sequence_numbers[peer]

    def announce_attack(self, attacker_peer, move_type, defender_peer):
        battle = self.battles[attacker_peer]
        battle["state"] = "WAITING_FOR_DEFENSE"
        self.pending_turn[attacker_peer] = defender_peer
        seq = self.increment_seq(attacker_peer)
        return {
            "message_type": "ATTACK_ANNOUNCE",
            "sequence": seq,
            "move_type": move_type
        }

    def announce_defense(self, defender_peer, attacker_peer):
        battle = self.battles[defender_peer]
        battle["state"] = "PROCESSING_TURN"
        seq = self.increment_seq(defender_peer)
        return {
            "message_type": "DEFENSE_ANNOUNCE",
            "sequence": seq
        }

    def calculate_damage(self, attacker_peer, defender_peer, move_type):
        attacker = self.battles[attacker_peer]
        defender = self.battles[defender_peer]

        if move_type == "SPECIAL_ATTACK" and attacker["special_attack_uses"] > 0:
            damage = max(1, attacker["pokemon"].sp_attack - defender["pokemon"].sp_defense)
            attacker["special_attack_uses"] -= 1
        elif move_type == "SPECIAL_DEFENSE" and attacker["special_defense_uses"] > 0:
            damage = 0
            heal = min(attacker["pokemon"].sp_defense, attacker["pokemon"].hp - attacker["current_hp"])
            attacker["current_hp"] += heal
        else:
            damage = max(1, attacker["pokemon"].attack - defender["pokemon"].defense)

        defender["current_hp"] -= damage
        attacker["turn"] += 1
        seq = self.increment_seq(attacker_peer)

        report = {
            "message_type": "CALCULATION_REPORT",
            "sequence": seq,
            "attacker": attacker_peer,
            "defender": defender_peer,
            "remaining_hp_attacker": attacker["current_hp"],
            "remaining_hp_defender": defender["current_hp"]
        }
        return report

    def apply_calculation_confirm(self, peer):
        self.battles[peer]["state"] = "WAITING_FOR_MOVE"

    def request_resolution(self, peer, attacker_hp, defender_hp):
        return {
            "message_type": "RESOLUTION_REQUEST",
            "remaining_hp_attacker": attacker_hp,
            "remaining_hp_defender": defender_hp
        }

    def ack_resolution(self, peer):
        return {"message_type": "ACK"}

    def setup_battle(self, addr, pokemon, boosts, mode):
        self.battles[addr] = {
            "pokemon": pokemon,
            "boosts": boosts,
            "mode": mode,
            "turn": 0,
            "current_player": None,
            "battle_over": False,
            "hp": pokemon.hp,
            "special_attack_uses": boosts.get("special_attack_uses",4),
            "special_defense_uses": boosts.get("special_defense_uses",3)
        }

    def show_battle(self, addr):
        battle = self.battles.get(addr)
        if not battle:
            print(f"[BATTLE] No battle for {addr}")
            return
        print(f"[BATTLE][{addr}] Pokémon={battle['pokemon'].name}, HP={battle['hp']}, "
              f"Boosts={battle['boosts']}, Turn={battle['turn']}, Current={battle['current_player']}")

    def process_attack(self, attacker, defender, move_type):
        battle = self.battles[attacker]
        if battle["battle_over"]:
            return
        damage = 0
        if move_type == "ATTACK":
            damage = max(1, battle["pokemon"].attack - self.battles[defender]["pokemon"].defense)
        elif move_type == "SPECIAL_ATTACK" and battle["special_attack_uses"] > 0:
            damage = max(1, battle["pokemon"].sp_attack - self.battles[defender]["pokemon"].sp_defense)
            battle["special_attack_uses"] -= 1
        elif move_type == "SPECIAL_DEFENSE" and battle["special_defense_uses"] > 0:
            heal = min(battle["pokemon"].sp_defense, battle["pokemon"].hp - battle["hp"])
            battle["hp"] += heal
            battle["special_defense_uses"] -= 1
            print(f"[BATTLE][{attacker}] used SPECIAL_DEFENSE, healed {heal} HP")
            return 0  # no damage
        self.battles[defender]["hp"] -= damage
        print(f"[BATTLE][{attacker}] {move_type} -> {defender}, damage={damage}, defender HP={self.battles[defender]['hp']}")
        battle["turn"] += 1
        # Switch player
        if battle["current_player"] == "HOST":
            battle["current_player"] = "JOINER"
        else:
            battle["current_player"] = "HOST"
        if self.battles[defender]["hp"] <= 0:
            self.battles[defender]["battle_over"] = True
            print(f"[BATTLE] {defender} fainted! Winner: {battle['current_player']}")
        return damage
