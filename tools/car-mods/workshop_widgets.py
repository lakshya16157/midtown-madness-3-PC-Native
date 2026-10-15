"""Tk building blocks for the Car Workshop.

The widgets know nothing about manifests or game data: the editor supplies
text and values. ``ToolTip`` and ``ScrollableFrame`` moved here from editor.py,
which keeps aliases for compatibility.
"""
from __future__ import annotations

import tkinter as tk
from tkinter import ttk


class ToolTip:
    """Keyboard-focus help plus a small, delayed hover explanation.

    ``text`` may be a string or a callable returning the current text, so a
    tooltip can describe live comparison values.
    """
    def __init__(self, widget, text, on_focus=None):
        self.widget, self.text, self.window, self.timer = widget, text, None, None
        widget.bind("<Enter>", self.schedule, add="+")
        widget.bind("<Leave>", self.hide, add="+")
        widget.bind("<ButtonPress>", self.hide, add="+")
        widget.bind("<Destroy>", self.hide, add="+")
        if on_focus:
            widget.bind("<FocusIn>", lambda _event: on_focus(self.current_text()), add="+")

    def current_text(self) -> str:
        return str(self.text() if callable(self.text) else self.text)

    def schedule(self, _event=None):
        self.hide()
        self.timer = self.widget.after(450, self.show)

    def show(self):
        self.timer = None
        text = self.current_text()
        if not text:
            return
        self.window = tk.Toplevel(self.widget)
        self.window.overrideredirect(True)
        x = min(self.widget.winfo_rootx() + 12, self.widget.winfo_screenwidth() - 430)
        y = min(self.widget.winfo_rooty() + self.widget.winfo_height() + 5,
                self.widget.winfo_screenheight() - 150)
        self.window.geometry(f"+{max(0, x)}+{max(0, y)}")
        tk.Label(self.window, text=text, wraplength=390, justify="left", padx=12, pady=10,
                 background="#172638", foreground="white", font=("Segoe UI", 10)).pack()

    def hide(self, _event=None):
        if self.timer is not None:
            self.widget.after_cancel(self.timer)
            self.timer = None
        if self.window is not None:
            self.window.destroy()
            self.window = None


class ScrollableFrame(ttk.Frame):
    """Resizable form whose fields remain reachable on smaller screens.

    The mouse wheel scrolls the page. Value widgets registered with
    ``capture_wheel`` scroll the page too, instead of silently changing their
    value while the user scrolls past them.
    """
    def __init__(self, parent):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, background="#f3f5f8", highlightthickness=0)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        scrollbar.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.content = ttk.Frame(self.canvas, padding=(14, 12))
        self.item = self.canvas.create_window((0, 0), window=self.content, anchor="nw")
        self._wheel_rest = 0.0
        self.content.bind("<Configure>", lambda _e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(self.item, width=e.width))
        self.canvas.bind("<Configure>", self._resize_labels, add="+")
        self.canvas.bind_all("<MouseWheel>", self._wheel, add="+")

    def _resize_labels(self, event):
        def resize(parent):
            for widget in parent.winfo_children():
                if isinstance(widget, ttk.Label) and widget.cget("wraplength"):
                    if not hasattr(widget, "preferred_wrap"):
                        widget.preferred_wrap = float(widget.cget("wraplength"))
                    widget.configure(wraplength=max(180, min(widget.preferred_wrap, event.width - 45)))
                resize(widget)
        resize(self.content)

    def scroll(self, delta: int) -> None:
        # Touchpads send small deltas; keep the remainder instead of losing it.
        self._wheel_rest -= delta / 120
        steps = int(self._wheel_rest)
        if steps:
            self._wheel_rest -= steps
            self.canvas.yview_scroll(steps, "units")

    def _wheel(self, event):
        widget = event.widget
        while widget is not None:
            if widget is self or widget is self.canvas:
                self.scroll(event.delta)
                return "break"
            widget = getattr(widget, "master", None)

    def _captured_wheel(self, event):
        self.scroll(event.delta)
        return "break"

    def capture_wheel(self, widget) -> None:
        """Make the wheel over ``widget`` scroll this form, never change its value."""
        widget.bind("<MouseWheel>", self._captured_wheel)

    def capture_value_widgets(self) -> None:
        """Apply ``capture_wheel`` to every spinbox and combobox in the form."""
        def visit(parent):
            for widget in parent.winfo_children():
                if isinstance(widget, (ttk.Spinbox, ttk.Combobox, tk.Spinbox)):
                    self.capture_wheel(widget)
                visit(widget)
        visit(self.content)

    def reveal(self, widget) -> None:
        self.update_idletasks()
        total = max(1, self.content.winfo_height())
        top = self.canvas.canvasy(0)
        y = widget.winfo_rooty() - self.content.winfo_rooty()
        bottom = y + widget.winfo_height() + 12
        if y < top:
            self.canvas.yview_moveto(max(0, y - 12) / total)
        elif bottom > top + self.canvas.winfo_height():
            self.canvas.yview_moveto(max(0, bottom - self.canvas.winfo_height()) / total)


