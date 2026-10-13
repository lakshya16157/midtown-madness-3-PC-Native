"""Read/write the reference graph in MM3's retail MemeFile 2.0 resources.

The string dictionary is byte-length-prefixed and zero-terminated. After it,
the root has a 16-bit class ID. References in class payloads have an inclusive
32-bit byte span, a 16-bit name ID and a 16-bit class ID. Class zero denotes an
existing named object (or null when its name is also zero). References contain
their object's payload; scalar fields stay byte-for-byte intact.

Only the known UI classes used here are interpreted. Other class payloads are
preserved, and scalar leaf classes are never searched for nested references.
This lets unchanged retail graphs round-trip without reimplementing the title's
font, animation, event or numeric serialization.
"""
from __future__ import annotations

from dataclasses import dataclass
import copy
import struct

PREFIX = "dice::meme::"
LEAF_CLASSES = {"WSidData", "WstringData", "IntData", "FloatData", "BoolData",
                "Function", "SoundAction", "ButtonEvent", "TypeEvent",
                "MmxStyle", "MmxCapitalizedStyle", "MmxWordWrappingStyle", "Style", "RightAlignedStyle",
                "CenterAlignedStyle", "MmxProfileInfoListBoxData"}


@dataclass
class Object:
    kind: str
    parts: list[bytes | "Reference"]

    def encode(self, resource: "Resource") -> bytes:
        return b"".join(part if isinstance(part, bytes) else part.encode(resource)
                        for part in self.parts)

    def references(self):
        return [part for part in self.parts if isinstance(part, Reference)]

    def walk(self):
        yield self
        for ref in self.references():
            if ref.object:
                yield from ref.object.walk()


@dataclass
class Reference:
    name: str = ""
    object: Object | None = None

    def encode(self, resource: "Resource") -> bytes:
        payload = self.object.encode(resource) if self.object else b""
        return struct.pack("<IHH", 8 + len(payload), resource.intern(self.name),
                           resource.intern(PREFIX + self.object.kind) if self.object else 0) + payload


class Resource:
    def __init__(self, strings: list[str], root: Object):
        self.strings = strings
        self.root = root

    def intern(self, value: str):
        if not value:
            return 0
        if value not in self.strings:
            encoded = value.encode("ascii")
            if not 1 <= len(encoded) <= 255:
                raise ValueError("Meme dictionary string is too long")
            self.strings.append(value)
        return self.strings.index(value)

    def encode(self):
        # Encoding the graph interns new names/classes before the table is emitted.
        kind = self.intern(PREFIX + self.root.kind)
        payload = self.root.encode(self)
        table = b"".join(bytes([len(s.encode("ascii"))]) + s.encode("ascii")
                         for s in self.strings)
        return table + b"\0" + struct.pack("<H", kind) + payload

    @classmethod
    def decode(cls, data: bytes):
        strings = []
        pos = 0
        while pos < len(data) and data[pos]:
            length = data[pos]
            if pos + 1 + length > len(data):
                raise ValueError("Truncated Meme dictionary")
            strings.append(data[pos + 1:pos + 1 + length].decode("ascii"))
            pos += length + 1
        if not strings or strings[0] != "MemeFile 2.0" or pos + 3 > len(data):
            raise ValueError("Not a MemeFile 2.0 resource")
        pos += 1
        root_type, = struct.unpack_from("<H", data, pos)

        def decode_object(type_id, start, end):
            if not 0 < type_id < len(strings) or not strings[type_id].startswith(PREFIX):
                raise ValueError("Invalid Meme object class")
            kind = strings[type_id][len(PREFIX):]
            if kind in LEAF_CLASSES:
                return Object(kind, [data[start:end]])
            parts = []
            cursor = raw_start = start
            while cursor + 8 <= end:
                span, name, child_type = struct.unpack_from("<IHH", data, cursor)
                valid = (8 <= span <= end - cursor and name < len(strings) and
                         (name == 0 or not strings[name].startswith(PREFIX)) and
                         (child_type == 0 and span == 8 or
                          0 < child_type < len(strings) and strings[child_type].startswith(PREFIX)))
                if not valid:
                    cursor += 1
                    continue
                if raw_start < cursor:
                    parts.append(data[raw_start:cursor])
                obj = decode_object(child_type, cursor + 8, cursor + span) if child_type else None
                parts.append(Reference(strings[name] if name else "", obj))
                cursor += span
                raw_start = cursor
            if raw_start < end:
                parts.append(data[raw_start:end])
            return Object(kind, parts)

        result = cls(strings, decode_object(root_type, pos + 2, len(data)))
        if result.encode() != data:
            raise ValueError("Meme graph did not round-trip")
        return result


def clone(value):
    return copy.deepcopy(value)


def sid(value):
    encoded = value.encode("ascii")
    return Object("WSidData", [struct.pack("<I", len(encoded)) + encoded])


def sid_text(obj):
    if obj.kind != "WSidData" or len(obj.parts) != 1 or not isinstance(obj.parts[0], bytes):
        raise ValueError("Expected a WSidData leaf")
    raw = obj.parts[0]
    length, = struct.unpack_from("<I", raw)
    if len(raw) != length + 4:
        raise ValueError("Invalid WSidData length")
    return raw[4:].decode("ascii")


def call_function(name):
    return Reference(object=Object("CallFunctionAction", [
        Reference("functions/" + name, Object("Function", []))]))


def actions(items):
    return Reference(object=Object("ActionListAction", [*items, Reference()]))


def row_button(transform):
    """The retail row is Transform(next, rectangle, Tabstop(SimpleButton))."""
    if transform.kind != "TransformNode" or len(transform.parts) != 3:
        return None
    next_ref, rect, child = transform.parts
    if not isinstance(next_ref, Reference) or not isinstance(rect, bytes) or len(rect) != 16:
        return None
    if not isinstance(child, Reference) or not child.object or child.object.kind != "TabstopNode":
        return None
    refs = child.object.references()
    if len(refs) != 1 or not refs[0].object or refs[0].object.kind != "MmxSimpleButtonNode":
        return None
    button = refs[0].object
    if len(button.parts) != 5 or not all(isinstance(p, Reference) for p in button.parts):
        raise ValueError("Retail button schema changed")
    return button


def find_row(resource, label_name):
    matches = [obj for obj in resource.root.walk()
               if (button := row_button(obj)) and button.parts[1].name == label_name]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one retail row {label_name}: {len(matches)}")
    return matches[0]


def named_references(resource, name):
    return [ref for obj in resource.root.walk() for ref in obj.references() if ref.name == name]


def rows(resource):
    result = []
    for obj in resource.root.walk():
        if button := row_button(obj):
            label = button.parts[1]
            text = sid_text(label.object) if label.object and label.object.kind == "WSidData" else label.name
            result.append((text, struct.unpack("<4f", obj.parts[1]), obj))
    return result
