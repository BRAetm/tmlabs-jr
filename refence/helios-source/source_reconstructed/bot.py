# bot.py — reconstructed from bot.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/bot.c (Cython 3.0.12)

import time
import cv2
import numpy as np

# Button constants (DS5 mapping)
BUTTON_8  = 8   # L2
BUTTON_16 = 16  # Square
BUTTON_17 = 17  # Triangle


class AIConfig:
    """AI personality/role configuration."""

    ROLES = {
        'PG': {'paint_shots': False, 'perimeter_priority': True,  'chase_ball': True},
        'SG': {'paint_shots': False, 'perimeter_priority': True,  'chase_ball': True},
        'SF': {'paint_shots': True,  'perimeter_priority': False, 'chase_ball': False},
        'PF': {'paint_shots': True,  'perimeter_priority': False, 'chase_ball': False},
        'C':  {'paint_shots': True,  'perimeter_priority': False, 'chase_ball': False},
    }

    def __init__(self, role='PG'):
        self.role = role
        self.params = self.ROLES.get(role, self.ROLES['PG']).copy()

    def get_role_modifiers(self):
        """Adjust AI personality parameters based on role."""
        return self.params


class Action:
    """Base class for all actions."""

    def __init__(self, duration=1):
        self.duration = duration       # frames
        self.frame_count = 0
        self.complete = False

    def step(self, gcvdata):
        self.frame_count += 1
        if self.frame_count >= self.duration:
            self.complete = True


class DribbleMove(Action):
    def __init__(self, inputs, duration):
        super().__init__(duration)
        # inputs: list of (button, value) pairs per frame
        # 'timer': [3, 3] — frames for each input phase
        self.inputs = inputs

    def get_current_input(self):
        idx = min(self.frame_count, len(self.inputs) - 1)
        return self.inputs[idx]


class ShootAction(Action):
    """Execute shooting action - hold button for duration."""

    def __init__(self, button=BUTTON_16, duration=8):
        super().__init__(duration)
        self.button = button

    def step(self, gcvdata):
        gcvdata.set_button(self.button, 100)
        super().step(gcvdata)
        if self.complete:
            gcvdata.set_button(self.button, 0)


class DefendAction(Action):
    def __init__(self, target_pos, jump=False, follow=False):
        super().__init__()
        self.target_pos = target_pos
        self.jump = jump
        self.follow = follow


class MoveAction(Action):
    """Execute movement to target position."""

    def __init__(self, target):
        super().__init__()
        self.target = target  # (x, y) court coords

    def is_in_bounds(self, pos):
        """Check if move target is within court bounds."""
        x, y = pos
        return 0 <= x <= 1920 and 0 <= y <= 1080


class PassAction(Action):
    def __init__(self, target_player_id):
        super().__init__(duration=2)
        self.target_player_id = target_player_id


class ActionManager:
    def __init__(self):
        self.queue = []
        self.current = None
        self._init_dribble_moves()

    def _init_dribble_moves(self):
        """Pre-build dribble move library."""
        self.dribble_library = {}


class DecisionEngine:
    """
    Core decision-making. Determines game phase and selects actions.
    """

    PHASES = ['offense', 'defense', 'transition', 'loose_ball']

    def __init__(self, config: AIConfig):
        self.config = config
        self.phase = 'offense'
        self.history = []

    def _determine_phase(self, game_state):
        """State machine: determine current game phase."""
        if game_state.ball_loose:
            return 'loose_ball'
        if game_state.on_offense:
            return 'offense'
        return 'defense'

    def update(self, game_state):
        """Update phase and return next action."""
        self.phase = self._determine_phase(game_state)

        if self.phase == 'offense':
            return self._offense_decision(game_state)
        elif self.phase == 'defense':
            return self._defense_decision(game_state)
        elif self.phase == 'loose_ball':
            return self._loose_ball_decision(game_state)
        return None

    def _offense_decision(self, game_state):
        role = self.config.params
        if game_state.open_shot and not (not role['paint_shots'] and game_state.in_paint):
            return ShootAction()
        if game_state.can_pass:
            return PassAction(game_state.best_pass_target)
        return MoveAction(game_state.best_move_target)

    def _defense_decision(self, game_state):
        return DefendAction(
            target_pos=game_state.my_matchup_pos,
            jump=False,
            follow=True
        )

    def _loose_ball_decision(self, game_state):
        """Dynamic Target Tracking: Follow moving objects (loose balls)."""
        return MoveAction(game_state.ball_pos)


class GameState:
    """Create GameState from tracker output."""

    def __init__(self):
        self.ball_pos = (0, 0)
        self.ball_loose = False
        self.on_offense = True
        self.open_shot = False
        self.in_paint = False
        self.can_pass = False
        self.best_pass_target = None
        self.best_move_target = (960, 540)
        self.my_matchup_pos = (960, 540)


class Bot:
    """
    Main bot class.
    - Guards (PG/SG): Never shoot in paint, prioritize perimeter, chase balls longest
    - State Machine: Track multi-frame actions (shoot, dribble, move)
    - Dynamic Target Tracking: Follow moving objects (loose balls)
    - Prevention Logic: Avoid paint violations, shot loops, out-of-bounds
    """

    def __init__(self, role='PG'):
        self.config = AIConfig(role)
        self.decision_engine = DecisionEngine(self.config)
        self.action_manager = ActionManager()
        self.debug = False
        self.game_state = GameState()

    def run(self, frame, gcvdata, tracker_output):
        """Main per-frame entry point called by Helper.run()."""
        self._update_game_state(tracker_output)
        action = self.decision_engine.update(self.game_state)
        self._apply_inputs(action, gcvdata)
        if self.debug:
            self._draw_debug_info(frame)
        return frame

    def _apply_inputs(self, action, gcvdata):
        """Apply generic inputs to gcvdata."""
        self._reset_controls(gcvdata)
        if action:
            action.step(gcvdata)

    def _reset_controls(self, gcvdata):
        pass

    def _update_game_state(self, tracker_output):
        if tracker_output:
            self.game_state.ball_pos = tracker_output.get('ball', (0, 0))

    def _execute_shoot(self, gcvdata):
        pass

    def _execute_dribble(self, gcvdata):
        pass

    def _execute_move(self, gcvdata):
        """Execute movement to target position."""
        pass

    def _execute_pass(self, gcvdata):
        pass

    def _execute_defend(self, gcvdata):
        pass

    def set_personality(self, role):
        self.config = AIConfig(role)
        self.decision_engine.config = self.config

    def toggle_debug(self):
        self.debug = not self.debug

    def add_custom_dribble(self, name, inputs, duration):
        self.action_manager.dribble_library[name] = DribbleMove(inputs, duration)

    def _draw_debug_info(self, frame):
        """Draw debug information on frame."""
        cv2.putText(frame, f"Phase: {self.decision_engine.phase}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 1)
        cv2.putText(frame, f"At Target: {self.game_state.best_move_target}", (10, 55),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 1)
        cv2.putText(frame, f"DEF POS: {self.game_state.my_matchup_pos}", (10, 80),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 0, 0), 1)