def block_wheel(widget) -> None:
    """Stop the wheel from changing a combobox or spinbox outside a scroll form."""
    widget.bind("<MouseWheel>", lambda _event: "break")


# Explicit fonts: the editor sets a "*Font" option-database default, which
# ttk labels apply ahead of any font configured in their style.
FONT = ("Segoe UI", 10)
BOLD = ("Segoe UI", 10, "bold")
HEADING_FONT = ("Segoe UI", 9, "bold")


def _focused(widget) -> bool:
    try:
        return widget.focus_get() is widget
    except (KeyError, tk.TclError):
        # focus_get cannot name Tk-internal windows such as combobox popdowns.
        return False


class Placeholder:
    """Grey text over an empty entry, such as the value a blank field inherits.

    Hidden while the entry has text or focus, or is read-only, so it never
    covers the caret. Clicking it focuses the entry.
    """
    def __init__(self, entry, variable: tk.StringVar, text: str = ""):
        self.entry, self.variable, self.text = entry, variable, text
        self.label = tk.Label(entry.master, text=text, foreground="#8995a5", background="white",
                              font=FONT, borderwidth=0, padx=0, pady=0, cursor="xterm")
        self.label.bind("<Button-1>", lambda _event: entry.focus_set())
        variable.trace_add("write", lambda *_args: self.update())
        entry.bind("<FocusIn>", lambda _event: self.update(), add="+")
        entry.bind("<FocusOut>", lambda _event: self.update(), add="+")
        self.update()

    @property
    def visible(self) -> bool:
        return bool(self.label.winfo_manager())

    def set_text(self, text: str) -> None:
        self.text = text
        self.label.configure(text=text)
        self.update()

    def update(self) -> None:
        readonly = "readonly" in self.entry.state() or "disabled" in self.entry.state()
        if self.text and not self.variable.get() and not readonly and not _focused(self.entry):
            self.label.place(in_=self.entry, x=5, rely=0.5, anchor="w")
            self.label.lift()
        else:
            self.label.place_forget()


