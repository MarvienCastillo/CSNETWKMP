from dataclasses import dataclass, field

@dataclass
class Pokemon:
    name: str
    hp: int
    attack: int
    defense: int
    sp_attack: int
    sp_defense: int
    speed: int
    type1: str
    type2: str = None

@dataclass
class BattleContext:
    my_pokemon: Pokemon = None
    opp_pokemon: Pokemon = None
    last_move_used: str = ""
    last_damage: int = 0
    last_remaining_hp: int = 0
    current_sequence_num: int = 0

@dataclass
class BattleManager:
    ctx: BattleContext = field(default_factory=BattleContext)
    outgoing_message: str = ""

    def init_battle(self, my_pokemon: Pokemon, opp_pokemon: Pokemon):
        self.ctx.my_pokemon = my_pokemon
        self.ctx.opp_pokemon = opp_pokemon
        print(f"[BATTLE] {my_pokemon.name} VS {opp_pokemon.name}")

    def handle_attack(self, move_name: str):
        dmg = max(1, self.ctx.my_pokemon.attack - self.ctx.opp_pokemon.defense // 2)
        self.ctx.opp_pokemon.hp -= dmg
        self.ctx.last_move_used = move_name
        self.ctx.last_damage = dmg
        self.ctx.last_remaining_hp = self.ctx.opp_pokemon.hp
        self.ctx.current_sequence_num += 1
        self.outgoing_message = (
            f"message_type: ATTACK_ANNOUNCE\n"
            f"attacker: {self.ctx.my_pokemon.name}\n"
            f"move_used: {move_name}\n"
            f"damage_dealt: {dmg}\n"
            f"defender_hp_remaining: {self.ctx.opp_pokemon.hp}\n"
            f"sequence_number: {self.ctx.current_sequence_num}\n"
        )
        print(f"[BATTLE] {self.ctx.my_pokemon.name} used {move_name} for {dmg} damage!")

    def handle_defense(self):
        self.ctx.current_sequence_num += 1
        self.outgoing_message = (
            f"message_type: DEFENSE_ANNOUNCE\n"
            f"sequence_number: {self.ctx.current_sequence_num}\n"
        )
        print(f"[BATTLE] Defense announced!")

    def calculation_report(self):
        self.ctx.current_sequence_num += 1
        self.outgoing_message = (
            f"message_type: CALCULATION_REPORT\n"
            f"attacker: {self.ctx.my_pokemon.name}\n"
            f"move_used: {self.ctx.last_move_used}\n"
            f"damage_dealt: {self.ctx.last_damage}\n"
            f"defender_hp_remaining: {self.ctx.last_remaining_hp}\n"
            f"sequence_number: {self.ctx.current_sequence_num}\n"
        )

    def calculation_confirm(self):
        self.ctx.current_sequence_num += 1
        self.outgoing_message = (
            f"message_type: CALCULATION_CONFIRM\n"
            f"sequence_number: {self.ctx.current_sequence_num}\n"
        )

    def resolution_request(self):
        self.ctx.current_sequence_num += 1
        self.outgoing_message = (
            f"message_type: RESOLUTION_REQUEST\n"
            f"attacker: {self.ctx.my_pokemon.name}\n"
            f"move_used: {self.ctx.last_move_used}\n"
            f"damage_dealt: {self.ctx.last_damage}\n"
            f"defender_hp_remaining: {self.ctx.last_remaining_hp}\n"
            f"sequence_number: {self.ctx.current_sequence_num}\n"
        )

    def trigger_game_over(self):
        self.outgoing_message = f"message_type: GAME_OVER\nwinner: {self.ctx.my_pokemon.name}\n"

    def get_outgoing_message(self):
        return self.outgoing_message

    def clear_outgoing_message(self):
        self.outgoing_message = ""
