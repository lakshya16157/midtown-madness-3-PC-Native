"""Read-only, searchable native car specifications for the car editor.

The widget intentionally knows nothing about manifests or the editor.  Cars use
``sections = [{"name": str, "entries": [(key, raw_value), ...]}, ...]`` so repeated
native settings retain their original order and their own comparison values.
"""

from __future__ import annotations

import json
import tkinter as tk
from collections import defaultdict
from collections.abc import Callable, Iterable, Mapping
from tkinter import ttk


def _value_text(value: object) -> str:
    """Keep extracted native strings intact; also accept effective Python values."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, (list, tuple, dict)):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def _entries(car: dict):
    """Yield every setting with a stable identity, including duplicate keys."""
    occurrences = defaultdict(int)
    sections = car.get("sections", [])
    if not sections:
        sections = [
            {"name": "Ratings", "entries": list(car.get("ratings", {}).items())},
            {"name": "Simulation", "entries": list(car.get("physics", {}).items())},
        ]
    for section in sections:
        section_name = str(section.get("name", ""))
        entries = section.get("entries", [])
        if isinstance(entries, Mapping):
            entries = entries.items()
        for key, value in entries:
            key = str(key)
            identity = (section_name, key, occurrences[section_name, key])
            occurrences[section_name, key] += 1
            yield identity, section_name, key, _value_text(value)


class StatsBrowser(ttk.Frame):
    """Display a car's complete native settings and compare an original car.

    ``tip_for(section, key)`` supplies the explanation shown when a row is
    selected.  ``show_car(car)`` refreshes the selected car; ``set_original_cars``
    refreshes available references without replacing it.  Both an iterable of
    car dictionaries and a dictionary of car dictionaries are accepted.
    """

    def __init__(self, parent, original_cars: Iterable[dict] | Mapping,
                 tip_for: Callable[[str, str], str], show_heading: bool = True,
                 reference_var: tk.StringVar | None = None, show_reference_picker: bool = True,
                 range_for: Callable[[str, str], str] | None = None, **kwargs):
        """``reference_var`` lets a host window share its own compare picker;
        ``show_reference_picker=False`` hides this widget's picker (it keeps
        working for programmatic use). ``range_for(section, key)`` may add a
        game-wide range sentence to the setting explanation. Reference cars
        with ``reference_group`` 0 (added cars) are listed first and never
        become the default comparison."""
        super().__init__(parent, **kwargs)
        self.tip_for = tip_for
        self.range_for = range_for
        self.car: dict | None = None
        self.original_cars: list[dict] = []
        self._references: dict[str, dict] = {}
        self._rows: list[dict] = []
        self._visible_rows: dict[str, dict] = {}
        self._reference_initialized = False
        self._updating_references = False

        self.columnconfigure(0, weight=1)
        self.rowconfigure(3, weight=1)

        heading = ttk.Frame(self)
        heading.grid(row=0, column=0, sticky="ew", pady=(0, 12))
        heading.columnconfigure(0, weight=1)
        self.title_var = tk.StringVar(value="Full car specification")
        ttk.Label(heading, textvariable=self.title_var,
                  font=("Segoe UI", 15, "bold")).grid(row=0, column=0, sticky="w")
        self.subtitle_var = tk.StringVar(value="Select a car to inspect its original settings.")
        ttk.Label(heading, textvariable=self.subtitle_var,
                  style="Sub.TLabel").grid(row=1, column=0, sticky="w", pady=(4, 0))
        if not show_heading:
            heading.grid_remove()

        controls = ttk.Frame(self)
        controls.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        controls.columnconfigure(0, weight=1)
        controls.columnconfigure(2, weight=1)
        ttk.Label(controls, text="Search setting, section or value").grid(
            row=0, column=0, columnspan=2, sticky="w", pady=(0, 4))
        self.reference_label = ttk.Label(controls, text="Compare with car")
        self.reference_label.grid(row=0, column=2, sticky="w", padx=(16, 0), pady=(0, 4))
        self.search_var = tk.StringVar()
        self.search_entry = ttk.Entry(controls, textvariable=self.search_var)
        self.search_entry.grid(row=1, column=0, sticky="ew")
        self.clear_button = ttk.Button(controls, text="Clear", command=self.clear_search)
        self.clear_button.grid(row=1, column=1, padx=(6, 0))
        self.reference_var = reference_var if reference_var is not None else tk.StringVar(value="None")
        self.reference_combo = ttk.Combobox(
            controls, textvariable=self.reference_var, state="readonly", width=29)
        self.reference_combo.grid(row=1, column=2, sticky="ew", padx=(16, 0))
        self.reference_combo.bind("<<ComboboxSelected>>", self._render)
        if not show_reference_picker:
            self.reference_label.grid_remove()
            self.reference_combo.grid_remove()
            controls.columnconfigure(2, weight=0)
        self.reference_var.trace_add("write", self._reference_written)
        self.search_var.trace_add("write", self._render)
        self.search_entry.bind("<Escape>", self._clear_event)

        status = ttk.Frame(self)
        status.grid(row=2, column=0, sticky="ew", pady=(0, 6))
        status.columnconfigure(0, weight=1)
        self.count_var = tk.StringVar(value="0 settings")
        self.count_label = ttk.Label(status, textvariable=self.count_var, style="Sub.TLabel")
        self.count_label.grid(
            row=0, column=0, sticky="w")
        self.copy_button = ttk.Button(
            status, text="Copy selected value", command=self.copy_selected_value,
            state="disabled")
        self.copy_button.grid(row=0, column=1, sticky="e")

        table = ttk.Frame(self)
        table.grid(row=3, column=0, sticky="nsew")
        table.rowconfigure(0, weight=1)
        table.columnconfigure(0, weight=1)
        self.tree = ttk.Treeview(
            table, columns=("section", "setting", "value", "reference"),
            show="headings", selectmode="browse", height=15)
        headings = (
            ("section", "Section", 150, 110, False),
            ("setting", "Setting (native key)", 245, 180, True),
            ("value", "Value", 210, 150, True),
            ("reference", "Reference value", 210, 150, True),
        )
        for name, label, width, minimum, stretch in headings:
            self.tree.heading(name, text=label, anchor="w")
            self.tree.column(name, width=width, minwidth=minimum,
                             stretch=stretch, anchor="w")
        vertical = ttk.Scrollbar(table, orient="vertical", command=self.tree.yview)
        horizontal = ttk.Scrollbar(table, orient="horizontal", command=self.tree.xview)
        self.tree.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        vertical.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="ew")
        self.tree.tag_configure("even", background="#ffffff")
        self.tree.tag_configure("odd", background="#f4f7fb")
        self.tree.bind("<<TreeviewSelect>>", self._selection_changed)
        self.tree.bind("<Control-c>", self.copy_selected_value)
        self.tree.bind("<Control-C>", self.copy_selected_value)
        for widget in (self.tree, self.search_entry, self.reference_combo):
            widget.bind("<Control-f>", self._focus_search)

        detail = ttk.LabelFrame(self, text="About this setting", padding=(12, 9))
        detail.grid(row=4, column=0, sticky="ew", pady=(12, 0))
        detail.columnconfigure(0, weight=1)
        self.detail_title_var = tk.StringVar(value="Select a setting for a helpful tip")
        ttk.Label(detail, textvariable=self.detail_title_var,
                  font=("Segoe UI", 10, "bold")).grid(row=0, column=0, sticky="w")
        self.tip_var = tk.StringVar(
            value="Use the arrow keys to browse settings. Ctrl+C copies the selected value.")
        self.tip_label = ttk.Label(
            detail, textvariable=self.tip_var, style="Sub.TLabel",
            wraplength=700, justify="left")
        self.tip_label.grid(row=1, column=0, sticky="ew", pady=(5, 0))
        detail.bind("<Configure>", self._resize_tip)

        self.source_var = tk.StringVar(value="")
        self.source_label = ttk.Label(self, textvariable=self.source_var, style="Hint.TLabel", wraplength=700)
        self.source_label.grid(
            row=5, column=0, sticky="w", pady=(8, 0))
        self.bind("<Configure>", lambda e: self.source_label.configure(wraplength=max(240, e.width - 20)))
        status.bind("<Configure>", lambda e: self.count_label.configure(wraplength=max(140, e.width - self.copy_button.winfo_reqwidth() - 20)))
        self.set_original_cars(original_cars)

    def set_original_cars(self, cars: Iterable[dict] | Mapping | None) -> None:
        """Update references, preserving the chosen car whenever possible."""
        previous = self._references.get(self.reference_var.get())
        previous_id = self._car_id(previous) if previous else None
        candidates = cars.values() if isinstance(cars, Mapping) else (cars or [])
        self.original_cars = [car for car in candidates if isinstance(car, dict)]
        self._references = {}
        preferred = "None"
        for car in self.original_cars:
            name = str(car.get("display_name") or car.get("alias") or car.get("id") or "Original car")
            label = name
            suffix = 2
            while label == "None" or label in self._references:
                label = f"{name} ({suffix})"
                suffix += 1
            self._references[label] = car
            if previous_id and self._car_id(car) == previous_id:
                preferred = label
        if not self._reference_initialized:
            for label, car in self._references.items():
                if self._group(car) == 0:
                    continue
                identity = " ".join(str(car.get(key, "")) for key in ("id", "alias", "display_name")).lower()
                if "lotus" in identity or "esprit" in identity:
                    preferred = label
                    break
        self._reference_initialized = self._reference_initialized or bool(self._references)
        labels = sorted(self._references,
                        key=lambda label: (self._group(self._references[label]), label.casefold()))
        self.reference_combo.configure(values=["None", *labels],
                                       state="readonly" if labels else "disabled")
        self._updating_references = True
        try:
            self.reference_var.set(preferred)
        finally:
            self._updating_references = False
        self._render()

    @staticmethod
    def _group(car: dict) -> int:
        group = car.get("reference_group", 1)
        return group if isinstance(group, int) else 1

    def reference_values(self) -> list[str]:
        """Compare choices in display order, starting with "None"."""
        return [str(value) for value in self.reference_combo.cget("values")]

    def reference_car(self, label: str | None = None) -> dict | None:
        """The compared car for ``label`` (default: the current choice)."""
        return self._references.get(self.reference_var.get() if label is None else label)

    def refresh(self) -> None:
        """Re-render after the shown car or the reference data changed."""
        self._render()

    def _reference_written(self, *_args) -> None:
        if not self._updating_references:
            self._render()

    @staticmethod
    def _car_id(car: dict) -> str:
        return str(car.get("id") or car.get("alias") or car.get("display_name") or "")

    def show_car(self, car: dict | None) -> None:
        """Render every native section entry, retaining repeated native keys."""
        self.car = car
        self._rows = []
        if car:
            name = str(car.get("display_name") or car.get("alias") or car.get("id") or "Car")
            self.title_var.set(name)
            alias = str(car.get("alias") or car.get("id") or "")
            subtitle = "Full specification · read-only values"
            if alias and alias != name:
                subtitle += f" · {alias}"
            self.subtitle_var.set(subtitle)
            for index, (identity, section, key, value) in enumerate(_entries(car)):
                self._rows.append({"iid": f"stat-{index}", "identity": identity,
                                   "section": section, "key": key, "value": value})
            source = str(car.get("source") or "")
            self.source_var.set(f"Source: {source.replace(chr(92), '/').rsplit('/', 1)[-1]}" if source else "")
        else:
            self.title_var.set("Full car specification")
            self.subtitle_var.set("Select a car to inspect its original settings.")
            self.source_var.set("")
        self._render()

    def clear_search(self) -> None:
        self.search_var.set("")
        self.search_entry.focus_set()

    def _clear_event(self, _event=None):
        self.clear_search()
        return "break"

    def _focus_search(self, _event=None):
        self.search_entry.focus_set()
        self.search_entry.selection_range(0, "end")
        return "break"

    def _render(self, *_args) -> None:
        if not hasattr(self, "tree"):
            return
        selected = self.tree.selection()
        selected_id = selected[0] if selected else None
        reference = self._references.get(self.reference_var.get())
        self.tree.heading("reference", text=f"Compare: {self.reference_var.get()}" if reference else "Reference value")
        comparison = {identity: value for identity, _section, _key, value in _entries(reference)} if reference else {}
        tokens = self.search_var.get().casefold().split()
        children = self.tree.get_children()
        if children:
            self.tree.delete(*children)
        self._visible_rows = {}
        for row in self._rows:
            other = comparison.get(row["identity"], "—") if reference else ""
            text = " ".join((row["section"], row["key"], row["value"], other)).casefold()
            if not all(token in text for token in tokens):
                continue
            tag = "even" if len(self._visible_rows) % 2 == 0 else "odd"
            self.tree.insert("", "end", iid=row["iid"],
                             values=(row["section"] or "General", row["key"], row["value"], other),
                             tags=(tag,))
            self._visible_rows[row["iid"]] = row
        visible, total = len(self._visible_rows), len(self._rows)
        count = f"{total:,} setting{'s' if total != 1 else ''}"
        if tokens:
            count = f"{visible:,} of {count}"
        if reference:
            count += " · — means this setting is absent from the reference"
        self.count_var.set(count)
        self.clear_button.configure(state="normal" if self.search_var.get() else "disabled")
        if selected_id in self._visible_rows:
            self.tree.selection_set(selected_id)
            self.tree.focus(selected_id)
        elif self._visible_rows:
            first = next(iter(self._visible_rows))
            self.tree.selection_set(first)
            self.tree.focus(first)
        self._selection_changed()

    def _selection_changed(self, _event=None) -> None:
        selected = self.tree.selection()
        row = self._visible_rows.get(selected[0]) if selected else None
        self.copy_button.configure(state="normal" if row else "disabled")
        if not row:
            self.detail_title_var.set("No matching settings" if self._rows else "Select a setting for a helpful tip")
            self.tip_var.set("Try a shorter search or clear the filter." if self._rows
                             else "Use the arrow keys to browse settings. Ctrl+C copies the selected value.")
            return
        self.detail_title_var.set(f"{row['key']} · {row['section'] or 'General'}")
        tip = self.tip_for(row["section"], row["key"])
        text = str(tip) if tip else "Native car setting. Compare its value with an original car before changing it."
        if self.range_for is not None:
            extra = self.range_for(row["section"], row["key"])
            if extra:
                text += "\n" + str(extra)
        self.tip_var.set(text)

    def copy_selected_value(self, _event=None):
        selected = self.tree.selection()
        row = self._visible_rows.get(selected[0]) if selected else None
        if row:
            self.clipboard_clear()
            self.clipboard_append(row["value"])
        return "break"

    def _resize_tip(self, event) -> None:
        self.tip_label.configure(wraplength=max(180, event.width - 28))
