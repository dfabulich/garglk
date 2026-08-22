
#pragma mark Map document extension

static NSMutableDictionary *g_map_windows;
static NSMutableDictionary *g_map_pending_subtype;
static NSMutableDictionary *g_map_pending_payload;

- (MapWindowController *) mapControllerForPID:(pid_t)pid
{
    if (g_map_windows == nil) {
        g_map_windows = [[NSMutableDictionary alloc] init];
        g_map_pending_subtype = [[NSMutableDictionary alloc] init];
        g_map_pending_payload = [[NSMutableDictionary alloc] init];
    }
    NSNumber *key = [NSNumber numberWithInt:pid];
    MapWindowController *map = g_map_windows[key];
    if (map == nil) {
        GargoyleWindow *gameWindow = [windows objectForKey:key];
        GargoyleMapEventBlock block = ^(NSUInteger subtype, NSUInteger payload) {
            g_map_pending_subtype[key] = @(subtype);
            g_map_pending_payload[key] = @(payload);
            kill(pid, SIGUSR1);
        };
        map = [[MapWindowController alloc] initWithGameWindow:gameWindow
                                                        title:gameWindow.title
                                                   eventBlock:block];
        g_map_windows[key] = map;
    }
    return map;
}

- (NSArray<MapHyperlink *> *) mapUnpackHyperlinks:(const unsigned char *)bytes length:(size_t)blen offset:(size_t *)offInOut
{
    NSMutableArray<MapHyperlink *> *hyperlinks = [NSMutableArray new];
    size_t off = *offInOut;
    if (off + 4 > blen) {
        return hyperlinks;
    }
    glui32 nlinks = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
    off += 4;
    for (glui32 hi = 0; hi < nlinks; hi++) {
        if (off + 8 > blen) {
            break;
        }
        glui32 hid = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
        off += 4;
        glui32 npoints = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
        off += 4;
        if (npoints < 3 || off + npoints * 8 + 4 > blen) {
            break;
        }
        NSMutableArray<NSValue *> *pts = [NSMutableArray arrayWithCapacity:npoints];
        for (glui32 pi = 0; pi < npoints; pi++) {
            int32_t x = (int32_t)((glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24));
            off += 4;
            int32_t y = (int32_t)((glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24));
            off += 4;
            [pts addObject:[NSValue valueWithPoint:NSMakePoint(x, y)]];
        }
        glui32 labellen = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
        off += 4;
        if (off + labellen > blen) {
            break;
        }
        NSString *label = nil;
        if (labellen > 0) {
            label = [[NSString alloc] initWithBytes:bytes + off length:labellen encoding:NSUTF8StringEncoding];
        }
        off += labellen;
        MapHyperlink *hlink = [MapHyperlink new];
        hlink.linkId = hid;
        hlink.label = label;
        hlink.points = pts;
        [hyperlinks addObject:hlink];
    }
    *offInOut = off;
    return hyperlinks;
}

- (void) mapPresent:(pid_t)processID flags:(int)flags focusLeft:(int)fl focusTop:(int)ft focusWidth:(int)fw focusHeight:(int)fh data:(NSData *)buf
{
    MapWindowController *map = [self mapControllerForPID:processID];
    const unsigned char *bytes = (const unsigned char *)buf.bytes;
    size_t blen = buf.length;
    size_t off = 0;
    glui32 bgcolor = mapcolor_Default, dataLen = 0;
    if (off + 4 <= blen) {
        bgcolor = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
        off += 4;
    }
    if (off + 4 <= blen) {
        dataLen = (glui32)bytes[off] | ((glui32)bytes[off + 1] << 8) | ((glui32)bytes[off + 2] << 16) | ((glui32)bytes[off + 3] << 24);
        off += 4;
    }
    if (off + dataLen > blen) {
        return;
    }
    const unsigned char *payload = bytes + off;
    off += dataLen;
    NSArray<MapHyperlink *> *hyperlinks = [self mapUnpackHyperlinks:bytes length:blen offset:&off];
    MapFocusRect *focus = nil;
    if ((flags & mapflag_HasFocus) != 0) {
        focus = [MapFocusRect new];
        focus.left = fl;
        focus.top = ft;
        focus.width = MAX(fw, 0);
        focus.height = MAX(fh, 0);
    }
    NSString *svg = [[NSString alloc] initWithBytes:payload length:dataLen encoding:NSUTF8StringEncoding] ?: @"";
    [map presentSVG:svg flags:flags bgcolor:bgcolor focus:focus hyperlinks:hyperlinks];
}