class SegmentBar(tk.Canvas):
    """Clickable menu-rating bar: the value is the number of lit segments.

    Clicking the highest lit segment steps down by one, which reaches zero.
    Small ticks under the bar mark the base and compare cars' ratings.
    """
    def __init__(self, parent, variable: tk.StringVar, maximum: int = 9, segment: int = 9, gap: int = 2):
        self.variable, self.maximum, self.pitch = variable, maximum, segment + gap
        super().__init__(parent, width=maximum * self.pitch, height=20, highlightthickness=0,
                         background="#f3f5f8", takefocus=0, cursor="hand2")
        self.segments = [self.create_rectangle(index * self.pitch, 1, index * self.pitch + segment, 13,
                                               outline="", fill="#d5dde8") for index in range(maximum)]
        self.ticks = {name: self.create_line(0, 0, 0, 0, fill=color, width=2)
                      for name, color in (("base", "#28384b"), ("compare", "#c2620f"))}
        self.markers = {"base": None, "compare": None}
        self.bind("<Button-1>", lambda event: self.select(min(self.maximum, max(1, event.x // self.pitch + 1))))
        variable.trace_add("write", lambda *_args: self.redraw())
        self.redraw()

    def value(self):
        try:
            return max(0, min(self.maximum, int(self.variable.get())))
        except ValueError:
            return None

    def select(self, segment: int) -> None:
        self.variable.set(str(segment - 1 if self.value() == segment else segment))

    def set_markers(self, base=None, compare=None) -> None:
        self.markers = {"base": base, "compare": compare}
        self.redraw()

    def redraw(self) -> None:
        value = self.value() or 0
        for index, item in enumerate(self.segments):
            self.itemconfigure(item, fill="#2066ba" if index < value else "#d5dde8")
        for offset, (name, rating) in enumerate(self.markers.items()):
            if isinstance(rating, int) and 1 <= rating <= self.maximum:
                x = (rating - 1) * self.pitch + 2 + offset * 4
                self.coords(self.ticks[name], x, 15, x, 20)
            else:
                self.coords(self.ticks[name], 0, 0, 0, 0)


def _ratio_text(value) -> str:
    return format(value, "g") if isinstance(value, (int, float)) else str(value)


class GearGrid(ttk.Frame):
    """Reverse plus up to seven forward gears, kept in sync with a text variable.

    ``gear_var`` holds "-3.3, 2.9, 1.8" style text, and blank means inherit the
    base gearbox. The first edit while inheriting fills the other gears from
    the base, so changing one ratio never drops the rest.
    """
    MAX_FORWARD = 7

    def __init__(self, parent, gear_var: tk.StringVar, tokens, text_of):
        super().__init__(parent)
        self.gear_var, self.tokens, self.text_of = gear_var, tokens, text_of
        self.cells = [tk.StringVar(master=self) for _ in range(self.MAX_FORWARD + 1)]
        self.base: list = []
        self.count = 5
        self._syncing = False
        for column, text in enumerate(["", "R", *map(str, range(1, self.MAX_FORWARD + 1))]):
            ttk.Label(self, text=text, font=HEADING_FONT, style="Column.TLabel").grid(
                row=0, column=column, sticky="w", padx=(0, 6))
        ttk.Label(self, text="Yours", font=FONT).grid(row=1, column=0, sticky="w", padx=(0, 10))
        self.entries = []
        for index, variable in enumerate(self.cells):
            entry = ttk.Entry(self, textvariable=variable, width=6)
            entry.grid(row=1, column=index + 1, sticky="w", padx=(0, 6), pady=2)
            self.entries.append(entry)
            variable.trace_add("write", lambda *_args, index=index: self._cell_changed(index))
        self.reference_labels = {}
        for row, (name, style) in enumerate((("Base", "Value.TLabel"), ("Compare", "Compare.TLabel"),
                                             ("Game max", "Hint.TLabel")), 2):
            ttk.Label(self, text=name, font=FONT).grid(row=row, column=0, sticky="w", padx=(0, 10))
            labels = []
            for index in range(len(self.cells)):
                label = ttk.Label(self, style=style)
                label.grid(row=row, column=index + 1, sticky="w", padx=(0, 6))
                labels.append(label)
            self.reference_labels[name] = labels
        buttons = ttk.Frame(self)
        buttons.grid(row=5, column=0, columnspan=len(self.cells) + 1, sticky="w", pady=(8, 0))
        self.add_button = ttk.Button(buttons, text="+ Gear", style="Small.TButton", command=self.add_gear)
        self.remove_button = ttk.Button(buttons, text="- Gear", style="Small.TButton", command=self.remove_gear)
        self.start_button = ttk.Button(buttons, text="Start from base", style="Small.TButton", command=self.start_from_base)
        self.clear_button = ttk.Button(buttons, text="Use base gearbox", style="Small.TButton", command=self.clear)
        for button in (self.add_button, self.remove_button, self.start_button, self.clear_button):
            button.pack(side="left", padx=(0, 6))
        gear_var.trace_add("write", lambda *_args: self._text_changed())
        self._text_changed()

    def _set_cells(self, values) -> None:
        self._syncing = True
        try:
            for index, variable in enumerate(self.cells):
                variable.set(values[index] if index < len(values) else "")
        finally:
            self._syncing = False

    def _publish(self, values) -> None:
        self._syncing = True
        try:
            self.gear_var.set(self.text_of(values[:self.count + 1]))
        finally:
            self._syncing = False

    def _show_count(self) -> None:
        for index, entry in enumerate(self.entries):
            if index <= self.count:
                entry.grid()
            else:
                entry.grid_remove()
        self.add_button.state(["!disabled"] if self.count < self.MAX_FORWARD else ["disabled"])
        self.remove_button.state(["!disabled"] if self.count > 1 else ["disabled"])

    def _text_changed(self) -> None:
        if self._syncing:
            return
        tokens = self.tokens(self.gear_var.get())
        self._set_cells(tokens)
        self.count = (max(1, min(self.MAX_FORWARD, len(tokens) - 1)) if tokens
                      else max(1, len(self.base) - 1) if self.base else 5)
        self._show_count()

    def inheriting(self) -> bool:
        return not self.gear_var.get().strip()

    def _base_cells(self) -> list:
        return [_ratio_text(value) for value in self.base]

    def _cell_changed(self, index: int) -> None:
        if self._syncing:
            return
        values = [variable.get().strip() for variable in self.cells]
        if self.inheriting() and self.base:
            typed = values[index]
            values = self._base_cells() + [""] * (len(self.cells) - len(self.base))
            values[index] = typed
            self._set_cells(values)
        self._publish(values)

    def start_from_base(self) -> None:
        if self.base:
            self.count = max(1, len(self.base) - 1)
            self.gear_var.set(self.text_of(self._base_cells()))

    def clear(self) -> None:
        self.gear_var.set("")

    def add_gear(self) -> None:
        if self.inheriting():
            self.start_from_base()
        if self.count >= self.MAX_FORWARD:
            return
        values = [variable.get().strip() for variable in self.cells]
        try:
            last = float(values[self.count])
        except ValueError:
            last = 1.0
        values[self.count + 1] = _ratio_text(max(0.1, round(last * 0.85, 2)))
        self.count += 1
        self._set_cells(values)
        self._publish(values)
        self._show_count()

    def remove_gear(self) -> None:
        if self.inheriting():
            self.start_from_base()
        if self.count <= 1:
            return
        values = [variable.get().strip() for variable in self.cells]
        values[self.count] = ""
        self.count -= 1
        self._set_cells(values)
        self._publish(values)
        self._show_count()

    def set_reference(self, name: str, ratios) -> None:
        """Fill the Base, Compare or Game max row; Base also sets the inherit count."""
        ratios = list(ratios or [])
        for index, label in enumerate(self.reference_labels[name]):
            label.configure(text=_ratio_text(ratios[index]) if index < len(ratios) and ratios[index] is not None else "")
        if name == "Base":
            self.base = [ratio for ratio in ratios if isinstance(ratio, (int, float))]
            if self.inheriting():
                self.count = max(1, len(self.base) - 1) if self.base else 5
                self._show_count()


class SettingTable(ttk.Frame):
    """Aligned rows: Setting | Your value | Base | Compare | Max allowed | Game max.

    Fixed minimum column widths keep separate tables on one page aligned.
    """
    COLUMNS = ("setting", "value", "base", "compare", "allowed", "game")
    MIN_WIDTHS = (150, 140, 95, 95, 58)

    def __init__(self, parent, headings: dict, **kwargs):
        super().__init__(parent, **kwargs)
        self.heading_labels = {}
        for column, key in enumerate(self.COLUMNS):
            label = ttk.Label(self, text=headings.get(key, ""), style="Column.TLabel", justify="left",
                              font=HEADING_FONT)
            label.grid(row=0, column=column, sticky="sw", padx=(0, 12), pady=(0, 3))
            self.heading_labels[key] = label
        ttk.Separator(self).grid(row=1, column=0, columnspan=len(self.COLUMNS), sticky="ew", pady=(0, 3))
        for column, width in enumerate(self.MIN_WIDTHS):
            self.columnconfigure(column, minsize=width + 12)
        self.columnconfigure(len(self.COLUMNS) - 1, weight=1)
        self.rows: dict = {}
        self._next_row = 2

    def set_heading(self, key: str, text: str) -> None:
        self.heading_labels[key].configure(text=text)

    def add_row(self, key: str, text: str, make_field, on_compare_click=None, on_reset=None,
                make_extra=None) -> "SettingRow":
        """``make_field(parent)`` builds the editing widget in the row's value cell.

        ``on_reset(key)`` adds a reset button; ``make_extra(parent)`` adds a
        widget such as a rating bar beside the field.
        """
        row = SettingRow(self, self._next_row, key, text, make_field, on_compare_click, on_reset, make_extra)
        self.rows[key] = row
        self._next_row += 1
        return row


class SettingRow:
    """One table row; the editor fills its reference cells."""
    def __init__(self, table: SettingTable, row: int, key: str, text: str, make_field,
                 on_compare_click=None, on_reset=None, make_extra=None):
        self.key = key
        self.overridden = False
        self.problem = ""
        self.label = ttk.Label(table, text=text, style="Setting.TLabel", font=FONT)
        self.label.grid(row=row, column=0, sticky="w", padx=(0, 12), pady=2)
        self.cell = ttk.Frame(table)
        self.cell.grid(row=row, column=1, sticky="w", padx=(0, 12), pady=2)
        self.field = make_field(self.cell)
        self.field.pack(side="left")
        self.reset_button = None
        if on_reset is not None:
            self.reset_button = ttk.Button(self.cell, text="↺", width=2, style="Small.TButton", takefocus=0,
                                           command=lambda: on_reset(key))
            self.reset_button.pack(side="left", padx=(4, 0))
            self.reset_button.state(["disabled"])
            ToolTip(self.reset_button, "Clear only this override and use the Base value.")
        self.extra = make_extra(self.cell) if make_extra is not None else None
        if self.extra is not None:
            self.extra.pack(side="left", padx=(8, 0))
        self.base_label = ttk.Label(table, style="Value.TLabel")
        self.compare_label = ttk.Label(table, style="Compare.TLabel")
        self.allowed_label = ttk.Label(table, style="Hint.TLabel")
        self.game_label = ttk.Label(table, style="Hint.TLabel")
        cells = (self.base_label, self.compare_label, self.allowed_label, self.game_label)
        for column, widget in enumerate(cells, 2):
            widget.grid(row=row, column=column, sticky="w", padx=(0, 12), pady=2)
        if on_compare_click is not None:
            self.compare_label.configure(cursor="hand2")
            self.compare_label.bind("<Button-1>", lambda _event: on_compare_click(key))

    def set_cells(self, base: str | None = None, compare: str | None = None,
                  allowed: str | None = None, game: str | None = None) -> None:
        for widget, text in ((self.base_label, base), (self.compare_label, compare),
                             (self.allowed_label, allowed), (self.game_label, game)):
            if text is not None:
                widget.configure(text=text)

    def set_overridden(self, overridden: bool) -> None:
        self.overridden = overridden
        self.label.configure(style="Overridden.TLabel" if overridden else "Setting.TLabel",
                             font=BOLD if overridden else FONT)
        if self.reset_button is not None:
            self.reset_button.state(["!disabled"] if overridden else ["disabled"])

    def set_problem(self, message: str) -> None:
        """Mark the field invalid (red) with ``message``, or clear it."""
        self.problem = message or ""
        try:
            self.field.state(["invalid"] if self.problem else ["!invalid"])
        except tk.TclError:
            pass

    def cell_text(self, name: str) -> str:
        return str(getattr(self, f"{name}_label").cget("text"))
