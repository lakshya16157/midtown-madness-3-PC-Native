"""Arcade weight/power presets layered onto pristine retail donor physics.

TorqueScale values are native MM3 tuning units, not manufacturer Nm. Lower
weights use moderate torque to limit wheelspin with the donor's road-speed
automatic shifting. The two lightweight race cars also retain their explicit
anti-roll suspension profile; other fields inherit the original donor.
"""
from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import re

_spec = importlib.util.spec_from_file_location(
    "mm3_race_stability_defaults", Path(__file__).with_name("race_stability.py"))
stability = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(stability)

# Alias: (mass in kg, native MM3 torque scale). Keep reset presets separate from
# editable manifests so saving custom tuning cannot overwrite the reset target.
TUNING = {
    "Panoz": (980, 750),
    "McLaren_F1": (1050, 800),
    "Form1": (600, 420),
    "Ford_GTcar": (1520, 1000),
    "Senna_Road": (1309, 1000),
    "Senna_GTR1": (1270, 1025),
    "FXXK_Racer": (1590, 1150),
    "Regera_GT1": (1590, 1250),
    "AventadorX": (1675, 1100),
}


def apply_preset(car: dict) -> dict:
    """Reset driving only; preserve identity, model, paint, ratings and gauges.

    The shipped state uses the body donor's driving values, so any base stats
    car (driving_base) and factory engine opt-in are removed as well.
    """
    alias = car.get("alias")
    if alias not in TUNING:
        raise ValueError(f"No default driving setup for {alias!r}")
    result = copy.deepcopy(car)
    mass, torque = TUNING[alias]
    result["physics"] = {"Mass": mass, "TorqueScale": torque}
    result.pop("engine_profile", None)
    result.pop("driving_base", None)
    if "native_overrides" in result:
        remaining = [item for item in result["native_overrides"]
                     if not re.fullmatch(r"Car(?:Body|Wheel\d+)", item["section"], re.I)]
        if remaining:
            result["native_overrides"] = remaining
        else:
            result.pop("native_overrides")
    return stability.apply_profile(result)
