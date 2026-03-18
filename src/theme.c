#include "theme.h"
#include <curses.h>

#include <string.h>
#include <stddef.h>
#include <stdlib.h>

// theme data
extern const char _binary_themes_fihsy_swt_start[];
extern const char _binary_themes_fihsy_swt_end[];
extern const char _binary_themes_nord_swt_start[];
extern const char _binary_themes_nord_swt_end[];
extern const char _binary_themes_gruvbox_swt_start[];
extern const char _binary_themes_gruvbox_swt_end[];

typedef struct {
    const char *name;
    const char *start;
    const char *end;
} theme_entry_t;

static const theme_entry_t theme_table[] = {
    { "default", _binary_themes_fihsy_swt_start,   _binary_themes_fihsy_swt_end   },
    { "nord",    _binary_themes_nord_swt_start,     _binary_themes_nord_swt_end    },
    { "gruvbox", _binary_themes_gruvbox_swt_start,  _binary_themes_gruvbox_swt_end },
    { NULL, NULL, NULL }
};

// custom color slots for hex values allocated from slot 16 upward
#define MAX_CUSTOM_COLORS 64
static int custom_color_count = 0;
static struct { short idx; short r, g, b; } custom_colors[MAX_CUSTOM_COLORS];

/* Parses a #rrggbb hex string, allocates a color slot, and returns its index.
   Returns -1 if the terminal doesn't support color changing or input is invalid. */
static short alloc_hex_color(const char *hex) {
    if (hex[0] != '#' || strlen(hex) != 7) return -1;

    char rs[3] = { hex[1], hex[2], '\0' };
    char gs[3] = { hex[3], hex[4], '\0' };
    char bs[3] = { hex[5], hex[6], '\0' };

    char *end;
    int r = (int)strtol(rs, &end, 16); if (*end) return -1;
    int g = (int)strtol(gs, &end, 16); if (*end) return -1;
    int b = (int)strtol(bs, &end, 16); if (*end) return -1;

    if (custom_color_count >= MAX_CUSTOM_COLORS) return -1;

    // ncurses color values are 0-1000
    short slot = (short)(16 + custom_color_count);
    custom_colors[custom_color_count++] = (typeof(custom_colors[0])){
        .idx = slot,
        .r   = (short)(r * 1000 / 255),
        .g   = (short)(g * 1000 / 255),
        .b   = (short)(b * 1000 / 255),
    };
    return slot;
}

static short parse_color(const char *val) {
    if (strcmp(val, "black")   == 0) return COLOR_BLACK;
    if (strcmp(val, "red")     == 0) return COLOR_RED;
    if (strcmp(val, "green")   == 0) return COLOR_GREEN;
    if (strcmp(val, "yellow")  == 0) return COLOR_YELLOW;
    if (strcmp(val, "blue")    == 0) return COLOR_BLUE;
    if (strcmp(val, "magenta") == 0) return COLOR_MAGENTA;
    if (strcmp(val, "cyan")    == 0) return COLOR_CYAN;
    if (strcmp(val, "white")   == 0) return COLOR_WHITE;
    if (val[0] == '#')               return alloc_hex_color(val);
    return -1; // "default" or unknown — use terminal default
}

/* Parses a .swt buffer into out, filling only fields present in the file.
   Caller should pre-fill out with defaults before calling. */
static void theme_parse(const char *data, size_t len, sw_theme_t *out) {
    const char *p   = data;
    const char *end = data + len;

    while (p < end) {
        const char *line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        while (p < line_end && (*p == ' ' || *p == '\t')) p++;

        if (p >= line_end || *p == '#') {
            p = line_end + 1;
            continue;
        }

        const char *eq = p;
        while (eq < line_end && *eq != '=') eq++;
        if (eq >= line_end) { p = line_end + 1; continue; }

        const char *key_end = eq - 1;
        while (key_end > p && (*key_end == ' ' || *key_end == '\t')) key_end--;
        size_t key_len = (size_t)(key_end - p + 1);

        char key[32] = {0};
        if (key_len >= sizeof(key)) { p = line_end + 1; continue; }
        memcpy(key, p, key_len);

        const char *val_start = eq + 1;
        while (val_start < line_end && (*val_start == ' ' || *val_start == '\t')) val_start++;
        const char *val_end = line_end - 1;
        while (val_end > val_start && (*val_end == ' ' || *val_end == '\t' || *val_end == '\r')) val_end--;

        char val[32] = {0};
        size_t val_len = (size_t)(val_end - val_start + 1);
        if (val_len >= sizeof(val)) { p = line_end + 1; continue; }
        memcpy(val, val_start, val_len);

        short color = parse_color(val);

        if      (strcmp(key, "normal_fg")    == 0) out->normal_fg    = color;
        else if (strcmp(key, "normal_bg")    == 0) out->normal_bg    = color;
        else if (strcmp(key, "highlight_fg") == 0) out->highlight_fg = color;
        else if (strcmp(key, "highlight_bg") == 0) out->highlight_bg = color;
        else if (strcmp(key, "selected_fg")  == 0) out->selected_fg  = color;
        else if (strcmp(key, "selected_bg")  == 0) out->selected_bg  = color;
        else if (strcmp(key, "query_fg")     == 0) out->query_fg     = color;
        else if (strcmp(key, "query_bg")     == 0) out->query_bg     = color;
        else if (strcmp(key, "header_fg")    == 0) out->header_fg    = color;
        else if (strcmp(key, "header_bg")    == 0) out->header_bg    = color;
        else if (strcmp(key, "status_fg")    == 0) out->status_fg    = color;
        else if (strcmp(key, "status_bg")    == 0) out->status_bg    = color;
        else if (strcmp(key, "root_fg")      == 0) out->root_fg      = color;
        else if (strcmp(key, "root_bg")      == 0) out->root_bg      = color;
        else if (strcmp(key, "dim_fg")       == 0) out->dim_fg       = color;
        else if (strcmp(key, "dim_bg")       == 0) out->dim_bg       = color;
        else if (strcmp(key, "title_fg")     == 0) out->title_fg     = color;
        else if (strcmp(key, "title_bg")     == 0) out->title_bg     = color;
        else if (strcmp(key, "popup_fg")     == 0) out->popup_fg     = color;
        else if (strcmp(key, "popup_bg")     == 0) out->popup_bg     = color;

        p = line_end + 1;
    }
}

void theme_load(const char *name, sw_theme_t *out) {
    // reset custom color allocator so re-loads don't leak slots
    custom_color_count = 0;

    const theme_entry_t *def = &theme_table[0];
    theme_parse(def->start, (size_t)(def->end - def->start), out);

    if (!name || strcmp(name, "default") == 0)
        return;

    for (int i = 1; theme_table[i].name; i++) {
        if (strcmp(theme_table[i].name, name) == 0) {
            theme_parse(theme_table[i].start,
                        (size_t)(theme_table[i].end - theme_table[i].start),
                        out);
            return;
        }
    }

    // unknown theme name — default is already loaded, silently continue
}

void theme_init_custom_colors(void) {
    if (!can_change_color()) return;
    for (int i = 0; i < custom_color_count; i++)
        init_color(custom_colors[i].idx,
                   custom_colors[i].r,
                   custom_colors[i].g,
                   custom_colors[i].b);
}

int theme_count(void) {
    int n = 0;
    while (theme_table[n].name) n++;
    return n;
}

const char *theme_name_at(int i) {
    int n = 0;
    while (theme_table[n].name) n++;
    if (i < 0 || i >= n) return NULL;
    return theme_table[i].name;
}
