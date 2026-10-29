#import "splash_macos.h"

static NSColor *axyne_splash_color(unsigned int rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0
                               green:((rgb >> 8) & 0xff) / 255.0
                                blue:(rgb & 0xff) / 255.0
                               alpha:1.0];
}

static NSString *axyne_splash_string(const char *utf8)
{
    NSString *text = utf8 != NULL ? [NSString stringWithUTF8String:utf8] : nil;
    return text != nil ? text : @"";
}

@interface AxyneSplashView : NSView {
    AxyneStartupStep _step;
    NSString *_footnote;
}
- (void)setStep:(AxyneStartupStep)step;
- (void)setFootnote:(NSString *)footnote;
@end

@implementation AxyneSplashView

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [_footnote release];
    [super dealloc];
}

- (void)setStep:(AxyneStartupStep)step { _step = step; }

- (void)setFootnote:(NSString *)footnote
{
    [_footnote release];
    _footnote = [footnote copy];
}

- (void)drawText:(NSString *)text inRect:(NSRect)rect font:(NSFont *)font
           color:(unsigned int)color alignment:(NSTextAlignment)alignment
{
    NSMutableParagraphStyle *style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [style setAlignment:alignment];
    [style setLineBreakMode:NSLineBreakByClipping];
    [text drawInRect:rect withAttributes:@{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: axyne_splash_color(color),
        NSParagraphStyleAttributeName: style
    }];
}

- (void)drawRect:(NSRect)dirty
{
    NSRect bounds = [self bounds];
    CGFloat center = NSMidX(bounds);
    CGFloat top = floor((NSHeight(bounds) - 48 - AXYNE_STARTUP_CONTENT_HEIGHT) / 2);
    CGFloat trackLeft = center - AXYNE_STARTUP_TRACK_WIDTH / 2;
    unsigned progress = axyne_startup_progress(_step);
    NSFont *text = [NSFont systemFontOfSize:13];
    NSImage *logo = [NSApp applicationIconImage];
    NSRect track;
    size_t i;
    (void)dirty;

    [axyne_splash_color(AXYNE_STARTUP_BACKGROUND) setFill];
    NSRectFill(bounds);
    if (logo != nil)
        [logo drawInRect:NSMakeRect(center - AXYNE_STARTUP_LOGO / 2, top,
                                    AXYNE_STARTUP_LOGO, AXYNE_STARTUP_LOGO)
                fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
                fraction:1.0 respectFlipped:YES hints:nil];
    [self drawText:@"Axyne"
            inRect:NSMakeRect(0, top + AXYNE_STARTUP_TITLE_Y, NSWidth(bounds), 52)
              font:[NSFont boldSystemFontOfSize:AXYNE_STARTUP_TITLE_PT]
             color:AXYNE_STARTUP_TEXT alignment:NSTextAlignmentCenter];

    track = NSMakeRect(trackLeft, top + AXYNE_STARTUP_TRACK_Y,
                       AXYNE_STARTUP_TRACK_WIDTH, AXYNE_STARTUP_TRACK_HEIGHT);
    [axyne_splash_color(AXYNE_STARTUP_TRACK) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:track xRadius:3 yRadius:3] fill];
    if (progress != 0) {
        track.size.width = AXYNE_STARTUP_TRACK_WIDTH * progress / 100.0;
        [axyne_splash_color(AXYNE_STARTUP_ACCENT) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:track xRadius:3 yRadius:3] fill];
    }

    [self drawText:axyne_splash_string(axyne_startup_step_status(_step))
            inRect:NSMakeRect(trackLeft, top + AXYNE_STARTUP_STATUS_Y, AXYNE_STARTUP_TRACK_WIDTH, 18)
              font:text color:AXYNE_STARTUP_MUTED alignment:NSTextAlignmentLeft];
    [self drawText:[NSString stringWithFormat:@"%u%%", progress]
            inRect:NSMakeRect(trackLeft, top + AXYNE_STARTUP_STATUS_Y, AXYNE_STARTUP_TRACK_WIDTH, 18)
              font:text color:AXYNE_STARTUP_ACCENT alignment:NSTextAlignmentRight];

    for (i = 0; i < AXYNE_STARTUP_STEP_COUNT; ++i) {
        CGFloat y = top + AXYNE_STARTUP_STEPS_Y + (CGFloat)i * AXYNE_STARTUP_STEP_ROW;
        BOOL done = (AxyneStartupStep)i < _step;
        BOOL running = (AxyneStartupStep)i == _step;
        [self drawText:done ? @"✓" : running ? @"●" : @"○"
                inRect:NSMakeRect(center - 52, y, 18, 20) font:text
                 color:done ? AXYNE_STARTUP_DONE : running ? AXYNE_STARTUP_ACCENT : AXYNE_STARTUP_DIM
             alignment:NSTextAlignmentLeft];
        [self drawText:axyne_splash_string(axyne_startup_step_label((AxyneStartupStep)i))
                inRect:NSMakeRect(center - 32, y, 160, 20) font:text
                 color:running ? AXYNE_STARTUP_TEXT : done ? AXYNE_STARTUP_MUTED : AXYNE_STARTUP_DIM
             alignment:NSTextAlignmentLeft];
    }

    if (_footnote != nil)
        [self drawText:_footnote
                inRect:NSMakeRect(0, NSHeight(bounds) - 48, NSWidth(bounds), 16)
                  font:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]
                 color:AXYNE_STARTUP_DIM alignment:NSTextAlignmentCenter];
}

@end

@implementation AxyneSplashController {
    NSWindow *_window;
    AxyneSplashView *_view;
}

- (instancetype)initWithFrame:(NSRect)frame version:(NSString *)version
{
    self = [super init];
    if (self == nil) return nil;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setReleasedWhenClosed:NO];
    [_window setOpaque:YES];
    [_window setHasShadow:YES];
    _view = [[AxyneSplashView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(frame), NSHeight(frame))];
    [_view setFootnote:[NSString stringWithFormat:@"%@ · v%@",
                        axyne_splash_string(AXYNE_STARTUP_FOOTNOTE), version != nil ? version : @""]];
    [_window setContentView:_view];
    [_window orderFront:nil];
    [_view display];
    return self;
}

- (void)dealloc
{
    [self close];
    [super dealloc];
}

- (void)setStep:(AxyneStartupStep)step
{
    if (_view == nil) return;
    [_view setStep:step];
    [_view display]; /* the main thread is busy starting up: draw now */
}

- (void)close
{
    if (_window == nil) return;
    [_window orderOut:nil];
    [_window close];
    [_window release];
    _window = nil;
    [_view release];
    _view = nil;
}

@end
