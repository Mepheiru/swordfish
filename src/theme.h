#pragma once

#include <stddef.h>

typedef struct {
    short normal_text, normal_bg;
    short highlight_text, highlight_bg;
    short selected_text, selected_bg;
    short query_text, query_bg;
    short header_text, header_bg;
    short status_text, status_bg;
    short root_text, root_bg;
    short root_selection_text, root_selection_bg;
    short dim_text, dim_bg;
    short title_text, title_bg;
    short popup_text, popup_bg;
    short dim_popup_text, dim_popup_bg;
    short pid_text;
    short pid_selected_text;
    short pid_highlight_text;
    short user_text;
    short user_selected_text;
    short user_highlight_text;
    short state_text;
    short state_selected_text;
    short state_highlight_text;
    short ram_text;
    short ram_selected_text;
    short ram_highlight_text;
} sw_theme_t;

void theme_load(const char *name, sw_theme_t *out);
void theme_init_custom_colors(void);
int theme_count(void);
const char *theme_name_at(int i);
