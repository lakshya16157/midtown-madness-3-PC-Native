"""Validated, occurrence-scoped edits of existing native tuning fields.

Managed driving settings keep their existing manifest validation. This module
handles the remaining numeric, vector and explicit boolean tuning values and
preserves the donor's section order, repeated keys, comments and whitespace.
"""
from __future__ import annotations

from collections import defaultdict
import copy
import math
import re


# Kept here so the editor and installer use exactly the same edit policy.
MANAGED_BODY = frozenset({
    "Mass", "TorqueScale", "TorqueScaleFactor", "TurboSize", "WheelDrive",
    "BrakeTorque", "HandBrakeTorque", "LatFrictionFactor", "AirKZZ", "AirKZY",
    "MaxTorqueRPM", "MaxRPM", "CutRPM", "MinRPM", "TimeChangeGear",
    "TimeChangeGearFR", "WheelRatio", "MinRPMGearChange", "MaxTurboTorqueRPM",
    "GearStatusDownGearFactor", "SteeringEndSpeed", "WheelRadius", "NGears",
    "GearRatioVector", "TurboSpinup",
})
_MANAGED_BODY = {key.casefold() for key in MANAGED_BODY}
_MARKER = re.compile(r"^(Begin|End)\s*\(\s*([^()]+?)\s*\)\s*$", re.I)
_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
_TUNING_SECTION = re.compile(r"Car(?:Body|Wheel\d+|Camera(?:SS)?)", re.I)
_INTEGER = re.compile(r"[+-]?(?:\d+|0[xX][0-9a-fA-F]+)")
_NUMBER = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?")
_COMMENT = re.compile(r"[ \t]+(?://|#|;)")
_ENTRY = re.compile(r"([ \t]*)([^ \t\r\n]+)([ \t]*)(.*)")
_PROTECTED = frozenset({
    "id", "nwheels", "ncolpoints", "tyretype", "tiretype", "transmission",
    "rpmtable", "color", "skin", "locked", "hidden", "admittance",
    "vehicleclass", "speedometertype", "paintjobname",
})


def _field_reason(section: str, key: str) -> str:
    """Reject metadata, array sizes and references even if their values are numeric."""
    if not _TUNING_SECTION.fullmatch(section):
        return "This identity or catalog field is managed by Car Workshop."
    lower = key.casefold()
    if section.casefold() == "carbody" and lower in _MANAGED_BODY:
        return "Use the driving, gears or RPM controls for this managed setting."
    if lower in {"speedometermaxkmh", "speedometermaxmph"}:
        return "Use the speedometer controls for this managed setting."
    if (lower in _PROTECTED or re.match(r"N[A-Z]", key)
            or re.search(r"(?:count|index|indices|type|table|file|path|name|resource|model|ref|reference|id)$", lower)):
        return "Counts, identities and resource selectors cannot be edited here."
    return ""


def _tokens(raw: str, *, original: bool = False) -> tuple[str, list[str]]:
    """Return a strict scalar/vector shape, with no comments or parser syntax."""
    if not isinstance(raw, str) or len(raw) > 4096:
        raise ValueError("A native value must be a text value of at most 4096 characters")
    if any(ord(char) < 32 and char != "\t" for char in raw) or any(ord(char) > 126 for char in raw):
        raise ValueError("A native value must stay on one line and contain ordinary numbers or true/false")
    value = raw.strip(" \t")
    if original:
        value = _COMMENT.split(value, maxsplit=1)[0].strip(" \t")
    if value.casefold() in {"true", "false"}:
        return "boolean", [value.casefold()]
    tokens = value.split()
    if not tokens or len(tokens) > 64:
        raise ValueError("Enter a number, a numeric vector, or true/false")
    for token in tokens:
        if _INTEGER.fullmatch(token):
            # Ordinary tuning fields are native floats even when their text
            # happens to spell a component as an integer (notably camera X=0).
            # Integer selectors and array counts are excluded by edit policy.
            number = int(token, 16 if "x" in token.casefold() else 10)
            if abs(number) > 3.4028234663852886e38:
                raise ValueError("Native numbers must be finite and fit a 32-bit float")
        elif _NUMBER.fullmatch(token):
            number = float(token)
            if not math.isfinite(number) or abs(number) > 3.4028234663852886e38:
                raise ValueError("Native numbers must be finite and fit a 32-bit float")
        else:
            raise ValueError("Enter only numbers separated by spaces, or true/false")
    return "number", tokens


def edit_reason(section: str, key: str, raw: str) -> str:
    """Empty for an editable native value; otherwise a user-facing reason."""
    if not isinstance(section, str) or not isinstance(key, str):
        return "This is not an editable native tuning field."
    reason = _field_reason(section, key)
    if reason:
        return reason
    try:
        _tokens(raw, original=True)
    except ValueError:
        return "Only numeric values, numeric vectors and explicit true/false settings can be edited."
    return ""


