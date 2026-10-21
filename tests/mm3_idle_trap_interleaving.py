"""Raw-XBE SDK trap reader under controlled FE decoder-register interleavings.

No production fix is applied. The native reader, dispatch, validation and
hardware/client-list unlink and playing-status instructions execute unchanged.
Only IRQL-scope helper calls are stubbed; every case returns from the native
trap handler after its actual FECTL acknowledgements.
"""
import hashlib
import json
import argparse
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/xboxrecomp'))
from tools.rtti.rtti import Image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP

MMIO = 0xFE800000
DEVICE, OWNER_A, OWNER_B, VOICES = 0x710000, 0x720000, 0x721000, 0x730000
STACK, RETURN = 0x700000, 0x600000
IDLE, ACTIVE = 7, 8
FECTL, METH, PARAM, FECV = 0x1100, 0x1300, 0x1304, 0x1110
TRAPPED = 0xFE0


def u32(machine, address):
    return struct.unpack('<I', machine.mem_read(address, 4))[0]


def w32(machine, address, value):
    machine.mem_write(address, struct.pack('<I', value))


def ordinary_current_voice(machine, latch, production_pair=None):
    # Source-checked effects of the production fe_method SET_CURRENT_VOICE:
    # decoder pair is overwritten unless latched; the command still executes.
    if production_pair is not None:
        assert production_pair[0] & 0xE0 == 0xE0
        w32(machine, MMIO + METH, production_pair[1])
        w32(machine, MMIO + PARAM, production_pair[2])
    elif not latch:
        w32(machine, MMIO + METH, 0x2F8)
        w32(machine, MMIO + PARAM, ACTIVE)
    w32(machine, MMIO + FECV, ACTIVE)


