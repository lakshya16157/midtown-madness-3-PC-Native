"""Hidden-Tk checks for browsing complete car specs without changing car data."""

import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

try:
    import tkinter as tk
except ImportError:
    tk = None


ROOT = Path(__file__).resolve().parents[1]
stats_browser = None
if tk is not None:
    SPEC = importlib.util.spec_from_file_location(
        "mm3_stats_browser_test", ROOT / "tools/car-mods/stats_browser.py")
    stats_browser = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(stats_browser)


def car_fixtures():
    lotus = {
        "id": 17, "alias": "Lotus", "display_name": "Lotus Esprit V8",
        "source": "fixture/Lotus.car",
        "sections": [
            {"name": "Simulation", "entries": [
                ("TorqueScale", "210"), ("TorqueKnot", "0.10 100"),
                ("TorqueKnot", "0.20 200"), ("OnlyLotus", "original") ]},
            {"name": "Tyres", "entries": [("WheelRadius", "0.32")]},
            {"name": "Simulation", "entries": [("TorqueKnot", "0.30 300")]},
        ],
    }
    added = {
        "id": 9011, "alias": "Added", "display_name": "Added test car",
        "source": "fixture/Added.car",
        "sections": [
            {"name": "Simulation", "entries": [
                ("TorqueScale", "650"), ("TorqueKnot", "0.10 400"),
                ("TorqueKnot", "0.20 800"), ("AddedOnly", "custom") ]},
            {"name": "Tyres", "entries": [("WheelRadius", "0.35")]},
            {"name": "Simulation", "entries": [
                ("TorqueKnot", "0.30 900"), ("TorqueKnot", "0.40 950")]},
        ],
    }
    other = {
        "id": 2, "alias": "Puma", "display_name": "Ford Puma",
        "sections": [{"name": "Simulation", "entries": [("TorqueScale", "120")]}],
    }
    return added, lotus, other


class CarStatsBrowserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if tk is None:
            raise unittest.SkipTest("Tkinter is not installed")
        try:
            cls.root = tk.Tk()
        except tk.TclError as error:
            raise unittest.SkipTest(f"Tk display is unavailable: {error}")
        cls.root.withdraw()

    @classmethod
    def tearDownClass(cls):
        cls.root.destroy()

    def setUp(self):
        self.added, self.lotus, self.other = car_fixtures()
        self.originals = [self.other, self.lotus]
        self.widget = stats_browser.StatsBrowser(
            self.root, self.originals,
            lambda section, key: f"Help for {section}.{key}")
        self.widget.pack(fill="both", expand=True)
        self.widget.show_car(self.added)
        self.root.update()

    def tearDown(self):
        self.widget.destroy()
        self.root.update_idletasks()

    def rows(self):
        return [self.widget.tree.item(item, "values")
                for item in self.widget.tree.get_children()]

    def test_default_reference_is_lotus_even_when_another_car_is_first(self):
        self.assertEqual(self.widget.reference_var.get(), "Lotus Esprit V8")
        self.assertEqual(self.rows()[0], ("Simulation", "TorqueScale", "650", "210"))

    def test_repeated_keys_match_reference_occurrences_across_repeated_sections(self):
        rows = self.rows()
        self.assertEqual(len(rows), 7)
        curves = [row for row in rows if row[1] == "TorqueKnot"]
        self.assertEqual(curves, [
            ("Simulation", "TorqueKnot", "0.10 400", "0.10 100"),
            ("Simulation", "TorqueKnot", "0.20 800", "0.20 200"),
            ("Simulation", "TorqueKnot", "0.30 900", "0.30 300"),
            ("Simulation", "TorqueKnot", "0.40 950", "—"),
        ])

    def test_absent_reference_fields_are_marked_and_do_not_add_reference_rows(self):
        self.assertIn(("Simulation", "AddedOnly", "custom", "—"), self.rows())
        self.assertFalse(any(row[1] == "OnlyLotus" for row in self.rows()))
        self.assertIn("absent from the reference", self.widget.count_var.get())

    def test_search_combines_section_key_and_value_and_clear_restores_rows(self):
        self.widget.search_var.set("simulation TORQUESCALE 650")
        self.assertEqual(self.rows(), [("Simulation", "TorqueScale", "650", "210")])
        self.assertTrue(self.widget.count_var.get().startswith("1 of 7 settings"))
        self.widget.search_var.set("no matching field")
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.widget.detail_title_var.get(), "No matching settings")
        self.assertTrue(self.widget.copy_button.instate(["disabled"]))
        self.widget.clear_search()
        self.assertEqual(len(self.rows()), 7)

    def test_selected_duplicate_shows_help_and_copies_its_own_raw_value(self):
        items = self.widget.tree.get_children()
        self.widget.tree.selection_set(items[2])
        self.root.update()
        self.assertEqual(self.widget.tip_var.get(), "Help for Simulation.TorqueKnot")
        self.assertTrue(self.widget.copy_button.instate(["!disabled"]))
        # Verify the selected value without replacing the user's OS clipboard.
        with patch.object(self.widget, "clipboard_clear") as clear, \
                patch.object(self.widget, "clipboard_append") as append:
            self.widget.copy_button.invoke()
        clear.assert_called_once_with()
        append.assert_called_once_with("0.20 800")

    def test_browsing_comparison_filtering_and_copy_do_not_mutate_car_data(self):
        before = copy.deepcopy((self.added, self.originals))
        self.widget.search_var.set("TorqueKnot")
        self.widget.reference_var.set("Ford Puma")
        self.widget.reference_combo.event_generate("<<ComboboxSelected>>")
        self.root.update()
        with patch.object(self.widget, "clipboard_clear"), \
                patch.object(self.widget, "clipboard_append"):
            self.widget.copy_selected_value()
        self.widget.clear_search()
        self.widget.show_car(self.lotus)
        self.widget.set_original_cars(self.originals)
        self.assertEqual((self.added, self.originals), before)

    def test_missing_references_and_no_selected_car_are_safe(self):
        self.widget.set_original_cars(None)
        self.assertEqual(self.widget.reference_var.get(), "None")
        self.assertTrue(self.widget.reference_combo.instate(["disabled"]))
        self.assertEqual(len(self.rows()), 7)
        self.assertTrue(all(row[3] == "" for row in self.rows()))
        self.widget.show_car(None)
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.widget.count_var.get(), "0 settings")
        self.assertTrue(self.widget.copy_button.instate(["disabled"]))

    def test_external_reference_variable_is_shared_and_hidden_picker_still_works(self):
        shared = tk.StringVar(master=self.root)
        widget = stats_browser.StatsBrowser(self.root, self.originals, lambda section, key: key,
                                            reference_var=shared, show_reference_picker=False)
        self.addCleanup(widget.destroy)
        widget.show_car(self.added)
        self.assertIs(widget.reference_var, shared)
        self.assertEqual(shared.get(), "Lotus Esprit V8")
        self.assertEqual(widget.reference_combo.winfo_manager(), "")
        self.assertEqual(widget.reference_values(), ["None", "Ford Puma", "Lotus Esprit V8"])
        shared.set("Ford Puma")  # The shared variable alone re-renders the table.
        first = widget.tree.get_children()[0]
        self.assertEqual(widget.tree.item(first, "values")[3], "120")
        self.assertIs(widget.reference_car(), self.other)
        self.assertEqual(widget.tree.heading("reference", "text"), "Compare: Ford Puma")
        shared.set("None")
        self.assertEqual(widget.tree.heading("reference", "text"), "Reference value")

    def test_added_group_sorts_first_and_is_never_the_default(self):
        added_lotus = {**self.lotus, "id": 9006, "display_name": "Lotus Special (added)", "reference_group": 0}
        widget = stats_browser.StatsBrowser(self.root, [self.other, added_lotus, self.lotus],
                                            lambda section, key: key)
        self.addCleanup(widget.destroy)
        self.assertEqual(widget.reference_values(),
                         ["None", "Lotus Special (added)", "Ford Puma", "Lotus Esprit V8"])
        self.assertEqual(widget.reference_var.get(), "Lotus Esprit V8")

    def test_detail_panel_adds_range_text_from_callback(self):
        widget = stats_browser.StatsBrowser(self.root, self.originals, lambda section, key: f"Help {key}",
                                            range_for=lambda section, key: f"Range of {section}.{key}")
        self.addCleanup(widget.destroy)
        widget.show_car(self.added)
        self.assertEqual(widget.tip_var.get(), "Help TorqueScale\nRange of Simulation.TorqueScale")

    def test_references_loaded_after_empty_start_still_default_to_lotus(self):
        widget = stats_browser.StatsBrowser(self.root, [], lambda section, key: key)
        self.addCleanup(widget.destroy)
        widget.show_car(self.added)
        widget.set_original_cars({"Puma": self.other, "Lotus": self.lotus})
        self.assertEqual(widget.reference_var.get(), "Lotus Esprit V8")
        first = widget.tree.get_children()[0]
        self.assertEqual(widget.tree.item(first, "values")[3], "210")


if __name__ == "__main__":
    unittest.main()
