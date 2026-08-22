//
//  mapwindow-cocoa.h
//  Gargoyle
//
//  Separate OS window for the Glk map-document extension (SVG / images).

#import <Cocoa/Cocoa.h>

#include "glk.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^GargoyleMapEventBlock)(NSUInteger subtype, NSUInteger payload);

@interface MapFocusRect : NSObject
@property NSInteger left;
@property NSInteger top;
@property NSUInteger width;
@property NSUInteger height;
@end

@interface MapHyperlink : NSObject
@property NSUInteger linkId;
@property (copy, nullable) NSString *label;
@property (strong) NSArray<NSValue *> *points;
@end

@interface MapOverlay : NSObject
@property NSUInteger overlayId;
@property (strong, nullable) NSImage *image;
@property (strong, nullable) NSColor *fillColor;
@property NSInteger left;
@property NSInteger top;
@property NSUInteger width;
@property NSUInteger height;
@property NSUInteger zindex;
@property NSUInteger linkId;
@property (copy, nullable) NSString *label;
@end

@interface MapWindowController : NSWindowController <NSWindowDelegate>

- (instancetype)initWithGameWindow:(NSWindow *)gameWindow
                             title:(NSString *)title
                        eventBlock:(GargoyleMapEventBlock)eventBlock;

@property (assign, nullable) NSWindow *gameWindow;
@property (copy) GargoyleMapEventBlock eventBlock;
@property (assign) BOOL mapEventRequest;
@property (readonly) BOOL mapVisible;
@property (readonly) BOOL hasDocument;

- (void)presentSVG:(NSString *)svg
             flags:(NSUInteger)flags
          bgcolor:(NSUInteger)bgcolor
             focus:(nullable MapFocusRect *)focus
       hyperlinks:(NSArray<MapHyperlink *> *)hyperlinks;
- (void)presentImage:(NSImage *)image
               flags:(NSUInteger)flags
            bgcolor:(NSUInteger)bgcolor
               focus:(nullable MapFocusRect *)focus
          hyperlinks:(NSArray<MapHyperlink *> *)hyperlinks;
- (void)setOverlay:(MapOverlay *)overlay;
- (void)moveOverlay:(NSUInteger)overlayId
               left:(NSInteger)left
                top:(NSInteger)top
              width:(NSUInteger)width
             height:(NSUInteger)height
             zindex:(NSUInteger)zindex;
- (void)clearOverlay:(NSUInteger)overlayId;
- (void)clearAllOverlays;
- (void)setHyperlinks:(NSArray<MapHyperlink *> *)hyperlinks;
- (void)closeMap;
- (void)setFocus:(nullable MapFocusRect *)focus;
- (void)showMap;
- (void)hideMap;

@end

NS_ASSUME_NONNULL_END
