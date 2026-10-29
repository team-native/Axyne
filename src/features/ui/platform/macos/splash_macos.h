#ifndef AXYNE_SPLASH_MACOS_H
#define AXYNE_SPLASH_MACOS_H

#import <AppKit/AppKit.h>

#include "axyne/startup_steps.h"

/* Startup loading screen (Figma 134:2451): a borderless window over the area
 * the main window will occupy. Startup runs on the main thread, so every
 * step change redraws synchronously. */
@interface AxyneSplashController : NSObject
- (instancetype)initWithFrame:(NSRect)frame version:(NSString *)version;
- (void)setStep:(AxyneStartupStep)step;
- (void)close;
@end

#endif
