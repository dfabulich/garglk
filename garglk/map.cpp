// Copyright (C) 2026 by Dan Fabulich.
//
// Map document extension (gestalt_Map). SVG/image present + overlays.

#include <cstring>
#include <vector>

#include "glk.h"
#include "garglk.h"
#include "map.h"

bool gli_map_event_request = false;

namespace {

constexpr glui32 MAP_MAX_OVERLAYS = 64;
constexpr glui32 MAP_MAX_HYPERLINKS = 64;
constexpr glui32 MAP_MAX_POINTS_PER_LINK = 32;

struct map_overlay_slot_t {
    bool used = false;
    overlayid_t id = 0;
};

map_overlay_slot_t map_overlays[MAP_MAX_OVERLAYS];
overlayid_t map_next_overlay_id = 1;

bool svg_looks_valid(const unsigned char *data, glui32 len)
{
    if (data == nullptr || len < 4) {
        return false;
    }
    for (glui32 i = 0; i + 4 <= len; i++) {
        if (data[i] == '<' &&
            (data[i + 1] == 's' || data[i + 1] == 'S') &&
            (data[i + 2] == 'v' || data[i + 2] == 'V') &&
            (data[i + 3] == 'g' || data[i + 3] == 'G')) {
            return true;
        }
    }
    return false;
}

bool map_hyperlink_is_valid(const glk_maphyperlink_t *h)
{
    if (h == nullptr) {
        return false;
    }
    if (h->id == 0) {
        return false;
    }
    if (h->npoints < 3 || h->npoints > MAP_MAX_POINTS_PER_LINK) {
        return false;
    }
    return h->points != nullptr;
}

std::vector<map_hyperlink_ui_t> map_filter_hyperlinks(const glk_maphyperlink_t *hyperlinks,
    glui32 nhyperlinks)
{
    std::vector<map_hyperlink_ui_t> out;
    if (hyperlinks == nullptr) {
        return out;
    }
    if (nhyperlinks > MAP_MAX_HYPERLINKS) {
        nhyperlinks = MAP_MAX_HYPERLINKS;
    }
    for (glui32 i = 0; i < nhyperlinks; i++) {
        if (!map_hyperlink_is_valid(&hyperlinks[i])) {
            continue;
        }
        map_hyperlink_ui_t link;
        link.id = hyperlinks[i].id;
        if (hyperlinks[i].label != nullptr) {
            link.label = hyperlinks[i].label;
        }
        link.points.reserve(hyperlinks[i].npoints);
        for (glui32 p = 0; p < hyperlinks[i].npoints; p++) {
            link.points.emplace_back(hyperlinks[i].points[p].x, hyperlinks[i].points[p].y);
        }
        out.push_back(std::move(link));
    }
    return out;
}

map_focus_rect_t map_focus_from_args(glsi32 focusleft, glsi32 focustop,
    glui32 focuswidth, glui32 focusheight, glui32 flags)
{
    map_focus_rect_t focus;
    if ((flags & mapflag_HasFocus) != 0) {
        focus.left = focusleft;
        focus.top = focustop;
        focus.width = focuswidth;
        focus.height = focusheight;
    }
    return focus;
}

void map_clear_overlays_local()
{
    for (glui32 i = 0; i < MAP_MAX_OVERLAYS; i++) {
        map_overlays[i].used = false;
        map_overlays[i].id = 0;
    }
}

void map_notify_clear_overlays()
{
    map_clear_overlays_local();
    gli_map_ui_overlay_clear_all();
}

overlayid_t map_alloc_overlay_id()
{
    for (glui32 i = 0; i < MAP_MAX_OVERLAYS; i++) {
        if (!map_overlays[i].used) {
            overlayid_t id = map_next_overlay_id++;
            if (id == 0) {
                id = map_next_overlay_id++;
            }
            map_overlays[i].used = true;
            map_overlays[i].id = id;
            return id;
        }
    }
    return 0;
}

bool map_overlay_slot_exists(overlayid_t overlay)
{
    if (overlay == 0) {
        return false;
    }
    for (glui32 i = 0; i < MAP_MAX_OVERLAYS; i++) {
        if (map_overlays[i].used && map_overlays[i].id == overlay) {
            return true;
        }
    }
    return false;
}

} // namespace

void gli_map_post_event(glui32 subtype, glui32 payload)
{
    if (!gli_map_event_request) {
        return;
    }
    gli_map_event_request = false;
    gli_event_store(evtype_Map, nullptr, subtype, payload);
}

void gli_map_ui_shutdown()
{
    gli_map_event_request = false;
    map_clear_overlays_local();
    gli_map_ui_close();
}

