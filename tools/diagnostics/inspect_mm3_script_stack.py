"""Decode MM3's captured mission command queue without accessing a live game.

Usage: python tools/diagnostics/inspect_mm3_script_stack.py CAPTURE_DIRECTORY
The retail European title's static script object is at guest 0x407D20.
Queue layout is established by sub_0007E97C/sub_0001D7C5/sub_001FF4F3;
type 3 is the stackWait command inserted by sub_001FAA50.
"""
import argparse
import json
from pathlib import Path
import struct


def inspect(memory):
    def read(address, size):
        if not 0 <= address <= len(memory) - size:
            raise ValueError(f'Address outside captured guest memory: {address:#x}+{size:#x}')
        return memory[address:address + size]

    def u32(address):
        return int.from_bytes(read(address, 4), 'little')

    mission = 0x407D20
    if u32(mission) != 0x383FD4:
        raise ValueError('Capture does not match the retail MM3 script object layout')
    queue = mission + 0x17C
    table, capacity, first, count = (u32(queue + i) for i in (4, 8, 12, 16))
    if not 0 < capacity <= 4096 or first >= capacity or count > capacity:
        raise ValueError('Invalid mission queue bounds')
    tasks = []
    for index in range(count):
        address = u32(table + ((first + index) % capacity) * 4)
        length, string_capacity = u32(address + 0x18), u32(address + 0x1C)
        if length > string_capacity or string_capacity > 1024 * 1024:
            raise ValueError(f'Invalid task string bounds at {address:#x}')
        string_address = address + 8 if string_capacity < 16 else u32(address + 8)
        command = read(string_address, length).decode('utf8', errors='replace')
        kind = u32(address + 0x20)
        tasks.append({'index': index, 'address': hex(address), 'type': kind,
                      'kind': {1: 'command', 3: 'stackWait'}.get(kind, f'type_{kind}'),
                      'delay_or_value': struct.unpack('<f', read(address, 4))[0],
                      'command': command})
    continued = read(mission + 0x1DD, 1)[0]
    return {'mission': hex(mission), 'queue_table': hex(table),
            'queue_capacity': capacity, 'queue_first': first, 'queue_count': count,
            'stack_continue_address': hex(mission + 0x1DD),
            'stack_continue': continued,
            'blocked_on_stack_wait': bool(tasks and tasks[0]['type'] == 3 and not continued),
            'movement_status': hex(u32(0x40807C)),
            'tasks': tasks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    result = inspect((args.capture / 'guest-main-ram.bin').read_bytes())
    destination = args.capture / 'script-stack.json'
    destination.write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf8')
    print(f"queue={result['queue_count']} continuation={result['stack_continue']} "
          f"blocked_on_stack_wait={result['blocked_on_stack_wait']}")
    for task in result['tasks']:
        print(f"{task['index']:2} {task['kind']}: {task['command']}")
    print(destination)


if __name__ == '__main__':
    main()