def run(image, name, interleave, latch=False, production_pair=None):
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0, 0x800000)
    machine.mem_map(MMIO, 0x10000)
    for va, raw, size, _ in image.secs:
        machine.mem_write(va, bytes(image.d[raw:raw + size]))
    w32(machine, 0x296138, VOICES)
    w32(machine, DEVICE + 0x2BC, 0)
    anchor = DEVICE + 0x6C0
    w32(machine, anchor, OWNER_A + 0x4C)
    w32(machine, anchor + 4, OWNER_B + 0x4C)
    for v, owner, previous, following in [
        (IDLE, OWNER_A, anchor, OWNER_B + 0x4C),
        (ACTIVE, OWNER_B, OWNER_A + 0x4C, anchor),
    ]:
        w32(machine, DEVICE + 0x2C0 + v * 4, owner)
        w32(machine, owner + 8, DEVICE)
        machine.mem_write(owner + 0x64, b'\x01\x00')  # one handle, 2D list
        machine.mem_write(owner + 0xC, struct.pack('<H', v))
        machine.mem_write(owner + 0x12, struct.pack('<H', 0x8001))  # SDK playing
        w32(machine, owner + 0x4C, following)
        w32(machine, owner + 0x50, previous)
        w32(machine, VOICES + v * 0x80 + 4, 0)  # nonpersistent
    w32(machine, VOICES + IDLE * 0x80 + 0x54, 0)
    w32(machine, VOICES + ACTIVE * 0x80 + 0x54, 1 << 21)
    w32(machine, VOICES + IDLE * 0x80 + 0x7C, ACTIVE)
    w32(machine, VOICES + ACTIVE * 0x80 + 0x7C, 0xFFFF)
    for offset, value in [(0x2054, IDLE), (0x2058, IDLE), (0x205C, ACTIVE),
                          (FECTL, TRAPPED), (METH, 0x8000), (PARAM, IDLE)]:
        w32(machine, MMIO + offset, value)
    w32(machine, STACK, RETURN)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.reg_write(UC_X86_REG_ECX, DEVICE)
    evidence = {'case': name, 'interleave': interleave, 'decode_pair_latch_model': latch and production_pair is None,
                'decode_pair_source': 'compiled production publisher' if production_pair is not None else 'source-checked store/latch model',
                'initial_pending_idle': IDLE, 'unlinked_voice': None, 'unlinked_owner': None,
                'active_voice_initially_active': True, 'scope_helpers_stubbed': []}
    executed = False

    def hook(m, address, size, opaque):
        nonlocal executed
        if not executed and interleave == 'between_method_and_parameter' and address == 0x27B713:
            assert m.reg_read(UC_X86_REG_ECX) == 0x8000
            ordinary_current_voice(m, latch, production_pair)
            executed = True
        if address in (0x2764D0, 0x276503):
            evidence['scope_helpers_stubbed'].append(f'{address:08X}')
            sp = m.reg_read(UC_X86_REG_ESP)
            m.reg_write(UC_X86_REG_EIP, u32(m, sp))
            m.reg_write(UC_X86_REG_ESP, sp + 4)
        if address == 0x27CF3F:
            owner = m.reg_read(UC_X86_REG_ECX)
            evidence['unlinked_owner'] = f'{owner:08X}'
            assert u32(m, m.reg_read(UC_X86_REG_ESP) + 4) == 1

    machine.hook_add(UC_HOOK_CODE, hook)
    if interleave == 'before_method_read':
        ordinary_current_voice(machine, latch, production_pair)
    machine.emu_start(0x27B6FB, RETURN, count=2000)
    for v in (IDLE, ACTIVE):
        if u32(machine, VOICES + v * 0x80 + 0x7C) & 0xFFFF == v:
            evidence['unlinked_voice'] = v
    assert machine.reg_read(UC_X86_REG_EIP) == RETURN
    evidence.update({
        'final_pc': f'{machine.reg_read(UC_X86_REG_EIP):08X}',
        'final_fectl': f'{u32(machine, MMIO + FECTL):08X}',
        'final_decoder_method': f'{u32(machine, MMIO + METH):08X}',
        'final_decoder_parameter': u32(machine, MMIO + PARAM),
        'regular_command_executed_fecv': u32(machine, MMIO + FECV),
        'final_head': u32(machine, MMIO + 0x2054),
        'idle_successor': u32(machine, VOICES + IDLE * 0x80 + 0x7C) & 0xFFFF,
        'active_successor': u32(machine, VOICES + ACTIVE * 0x80 + 0x7C) & 0xFFFF,
        'active_bit_after_unlink': bool(u32(machine, VOICES + ACTIVE * 0x80 + 0x54) & (1 << 21)),
        'idle_client_status_word': struct.unpack('<H', machine.mem_read(OWNER_A + 0x12, 2))[0],
        'active_client_status_word': struct.unpack('<H', machine.mem_read(OWNER_B + 0x12, 2))[0],
        'idle_client_unlinked': u32(machine, OWNER_A + 0x4C) == OWNER_A + 0x4C,
        'active_client_unlinked': u32(machine, OWNER_B + 0x4C) == OWNER_B + 0x4C,
        'client_list_first': f'{u32(machine, anchor):08X}',
        'client_list_last': f'{u32(machine, anchor + 4):08X}',
    })
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--production-pairs', type=Path)
    args = parser.parse_args()
    source = (ROOT / 'tools/xboxrecomp/src/apu/apu_vp.c').read_text()
    publisher_path = ROOT / 'tools/xboxrecomp/src/apu/apu_fe_decode.h'
    if publisher_path.exists() and 'apu_fe_publish(' in source:
        publisher = publisher_path.read_text()
        assert '*decode_method = method;' in publisher
        assert '*decode_argument = argument;' in publisher
    else:
        assert 'd->regs[NV_PAPU_FEDECMETH] = method;' in source
        assert 'd->regs[NV_PAPU_FEDECPARAM] = argument;' in source
    assert 'd->regs[NV_PAPU_FECV] = argument;' in source
    image = Image(str(ROOT / 'game_files/default.xbe'))
    production_pair = None
    if args.production_pairs:
        first, second = [struct.unpack('<4I', piece) for piece in
                         [args.production_pairs.read_bytes()[:16], args.production_pairs.read_bytes()[16:]]]
        assert first[:3] == (0xFE0, 0x8000, IDLE)
        assert second == (0xFE0, 0x8000, IDLE, ACTIVE)
        production_pair = second
    rows = [run(image, 'uninterleaved_native_control', 'none'),
            run(image, 'off_mode_overwrite_before_read', 'before_method_read'),
            run(image, 'off_mode_torn_pair_wrong_active_client', 'between_method_and_parameter'),
            run(image, 'latch_preserves_pending_pair', 'between_method_and_parameter', True, production_pair),
            run(image, 'latch_preserves_before_read', 'before_method_read', True, production_pair)]
    assert rows[0]['unlinked_voice'] == IDLE
    assert rows[0]['idle_client_unlinked'] and not rows[0]['active_client_unlinked']
    assert rows[0]['idle_client_status_word'] == 1 and rows[0]['active_client_status_word'] == 0x8001
    assert rows[1]['unlinked_voice'] is None and rows[1]['final_pc'] == f'{RETURN:08X}'
    assert int(rows[1]['final_fectl'], 16) & 0xE0 == 0  # actual SDK FECTL release
    assert rows[2]['unlinked_voice'] == ACTIVE and rows[2]['active_bit_after_unlink']
    assert rows[2]['idle_successor'] == 0xFFFF and rows[2]['final_head'] == IDLE
    assert rows[2]['active_client_unlinked'] and not rows[2]['idle_client_unlinked']
    assert rows[2]['active_client_status_word'] == 1 and rows[2]['idle_client_status_word'] == 0x8001
    for row in rows[3:]:
        assert row['unlinked_voice'] == IDLE and row['regular_command_executed_fecv'] == ACTIVE
        assert row['idle_client_unlinked'] and not row['active_client_unlinked']
        assert row['idle_client_status_word'] == 1 and row['active_client_status_word'] == 0x8001
    result = {
        'status': 'PASS_NATIVE_COUNTEREXAMPLES_REPRODUCED_NO_PRODUCTION_FIX',
        'scope': __doc__, 'cases': rows,
        'production_source_sha256_at_observation': hashlib.sha256((ROOT / 'tools/xboxrecomp/src/apu/apu_vp.c').read_bytes()).hexdigest(),
        'production_helper_pairs_supplied': production_pair is not None,
        'what_is_native': 'Original XBE27B6FB,27B67A,27B208, complete27CF3F hardware-list unlink/client-list removal, actual2764A0 list erase,27C8C5 playing-bit clear and280BAC FECTL mode writes. Only2764D0/276503 scope wrappers are stubbed.',
        'what_is_modeled': 'Unfixed counterexamples use the pre-fix FEDECMETH/PARAM stores and SET_CURRENT_VOICE effect interleaved at a chosen instruction boundary. Fixed cases ingest real compiled production-helper pairs when --production-pairs is supplied; otherwise their latch is only modeled. Neither case implements a full FE FIFO.',
        'ack_lifetime': 'SDK ISR27B818 acknowledges ISTS before reading the trap, so ISTS ack cannot safely release a pending decoder latch. After reading/handling the pair,27B6FB calls280BAC(0) then(1), changing FECTL mode TRAPPED->HALTED->FREE. A pending pair must remain stable through its reads and may retire when mode first leaves TRAPPED.',
        'limits': 'Controlled legal source-level interleavings demonstrate lost idle event and unlink of an ACTIVE unrelated client. They do not prove these interleavings caused the captured long fade. Hardware FE FIFO consumption remains outside this fixture.',
        'pinned_xemu_reference': 'https://raw.githubusercontent.com/xemu-project/xemu/478b4f496102379c7eaa7f3ec10e714a703c4300/hw/xbox/mcpx/apu/vp/vp.c',
        'upstream_limit': 'Pinned vp_write directly dispatches with a TODO to queue FE commands; its fe_method FREE_RUNNING assert is commented out. This matches a known model gap, not independent hardware conformance.',
    }
    output = ROOT / 'logs/dc-smoothness-pass3-20261019/audio-idle-trap-interleaving.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: raw SDK lost-idle and wrong-ACTIVE-client unlink counterexamples, native control and ' +
          ('production-backed latch pairs' if production_pair is not None else 'decoder-only latch model'))
    print(output.relative_to(ROOT))


if __name__ == '__main__':
    main()
