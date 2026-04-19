# gym.py — reconstructed from gym.cp311-win_amd64.pyd
# Source: E:/PythonProjects/2kVision/nba2k/gym.c (Cython 3.0.12)
# MyCareer gym mini-game automation

BUTTON_8  = 8   # L2
BUTTON_10 = 10  # D-pad Up
BUTTON_11 = 11  # D-pad Down


class Workout:
    """Base workout automation class."""

    def __init__(self):
        self.frame_count = 0
        self.complete    = False

    def run(self, frame, gcvdata):
        """Main per-frame workout loop — .iterate(frame) pattern."""
        raise NotImplementedError

    def iterate(self, frame):
        """Single iteration step."""
        self.frame_count += 1


class BenchPress(Workout):
    def __init__(self): super().__init__()
    def iterate(self, frame): super().iterate(frame)


class Squats(Workout):
    def __init__(self): super().__init__()


class Sprints(Workout):
    def __init__(self): super().__init__()


class Treadmill(Workout):
    def __init__(self): super().__init__()


class ExerciseBike(Workout):
    def __init__(self): super().__init__()


class Pullup(Workout):
    def __init__(self): super().__init__()


class BoxJump(Workout):
    def __init__(self): super().__init__()


class MedicineBall(Workout):
    def __init__(self): super().__init__()


class AgilityLadder(Workout):
    def __init__(self): super().__init__()


class AgilityHurdles(Workout):
    def __init__(self): super().__init__()


class Reaction(Workout):
    """Reaction-time drill."""
    def __init__(self): super().__init__()


WORKOUT_MAP = {
    'BenchPress':    BenchPress,
    'Squats':        Squats,
    'Sprints':       Sprints,
    'Treadmill':     Treadmill,
    'ExerciseBike':  ExerciseBike,
    'Pullup':        Pullup,
    'BoxJump':       BoxJump,
    'MedicineBall':  MedicineBall,
    'AgilityLadder': AgilityLadder,
    'AgilityHurdles':AgilityHurdles,
    'Reaction':      Reaction,
}
