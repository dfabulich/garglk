// Copyright (C) 2026 by Dan Fabulich.
//
// Internal Glk map-document extension (gestalt_Map).

#ifndef GARGLK_MAP_H
#define GARGLK_MAP_H

#include <cstdint>
#include <string>
#include <vector>

#include "glk.h"

extern bool gli_map_event_request;

struct map_focus_rect_t {
    int left = 0;
    int top = 0;
    unsigned width = 0;
    unsigned height = 0;
};

struct map_hyperlink_ui_t {
    glui32 id = 0;
    std::string label;
    std::vector<std::pair<glsi32, glsi32>> points;
};

struct map_overlay_ui_t {
    overlayid_t overlay_id = 0;
    glui32 image_id = 0;
    glui32 color = 0;
    glsi32 left = 0;
    glsi32 top = 0;
    glui32 width = 0;
    glui32 height = 0;
    glui32 zindex = 0;
    glui32 link_id = 0;
    std::string link_label;
    bool is_fill = false;
};

void gli_map_post_event(glui32 subtype, glui32 payload);
void gli_map_ui_shutdown();

void gli_map_ui_present_svg(const unsigned char *data, glui32 len,
    glui32 flags, glui32 bgcolor, const map_focus_rect_t *focus,
    const std::vector<map_hyperlink_ui_t> &hyperlinks);

void gli_map_ui_present_image(glui32 image, glui32 flags, glui32 bgcolor,
    const map_focus_rect_t *focus, const std::vector<map_hyperlink_ui_t> &hyperlinks);

void gli_map_ui_set_hyperlinks(const std::vector<map_hyperlink_ui_t> &hyperlinks);
void gli_map_ui_overlay(const map_overlay_ui_t &overlay);
void gli_map_ui_overlay_move(overlayid_t overlay, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex);
void gli_map_ui_overlay_clear(overlayid_t overlay);
void gli_map_ui_overlay_clear_all();
void gli_map_ui_close();
void gli_map_ui_set_focus(const map_focus_rect_t *focus);
void gli_map_ui_clear_focus();
void gli_map_ui_set_event_request(bool enabled);

#endif
