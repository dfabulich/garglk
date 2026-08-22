// Copyright (C) 2026 by Dan Fabulich.
//
// Cocoa IPC bridge for the Glk map-document extension (interpreter side).

#include <vector>

#import "Cocoa/Cocoa.h"
#include "garglk.h"
#import "sysmac.h"

#include "glk.h"
#include "imgload.h"
#include "map.h"

namespace {

NSMutableData *pack_hyperlinks(const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    NSMutableData *data = [NSMutableData data];
    auto append_u32 = [&](glui32 v) {
        unsigned char b[4] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
            static_cast<unsigned char>((v >> 16) & 0xff),
            static_cast<unsigned char>((v >> 24) & 0xff),
        };
        [data appendBytes:b length:4];
    };
    append_u32(hyperlinks.size());
    for (const auto &link : hyperlinks) {
        append_u32(link.id);
        append_u32(link.points.size());
        for (const auto &pt : link.points) {
            glsi32 x = pt.first;
            glsi32 y = pt.second;
            [data appendBytes:&x length:4];
            [data appendBytes:&y length:4];
        }
        append_u32(link.label.size());
        if (!link.label.empty()) {
            [data appendBytes:link.label.data() length:link.label.size()];
        }
    }
    return data;
}

NSData *picture_raw_data(glui32 image)
{
    std::vector<unsigned char> raw;
    glui32 chunktype = 0;
    if (!gli_picture_copy_raw(image, raw, chunktype)) {
        return nil;
    }
    return [NSData dataWithBytes:raw.data() length:raw.size()];
}

} // namespace

void gli_map_ui_present_svg(const unsigned char *data, glui32 len,
    glui32 flags, glui32 bgcolor, const map_focus_rect_t *focus,
    const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSMutableData *pack = [NSMutableData dataWithCapacity:len + 256];
    auto append_u32 = [&](glui32 v) {
        unsigned char b[4] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
            static_cast<unsigned char>((v >> 16) & 0xff),
            static_cast<unsigned char>((v >> 24) & 0xff),
        };
        [pack appendBytes:b length:4];
    };
    append_u32(bgcolor);
    append_u32(len);
    if (len > 0) {
        [pack appendBytes:data length:len];
    }
    [pack appendData:pack_hyperlinks(hyperlinks)];

    int fl = focus ? focus->left : 0;
    int ft = focus ? focus->top : 0;
    int fw = focus ? static_cast<int>(focus->width) : 0;
    int fh = focus ? static_cast<int>(focus->height) : 0;
    [gargoyle mapPresent:processID flags:flags focusLeft:fl focusTop:ft focusWidth:fw focusHeight:fh data:pack];
    [pool drain];
}

void gli_map_ui_present_image(glui32 image, glui32 flags, glui32 bgcolor,
    const map_focus_rect_t *focus, const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSData *png = picture_raw_data(image);
    if (png == nil) {
        [pool drain];
        return;
    }
    NSMutableData *pack = [NSMutableData data];
    auto append_u32 = [&](glui32 v) {
        unsigned char b[4] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
            static_cast<unsigned char>((v >> 16) & 0xff),
            static_cast<unsigned char>((v >> 24) & 0xff),
        };
        [pack appendBytes:b length:4];
    };
    append_u32(bgcolor);
    append_u32(image);
    [pack appendData:pack_hyperlinks(hyperlinks)];

    int fl = focus ? focus->left : 0;
    int ft = focus ? focus->top : 0;
    int fw = focus ? static_cast<int>(focus->width) : 0;
    int fh = focus ? static_cast<int>(focus->height) : 0;
    [gargoyle mapPresentImage:processID flags:flags focusLeft:fl focusTop:ft focusWidth:fw focusHeight:fh png:png data:pack];
    [pool drain];
}

void gli_map_ui_set_hyperlinks(const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapSetHyperlinks:processID data:pack_hyperlinks(hyperlinks)];
    [pool drain];
}

void gli_map_ui_overlay(const map_overlay_ui_t &overlay)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSData *png = nil;
    if (!overlay.is_fill) {
        png = picture_raw_data(overlay.image_id);
    }
    [gargoyle mapOverlay:processID overlayId:overlay.overlay_id left:overlay.left top:overlay.top
                   width:overlay.width height:overlay.height zindex:overlay.zindex
                 linkId:overlay.link_id color:overlay.color isFill:overlay.is_fill
                    png:png label:[NSString stringWithUTF8String:overlay.link_label.c_str()]];
    [pool drain];
}

void gli_map_ui_overlay_move(overlayid_t overlay, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapOverlayMove:processID overlayId:overlay left:left top:top width:width height:height zindex:zindex];
    [pool drain];
}

void gli_map_ui_overlay_clear(overlayid_t overlay)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapOverlayClear:processID overlayId:overlay];
    [pool drain];
}

void gli_map_ui_overlay_clear_all()
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapOverlayClearAll:processID];
    [pool drain];
}

void gli_map_ui_close()
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapClose:processID];
    [pool drain];
}

void gli_map_ui_set_focus(const map_focus_rect_t *focus)
{
    if (focus == nullptr) {
        return;
    }
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapSetFocus:processID focusLeft:focus->left focusTop:focus->top
               focusWidth:focus->width focusHeight:focus->height];
    [pool drain];
}

void gli_map_ui_clear_focus()
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle mapClearFocus:processID];
    [pool drain];
}

void gli_map_ui_set_event_request(bool enabled)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [gargoyle setMapEventRequest:processID enabled:enabled ? YES : NO];
    [pool drain];
}