def validate_value(original_raw: str, proposed: str) -> str:
    """Preserve numeric/boolean type and vector arity, accepting numeric fractions.

    Native tuning floats can be written as ``0`` or ``70000`` in retail data;
    that spelling must never turn camera positions or suspension into integers.
    """
    original_kind, original = _tokens(original_raw, original=True)
    proposed_kind, replacement = _tokens(proposed)
    if original_kind != proposed_kind:
        raise ValueError("Keep the original value type: a number/vector or true/false")
    if len(original) != len(replacement):
        raise ValueError(f"This native setting requires {len(original)} value(s)")
    if original_kind == "boolean":
        return replacement[0]
    return " ".join(replacement)


def validate_field_value(section: str, key: str, original_raw: str, proposed: str) -> str:
    """Check one existing field, including native boolean values encoded as 0/1."""
    reason = edit_reason(section, key, original_raw)
    if reason:
        raise ValueError(f"{section}.{key}: {reason}")
    result = validate_value(original_raw, proposed)
    kind, tokens = _tokens(original_raw, original=True)
    if (re.match(r"(?:b[A-Z]|Use[A-Z])", key) and kind == "number"
            and len(tokens) == 1 and tokens[0] in {"0", "1"} and result not in {"0", "1"}):
        raise ValueError(f"{section}.{key}: this native boolean requires 0 or 1")
    return result


def validate_overrides(overrides: list[dict]) -> None:
    """Validate manifest syntax; existing-field/type checks need the donor text."""
    if not isinstance(overrides, list) or len(overrides) > 4096:
        raise ValueError("native_overrides must be an array of at most 4096 setting edits")
    seen = set()
    for item in overrides:
        if not isinstance(item, dict) or set(item) != {"section", "key", "occurrence", "value"}:
            raise ValueError("Each native override requires exactly section, key, occurrence and value")
        section, key, occurrence = item["section"], item["key"], item["occurrence"]
        if (not isinstance(section, str) or not isinstance(key, str)
                or len(section) > 100 or len(key) > 100
                or not _NAME.fullmatch(section) or not _NAME.fullmatch(key)):
            raise ValueError("Native section and key names must be existing simple identifiers")
        if type(occurrence) is not int or not 0 <= occurrence < 4096:
            raise ValueError("Native occurrence must be a nonnegative integer below 4096")
        identity = section, key, occurrence
        if identity in seen:
            raise ValueError(f"Duplicate native override for {section}.{key} occurrence {occurrence}")
        seen.add(identity)
        reason = _field_reason(section, key)
        if reason:
            raise ValueError(f"{section}.{key}: {reason}")
        _tokens(item["value"])


def without_driving(overrides: list[dict]) -> list[dict]:
    """Remove body/wheel tuning for driving resets, keeping camera edits."""
    return copy.deepcopy([item for item in overrides
                          if not re.fullmatch(r"Car(?:Body|Wheel\d+)", item["section"], re.I)])


def apply_overrides(text: str, overrides: list[dict]) -> str:
    """Change only exact existing entries, retaining section/key occurrences."""
    validate_overrides(overrides)
    if not overrides:
        return text
    pending = {(item["section"], item["key"], item["occurrence"]): item["value"]
               for item in overrides}
    occurrences = defaultdict(int)
    section = None
    output = []
    for line_number, line in enumerate(text.splitlines(keepends=True), 1):
        content = line.rstrip("\r\n")
        ending = line[len(content):]
        stripped = content.strip().lstrip("\ufeff")
        if not stripped or stripped.startswith(("//", "#", ";")):
            output.append(line)
            continue
        marker = _MARKER.fullmatch(_COMMENT.split(stripped, maxsplit=1)[0])
        if marker:
            operation, name = marker.groups()
            if operation.casefold() == "begin":
                if section is not None:
                    raise ValueError(f"Nested native section at line {line_number}: {name}")
                section = name
            else:
                if section is None or section.casefold() != name.casefold():
                    raise ValueError(f"Unmatched native End({name}) at line {line_number}")
                section = None
            output.append(line)
            continue
        match = _ENTRY.fullmatch(content)
        if match is None:
            raise ValueError(f"Invalid native entry at line {line_number}")
        indent, key, separator, raw = match.groups()
        identity = section or "Global", key, occurrences[section or "Global", key]
        occurrences[section or "Global", key] += 1
        if identity not in pending:
            output.append(line)
            continue
        comment = _COMMENT.search(raw)
        value = raw[:comment.start()] if comment else raw
        suffix = value[len(value.rstrip(" \t")):] + (raw[comment.start():] if comment else "")
        replacement = validate_field_value(identity[0], key, value, pending.pop(identity))
        output.append(f"{indent}{key}{separator or chr(9)}{replacement}{suffix}{ending}")
    if section is not None:
        raise ValueError(f"Missing native End({section})")
    if pending:
        section, key, occurrence = next(iter(pending))
        raise ValueError(f"Native setting not found: {section}.{key} occurrence {occurrence}")
    return "".join(output)