- (void) mapPresentImage:(pid_t)processID flags:(int)flags focusLeft:(int)fl focusTop:(int)ft focusWidth:(int)fw focusHeight:(int)fh png:(NSData *)png data:(NSData *)buf
{
    MapWindowController *map = [self mapControllerForPID:processID];
    NSImage *img = [[NSImage alloc] initWithData:png];
    if (!img) {
        return;
    }
    size_t off = 0;
    const unsigned char *bytes = (const unsigned char *)buf.bytes;
    size_t blen = buf.length;
    glui32 bgcolor = mapcolor_Default;
    if (blen >= 4) {
        bgcolor = (glui32)bytes[0] | ((glui32)bytes[1] << 8) | ((glui32)bytes[2] << 16) | ((glui32)bytes[3] << 24);
        off = 8;
    }
    NSArray<MapHyperlink *> *hyperlinks = [self mapUnpackHyperlinks:bytes length:blen offset:&off];
    MapFocusRect *focus = nil;
    if ((flags & mapflag_HasFocus) != 0) {
        focus = [MapFocusRect new];
        focus.left = fl;
        focus.top = ft;
        focus.width = MAX(fw, 0);
        focus.height = MAX(fh, 0);
    }
    [map presentImage:img flags:flags bgcolor:bgcolor focus:focus hyperlinks:hyperlinks];
}

- (void) mapSetHyperlinks:(pid_t)processID data:(NSData *)buf
{
    MapWindowController *map = [self mapControllerForPID:processID];
    size_t off = 0;
    NSArray<MapHyperlink *> *hyperlinks = [self mapUnpackHyperlinks:(const unsigned char *)buf.bytes length:buf.length offset:&off];
    [map setHyperlinks:hyperlinks];
}

- (void) mapOverlay:(pid_t)processID overlayId:(unsigned)overlayId left:(int)left top:(int)top width:(unsigned)width height:(unsigned)height zindex:(unsigned)zindex linkId:(unsigned)linkId color:(unsigned)color isFill:(BOOL)isFill png:(NSData *)png label:(NSString *)label
{
    MapWindowController *map = [self mapControllerForPID:processID];
    MapOverlay *ov = [MapOverlay new];
    ov.overlayId = overlayId;
    ov.left = left;
    ov.top = top;
    ov.width = width;
    ov.height = height;
    ov.zindex = zindex;
    ov.linkId = linkId;
    ov.label = label;
    if (isFill) {
        ov.fillColor = [NSColor colorWithSRGBRed:((color >> 16) & 0xff) / 255.0 green:((color >> 8) & 0xff) / 255.0 blue:(color & 0xff) / 255.0 alpha:1.0];
    } else if (png) {
        ov.image = [[NSImage alloc] initWithData:png];
    }
    [map setOverlay:ov];
}

- (void) mapOverlayMove:(pid_t)processID overlayId:(unsigned)overlayId left:(int)left top:(int)top width:(unsigned)width height:(unsigned)height zindex:(unsigned)zindex
{
    MapWindowController *map = [self mapControllerForPID:processID];
    [map moveOverlay:overlayId left:left top:top width:width height:height zindex:zindex];
}

- (void) mapOverlayClear:(pid_t)processID overlayId:(unsigned)overlayId
{
    MapWindowController *map = [self mapControllerForPID:processID];
    [map clearOverlay:overlayId];
}

- (void) mapOverlayClearAll:(pid_t)processID
{
    MapWindowController *map = [self mapControllerForPID:processID];
    [map clearAllOverlays];
}

- (void) mapClose:(pid_t)processID
{
    NSNumber *key = [NSNumber numberWithInt:processID];
    MapWindowController *map = g_map_windows[key];
    if (map) {
        [map closeMap];
        [g_map_windows removeObjectForKey:key];
    }
    [g_map_pending_subtype removeObjectForKey:key];
    [g_map_pending_payload removeObjectForKey:key];
}

- (void) mapSetFocus:(pid_t)processID focusLeft:(int)fl focusTop:(int)ft focusWidth:(unsigned)fw focusHeight:(unsigned)fh
{
    MapWindowController *map = [self mapControllerForPID:processID];
    MapFocusRect *focus = [MapFocusRect new];
    focus.left = fl;
    focus.top = ft;
    focus.width = fw;
    focus.height = fh;
    [map setFocus:focus];
}

- (void) mapClearFocus:(pid_t)processID
{
    MapWindowController *map = [self mapControllerForPID:processID];
    [map setFocus:nil];
}

- (void) setMapEventRequest:(pid_t)processID enabled:(BOOL)enabled
{
    MapWindowController *map = [self mapControllerForPID:processID];
    map.mapEventRequest = enabled;
}

- (BOOL) retrieveMapEvent:(pid_t)processID subtype:(unsigned *)subtype payload:(unsigned *)payload
{
    NSNumber *key = [NSNumber numberWithInt:processID];
    NSNumber *st = g_map_pending_subtype[key];
    NSNumber *pl = g_map_pending_payload[key];
    if (!st || !pl) {
        return NO;
    }
    *subtype = st.unsignedIntegerValue;
    *payload = pl.unsignedIntegerValue;
    [g_map_pending_subtype removeObjectForKey:key];
    [g_map_pending_payload removeObjectForKey:key];
    return YES;
}
