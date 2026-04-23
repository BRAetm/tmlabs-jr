"""
SecretK.py — NBA 2K shot timing script.

This is a thin entry script. The actual engine lives in labs2kmain.py.
Drop your own .py file in this folder, import labs2kmain, and pass your
own settings dict to run() to add a new game / preset.

Usage:
    Spawned by Labs Engine. CLI args mirror the engine settings.
"""
import argparse
import sys

import labs2kmain


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--labs-pid",       type=int,   default=None, help="(unused, compat)")
    p.add_argument("--session",        type=int,   default=None, help="(unused, compat)")
    p.add_argument("--threshold",      type=float, default=0.95)
    p.add_argument("--threshold-l2",   type=float, default=0.75, dest="threshold_l2")
    p.add_argument("--tempo-ms",       type=int,   default=39,   dest="tempo_ms")
    p.add_argument("--tempo",          action="store_true")
    p.add_argument("--stick-tempo",    action="store_true", dest="stick_tempo")
    p.add_argument("--quickstop",      action="store_true")
    p.add_argument("--defense",        action="store_true")
    p.add_argument("--stamina",        action="store_true")
    p.add_argument("--no-hands-up",    action="store_true", dest="no_hands_up")
    p.add_argument("--xi-index",       type=int,   default=0,    dest="xi_index")
    p.add_argument("--gpu",            type=int,   default=0)
    p.add_argument("--target-fps",     type=int,   default=120,  dest="target_fps")
    p.add_argument("--passthrough-hz", type=int,   default=500,  dest="passthrough_hz")
    p.add_argument("--detect-every-n", type=int,   default=1,    dest="detect_every_n")
    p.add_argument("--low-end",        action="store_true",      dest="low_end")
    p.add_argument("--capture",        choices=["bettercam", "mss"], default="bettercam")
    args = p.parse_args()

    return labs2kmain.run(vars(args))


if __name__ == "__main__":
    sys.exit(main())
