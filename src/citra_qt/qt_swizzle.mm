// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#import <QtGui/private/qmetallayer_p.h>
#import <objc/runtime.h>

namespace QtSwizzle {

void Dummy() {
    // Call this anywhere to make sure that qt_swizzle.mm is linked.
    // noop
}

} // namespace QtSwizzle

@implementation QMetalLayer (AzaharPatch)

+ (void)load {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      Class targetClass = [self class];

      // Get the original and swizzled methods
      Method originalMethod =
          class_getInstanceMethod(targetClass, @selector(setNeedsDisplayInRect:));
      Method swizzledMethod =
          class_getInstanceMethod(targetClass, @selector(swizzled_setNeedsDisplayInRect:));

      // Swap the implementations
      method_exchangeImplementations(originalMethod, swizzledMethod);
    });
}

- (void)swizzled_setNeedsDisplayInRect:(CGRect)rect {
    constexpr auto tooBig = 1e10; // Arbitrary large number

    // Check for problematic huge rectangles
    if ((!self.needsDisplay) && (rect.size.width > tooBig || rect.size.height > tooBig ||
                                 rect.origin.x < -tooBig || rect.origin.y < -tooBig)) {
        return;
    }

    // Call the original implementation
    [self swizzled_setNeedsDisplayInRect:rect];
}

@end