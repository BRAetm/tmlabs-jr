"""
Combo data management — loading, recursive resolution, built-in combos.
Ported from Helios dribbles_main.py combo loading system.
"""

import json
import os


def load_combos(path):
    """
    Load combo definitions from a JSON file.
    Returns: dict of {name: action_list}
    """
    if not os.path.exists(path):
        return {}
    with open(path, "r") as f:
        data = json.load(f)
    return resolve_all(data)


def resolve_all(combo_map):
    """Resolve all combos, expanding chained references."""
    resolved = {}
    for name, entry in combo_map.items():
        resolved[name] = resolve_combo(entry, combo_map)
    return resolved


def resolve_combo(entry, combo_map):
    """
    Recursively resolve a combo entry into a flat action list.
    Entries can be:
    - A list of action dicts (already flat)
    - A list of combo name strings (chained)
    """
    if not entry:
        return []

    # Check if it's already a flat action list
    if isinstance(entry, list) and len(entry) > 0:
        if isinstance(entry[0], dict):
            return list(entry)  # Already flat
        if isinstance(entry[0], str):
            # Chained combo — resolve each sub-combo
            actions = []
            for sub_name in entry:
                sub = combo_map.get(sub_name)
                if sub is not None:
                    actions.extend(resolve_combo(sub, combo_map))
            return actions

    return []


def save_combos(path, combo_map):
    """Save combo definitions to a JSON file."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(combo_map, f, indent=2)


def mirror_combo(combo_data):
    """
    Mirror a combo (flip X-axis) for left-handed variants.
    Negates RX and LX control values.
    """
    mirrored = []
    for action in combo_data:
        a = dict(action)
        ctrl = a.get("control", "")
        if ctrl in ("RX", "LX") and "value" in a:
            a["value"] = -a["value"]
        mirrored.append(a)
    return mirrored


# -----------------------------------------------------------------------
# Built-in dribble combos for NBA 2K
# -----------------------------------------------------------------------

BUILTIN_COMBOS = {
    "behind_the_back": [
        {"type": "Hold", "control": "RX", "value": -100},
        {"type": "Wait", "wait": 80},
        {"type": "Hold", "control": "RY", "value": 100},
        {"type": "Wait", "wait": 60},
        {"type": "Release", "control": "RX"},
        {"type": "Wait", "wait": 40},
        {"type": "Release", "control": "RY"},
    ],

    "crossover": [
        {"type": "Hold", "control": "RX", "value": 100},
        {"type": "Wait", "wait": 100},
        {"type": "Release", "control": "RX"},
        {"type": "Wait", "wait": 50},
        {"type": "Hold", "control": "RX", "value": -100},
        {"type": "Wait", "wait": 100},
        {"type": "Release", "control": "RX"},
    ],

    "hesitation": [
        {"type": "Hold", "control": "RY", "value": -100},
        {"type": "Wait", "wait": 120},
        {"type": "Release", "control": "RY"},
    ],

    "spin_move": [
        {"type": "Hold", "control": "RX", "value": 100},
        {"type": "Wait", "wait": 50},
        {"type": "Hold", "control": "RY", "value": -100},
        {"type": "Wait", "wait": 50},
        {"type": "Hold", "control": "RX", "value": -100},
        {"type": "Wait", "wait": 50},
        {"type": "Hold", "control": "RY", "value": 100},
        {"type": "Wait", "wait": 50},
        {"type": "Release", "control": "RX"},
        {"type": "Release", "control": "RY"},
    ],

    "stepback": [
        {"type": "Hold", "control": "RY", "value": 100},
        {"type": "Wait", "wait": 150},
        {"type": "Release", "control": "RY"},
    ],

    "between_legs": [
        {"type": "Hold", "control": "RX", "value": -100},
        {"type": "Hold", "control": "RY", "value": -100},
        {"type": "Wait", "wait": 80},
        {"type": "Release", "control": "RX"},
        {"type": "Release", "control": "RY"},
    ],

    "size_up": [
        {"type": "Hold", "control": "RY", "value": -100},
        {"type": "Wait", "wait": 60},
        {"type": "Release", "control": "RY"},
        {"type": "Wait", "wait": 40},
        {"type": "Hold", "control": "RY", "value": -100},
        {"type": "Wait", "wait": 60},
        {"type": "Release", "control": "RY"},
    ],

    # Chained example
    "cross_to_behind": ["crossover", "behind_the_back"],
    "hesi_cross": ["hesitation", "crossover"],
}
