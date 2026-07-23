// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <qwindowdefs.h>

namespace MetalUtil {

void* CreateMetalLayer(WId winId) {
    // Qt's MetalSurface QWindow on macOS does not always back the NSView
    // with a CAMetalLayer immediately; MoltenVK 1.3+ aborts in
    // MVKSurface::getNaturalExtent() if the layer is not CAMetalLayer.
    // Force-install a fresh CAMetalLayer on the view.
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    NSView* view = (__bridge NSView*)winId;
    view.wantsLayer = YES;
    view.layer = metalLayer;
    return (__bridge void*)metalLayer;
}

} // namespace MetalUtil
