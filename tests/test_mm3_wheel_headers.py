"""Wheel-reference opacity repair preserves meshes, shadows and lamp effects."""
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/car-models"))
import mm3_model
import repair_wheel_headers


class WheelHeaderRepairTest(unittest.TestCase):
    def test_available_packages_match_opaque_donor_wheels_and_reapply_exactly(self):
        # Packages recreated from an install (rebuild_packages_from_install.py)
        # carry no conversion report, which this repair reads.
        folders = list((ROOT / "out/car-models/packages").glob("*/conversion-report.json"))
        if not folders:
            self.skipTest("Converted imported cars are not available")
        for path in folders:
            with self.subTest(car=path.parent.name):
                updates, count = repair_wheel_headers.prepare(path.parent, ROOT / "game_files/Data")
                self.assertEqual(count, 0)
                for _, before, after in updates[:2]:
                    self.assertEqual(before, after)

    def test_translucent_left_wheel_header_is_the_only_bytes_repaired(self):
        folder = ROOT / "out/car-models/packages/Panoz"
        if not (folder / "package.json").is_file():
            self.skipTest("Panoz package is not available")
        package = json.loads((folder / "package.json").read_text())
        item = next(item for item in package["files"]
                    if item["path"] == "Data/Shared/Cars/Panoz/body.cmp")
        valid = (folder / item["source"]).read_bytes()
        donor = (ROOT / "game_files/Data/Shared/Cars/Viper/body.cmp").read_bytes()
        model = mm3_model.read_model(valid)
        for path, node in model.root.walk():
            if "/tireFrontLeft/" in path and node.meshes:
                block = node.meshes[0]
                data = bytearray(block.payload)
                struct.pack_into("<4f", data, 48, .3, .4, .5, .02)
                node.meshes[0] = mm3_model.MeshBlock(block.name, bytes(data))
                break
        else:
            self.fail("Missing left wheel")
        damaged = model.write()
        self.assertNotEqual(damaged, valid)
        repaired, report = repair_wheel_headers.repair(damaged, donor)
        self.assertEqual(repaired, valid)
        self.assertEqual(len(report), 1)
        self.assertEqual(report[0]["to"], [1, 1, 1, 1])
        self.assertEqual(repair_wheel_headers.repair(repaired, donor), (repaired, []))


if __name__ == "__main__":
    unittest.main()
