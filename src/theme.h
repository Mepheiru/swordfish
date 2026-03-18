#pragma once

#include <stddef.h>

typedef struct {
    short normal_fg,    normal_bg;
    short highlight_fg, highlight_bg;
    short selected_fg,  selected_bg;
    short query_fg,     query_bg;
    short header_fg,    header_bg;
    short status_fg,    status_bg;
    short root_fg,      root_bg;
    short dim_fg,       dim_bg;
} sw_theme_t;

void theme_load(const char *name, sw_theme_t *out);
void theme_init_custom_colors(void);
int theme_count(void);

const char *theme_name_at(int i);