void glk_map_set_hyperlinks(const glk_maphyperlink_t *hyperlinks, glui32 nhyperlinks)
{
    gli_map_ui_set_hyperlinks(map_filter_hyperlinks(hyperlinks, nhyperlinks));
}

glui32 glk_map_present_svg(const unsigned char *data, glui32 len,
    glui32 flags, glui32 bgcolor,
    glsi32 focusleft, glsi32 focustop,
    glui32 focuswidth, glui32 focusheight,
    const glk_maphyperlink_t *hyperlinks, glui32 nhyperlinks)
{
    if (!svg_looks_valid(data, len)) {
        return 0;
    }

    map_notify_clear_overlays();
    auto focus = map_focus_from_args(focusleft, focustop, focuswidth, focusheight, flags);
    gli_map_ui_present_svg(data, len, flags, bgcolor, &focus,
        map_filter_hyperlinks(hyperlinks, nhyperlinks));
    return 1;
}

glui32 glk_map_present_image(glui32 image, glui32 flags, glui32 bgcolor,
    glsi32 focusleft, glsi32 focustop, glui32 focuswidth, glui32 focusheight,
    const glk_maphyperlink_t *hyperlinks, glui32 nhyperlinks)
{
    if (image == 0) {
        return 0;
    }
    if (!gli_picture_load(image)) {
        return 0;
    }

    map_notify_clear_overlays();
    auto focus = map_focus_from_args(focusleft, focustop, focuswidth, focusheight, flags);
    gli_map_ui_present_image(image, flags, bgcolor, &focus,
        map_filter_hyperlinks(hyperlinks, nhyperlinks));
    return 1;
}

overlayid_t glk_map_overlay(glui32 image, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex,
    glui32 link_id, const char *linklabel)
{
    if (image == 0) {
        return 0;
    }
    if (!gli_picture_load(image)) {
        return 0;
    }

    overlayid_t id = map_alloc_overlay_id();
    if (id == 0) {
        return 0;
    }

    map_overlay_ui_t ov;
    ov.overlay_id = id;
    ov.image_id = image;
    ov.left = left;
    ov.top = top;
    ov.width = width;
    ov.height = height;
    ov.zindex = zindex;
    ov.link_id = link_id;
    if (linklabel != nullptr) {
        ov.link_label = linklabel;
    }
    gli_map_ui_overlay(ov);
    return id;
}

overlayid_t glk_map_fill_rect(glui32 color, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex)
{
    if (width == 0 || height == 0) {
        return 0;
    }

    overlayid_t id = map_alloc_overlay_id();
    if (id == 0) {
        return 0;
    }

    map_overlay_ui_t ov;
    ov.overlay_id = id;
    ov.color = color;
    ov.left = left;
    ov.top = top;
    ov.width = width;
    ov.height = height;
    ov.zindex = zindex;
    ov.is_fill = true;
    gli_map_ui_overlay(ov);
    return id;
}

glui32 glk_map_overlay_move(overlayid_t overlay, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex)
{
    if (!map_overlay_slot_exists(overlay)) {
        return 0;
    }
    gli_map_ui_overlay_move(overlay, left, top, width, height, zindex);
    return 1;
}

glui32 glk_map_overlay_clear(overlayid_t overlay)
{
    if (overlay == 0) {
        return 0;
    }
    for (glui32 i = 0; i < MAP_MAX_OVERLAYS; i++) {
        if (map_overlays[i].used && map_overlays[i].id == overlay) {
            map_overlays[i].used = false;
            map_overlays[i].id = 0;
            gli_map_ui_overlay_clear(overlay);
            return 1;
        }
    }
    return 0;
}

glui32 glk_map_overlay_clear_all(void)
{
    map_notify_clear_overlays();
    return 1;
}

void glk_map_close(void)
{
    gli_map_event_request = false;
    map_clear_overlays_local();
    gli_map_ui_close();
}

void glk_map_set_focus(glsi32 focusleft, glsi32 focustop,
    glui32 focuswidth, glui32 focusheight)
{
    map_focus_rect_t focus;
    focus.left = focusleft;
    focus.top = focustop;
    focus.width = focuswidth;
    focus.height = focusheight;
    gli_map_ui_set_focus(&focus);
}

void glk_map_clear_focus(void)
{
    gli_map_ui_clear_focus();
}

void glk_request_map_event(void)
{
    gli_map_event_request = true;
    gli_map_ui_set_event_request(true);
}

void glk_cancel_map_event(void)
{
    gli_map_event_request = false;
    gli_map_ui_set_event_request(false);
}
