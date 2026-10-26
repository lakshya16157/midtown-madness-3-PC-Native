"""Voice bank fixes for the retail disc's language mix-ups.

The Spanish Paris voice bank (Data/Shared/Audio/Voice/Spanish/Paris.xwb)
holds the Italian recordings of the Paris police's 42 lines
(audio\\voice\\paris\\police\\dur_copfirst_00..41): the Spanish release was
never dubbed for them, so Paris's police spoke Italian. They are French
police, so the corrected copy uses the French recordings (same ADPCM format).

The original bank is never changed: the copy is written beside it as
Paris_mm3.xwb and src/mm3_language.c opens it in its place.
"""
import struct

ALIGN_DEFAULT = 0x800
POLICE = "audio\\voice\\paris\\police\\dur_copfirst_"


def _names(xcp):
    """{entry index: cue file name} from a cue table (.xcp)."""
    count = struct.unpack_from("<I", xcp, 8)[0]
    pos, out = 12, {}
    for _ in range(count):
        length = struct.unpack_from("<I", xcp, pos)[0]
        name = xcp[pos + 4:pos + 4 + length].decode("latin1")
        out[struct.unpack_from("<I", xcp, pos + 4 + length)[0]] = name
        pos += 8 + length
    return out


class Bank:
    """An XWB version 3 wave bank: four (offset, length) segments, then the
    bank header; 24-byte entries (flags | duration, format, offset, length,
    loop start, loop length), wave data offsets relative to segment 4."""

    def __init__(self, data):
        if data[:4] != b"WBND" or struct.unpack_from("<I", data, 4)[0] != 3:
            raise ValueError("not an XWB version 3 wave bank")
        self.data = data
        self.segments = struct.unpack_from("<8I", data, 8)
        self.count = struct.unpack_from("<I", data, 0x2C)[0]
        self.stride = struct.unpack_from("<I", data, 0x40)[0] or 0x18
        self.align = struct.unpack_from("<I", data, 0x48)[0] or ALIGN_DEFAULT
        if self.stride < 0x18 or self.segments[2] + self.count * self.stride > len(data):
            raise ValueError("wave bank entry table out of range")

    def entry(self, index):
        return list(struct.unpack_from("<6I", self.data, self.segments[2] + index * self.stride))

    def wave(self, index):
        _, _, offset, length, _, _ = self.entry(index)
        start = self.segments[6] + offset
        return self.data[start:start + length]


def spanish_paris_police(spanish_xwb, spanish_xcp, french_xwb, french_xcp):
    """The Spanish Paris bank with the police lines from the French one, or
    None when the banks are not the retail layout this expects."""
    spanish, french = Bank(spanish_xwb), Bank(french_xwb)
    es_names, fr_names = _names(spanish_xcp), _names(french_xcp)
    fr_index = {name: index for index, name in fr_names.items()}
    swaps = []
    for index, name in sorted(es_names.items()):
        if name.startswith(POLICE) and name in fr_index and index < spanish.count:
            es_entry, fr_entry = spanish.entry(index), french.entry(fr_index[name])
            if es_entry[1] != fr_entry[1]:          # the same audio format only
                return None
            swaps.append((index, fr_entry, french.wave(fr_index[name])))
    if len(swaps) != 42:
        return None
    data_offset, data_length = spanish.segments[6], spanish.segments[7]
    if data_offset + data_length != len(spanish_xwb):      # wave data must end the file
        return None
    out = bytearray(spanish_xwb)
    end = data_length
    for index, fr_entry, wave in swaps:
        end = (end + spanish.align - 1) // spanish.align * spanish.align
        out += bytes(data_offset + end - len(out))
        entry = [fr_entry[0], fr_entry[1], end, len(wave), fr_entry[4], fr_entry[5]]
        struct.pack_into("<6I", out, spanish.segments[2] + index * spanish.stride, *entry)
        out += wave
        end += len(wave)
    end = (end + spanish.align - 1) // spanish.align * spanish.align
    out += bytes(data_offset + end - len(out))
    struct.pack_into("<I", out, 0x24, end)                  # segment 4 length
    return bytes(out)
