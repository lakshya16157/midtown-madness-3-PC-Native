"""Explicit suspension profiles for the two lightweight imported race cars.

Driving-base selection copies engine/driving fields, not the donor body's
suspension. Use the retail Koenigsegg anti-roll stiffness on all four corners
of the Viper/Lotus bodies. Keep wheel mounts, springs, damping and collision
geometry unchanged. The profile remains editable in native Game stats.
"""
from __future__ import annotations

import copy


ALIASES = frozenset({"Panoz", "Form1"})
ROLL_STIFFNESS = 44200.0  # Retail Koenigsegg CarWheel0..3.RollStiffness.


def profile(alias: str) -> list[dict]:
    if alias not in ALIASES:
        return []
    return [{"section": f"CarWheel{index}", "key": "RollStiffness",
             "occurrence": 0, "value": format(ROLL_STIFFNESS, ".1f")}
            for index in range(4)]


def apply_profile(car: dict, *, limit_excess_grip=False) -> dict:
    """Replace only the profile's four entries, preserving unrelated tuning.

    The optional grip cap repairs the local F1 setup's 4.5 multiplier; the
    reset preset normally inherits the retail 2.0 value instead.
    """
    result = copy.deepcopy(car)
    edits = profile(car.get("alias"))
    if not edits:
        return result
    keys = {(item["section"].casefold(), item["key"].casefold(), item["occurrence"])
            for item in edits}
    remaining = [item for item in result.get("native_overrides", [])
                 if (item["section"].casefold(), item["key"].casefold(),
                     item["occurrence"]) not in keys]
    result["native_overrides"] = remaining + edits
    if limit_excess_grip and car["alias"] == "Form1":
        grip = result["physics"].get("LatFrictionFactor")
        if grip is not None and grip > 2.2:
            result["physics"]["LatFrictionFactor"] = 2.2
    return result
