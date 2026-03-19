# Swordfish Theme Reference

Swordfish themes are plain text files with a `.swt` extension. They are embedded at build time and selected with `--theme <name>`.

---

## Format

Each line is a `key = value` pair. Lines beginning with `#` are comments. Whitespace around `=` is ignored.

```
# this is a comment
normal_text = #ebdbb2
normal_bg   = default
```

### Color values

| Value | Meaning |
|---|---|
| `default` | Inherit the terminal's background or foreground |
| `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white` | Standard ANSI colors |
| `#rrggbb` | 24-bit hex color — requires terminal support for `can_change_color` |

---

## TUI Layout

```
┌─────────────────────────────────────────────────────┐
│ Swordfish fuzzy process finder          ← title     │
│   > query text here                     ← query     │
│   Tab: select  Enter: confirm ...        ← dim      │
├─────────────────────────────────────────────────────┤
│  PID    NAME        USER     STATE  RAM  ← header   │
│  1234   firefox     seaslug  S    512 M  ← normal   │
│  5678   systemd     root     S     12 M  ← root     │
│▶ 9012   helix       seaslug  S    128 M  ← highlight│
│  3456   pipewire    seaslug  S     64 M  ← selected │
├─────────────────────────────────────────────────────┤
│  4/312 processes    1 selected           ← status   │
└─────────────────────────────────────────────────────┘
```

---

## Field Reference

### Normal rows

| Field | Applies to |
|---|---|
| `normal_text` | Text on unselected, non-root, non-cursor rows |
| `normal_bg` | Background of unselected rows and the general TUI fill |

`normal_bg` is also used as the background for all column-specific pairs (`pid_text`, `user_text`, `state_text`, `ram_text`) so column colors always sit on a consistent base.

---

### Cursor row

The row the cursor is currently on. Overrides all column colors — everything on this row uses `highlight_text`/`highlight_bg`, including root names.

| Field | Applies to |
|---|---|
| `highlight_text` | Text on the cursor row |
| `highlight_bg` | Background of the cursor row |

---

### Selected rows

Rows toggled with Tab. Overrides all column colors — everything on a selected row uses `selected_text`/`selected_bg`. Root name color does not apply on selected rows.

| Field | Applies to |
|---|---|
| `selected_text` | Text on Tab-selected rows |
| `selected_bg` | Background of Tab-selected rows |

---

### Root process rows

Root-owned processes get a distinct name color as a visual warning. Only applies on normal (non-cursor, non-selected) rows.

| Field | Applies to |
|---|---|
| `root_text` | Name column text for root processes on normal rows |
| `root_bg` | Background behind the name on normal root rows |
| `root_selection_text` | Name column text for root processes on selected rows |
| `root_selection_bg` | Background behind the name on selected root rows |

---

### Column colors

Per-column text colors for normal rows only. The background is always `normal_bg`. These are ignored on cursor and selected rows.

| Field | Column |
|---|---|
| `pid_text` | PID number |
| `user_text` | Owner username |
| `state_text` | Process state character (`S`, `R`, `Z`, etc.) |
| `ram_text` | RAM usage value |

The NAME column does not have its own field — it uses `normal_text` on normal rows, or the root fields when the process is owned by root.

---

### Query bar

The three-line input area at the top of the TUI.

| Field | Applies to |
|---|---|
| `title_text` | The "Swordfish fuzzy process finder" heading |
| `title_bg` | Background behind the title |
| `query_text` | The text you type into the search box |
| `query_bg` | Background of the search input line |
| `dim_text` | The hint line (`Tab: select   Enter: confirm ...`) |
| `dim_bg` | Background of the hint line |

---

### Header bar

The column label row (`PID  NAME  USER  STATE  RAM`).

| Field | Applies to |
|---|---|
| `header_text` | Column label text |
| `header_bg` | Background of the header row |

---

### Status bar

The bottom bar showing process counts and current theme.

| Field | Applies to |
|---|---|
| `status_text` | Text in the status bar |
| `status_bg` | Background of the status bar |

---

### Popups

Both the signal confirmation popup and the theme picker use the same pair.

| Field | Applies to |
|---|---|
| `popup_text` | Text inside popups |
| `popup_bg` | Background of popups |

The dim hint lines inside popups (`[y] Confirm  [n / Esc] Cancel`, `* theme picker is in testing`) use `dim_text`/`dim_bg` rather than `popup_text`/`popup_bg`.

---

## Omitting fields

Any field not present in your theme file inherits from the default theme. You don't need to specify every field — a minimal theme file can override just the colors you care about and leave everything else as the default.
