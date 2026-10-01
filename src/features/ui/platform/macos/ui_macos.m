#import <AppKit/AppKit.h>

static const CGFloat AXYNE_SIDEBAR = 248.0;
static const CGFloat AXYNE_TOOLBAR = 40.0;
static const CGFloat AXYNE_TABS = 36.0;
static const CGFloat AXYNE_STATUS = 26.0;
static const CGFloat AXYNE_BOTTOM = 158.0;

static NSColor *axyne_color(CGFloat red, CGFloat green, CGFloat blue)
{
    return [NSColor colorWithCalibratedRed:red / 255.0
                                     green:green / 255.0
                                      blue:blue / 255.0
                                     alpha:1.0];
}

@interface AxyneWorkspaceView : NSView {
    NSView *_editorView;
    NSBundle *_scintillaBundle;
}
@end

@implementation AxyneWorkspaceView

- (BOOL)isFlipped
{
    return YES;
}

- (void)loadScintillaView
{
    if (_editorView != nil) {
        return;
    }
    NSString *frameworkPath = [[NSBundle mainBundle] pathForResource:@"Scintilla"
                                                               ofType:@"framework"
                                                          inDirectory:@"Frameworks"];
    if (frameworkPath != nil) {
        _scintillaBundle = [[NSBundle bundleWithPath:frameworkPath] retain];
        [_scintillaBundle load];
    }

    Class scintillaClass = NSClassFromString(@"ScintillaView");
    if (scintillaClass != Nil) {
        _editorView = [[scintillaClass alloc] initWithFrame:NSZeroRect];
        [_editorView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [self addSubview:_editorView];
        [self setNeedsLayout:YES];
    }
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self loadScintillaView];
}

- (void)layout
{
    [super layout];
    NSRect bounds = [self bounds];
    CGFloat editorTop = AXYNE_TOOLBAR + AXYNE_TABS;
    CGFloat bottomTop = NSHeight(bounds) - AXYNE_STATUS - AXYNE_BOTTOM;
    NSRect editorFrame = NSMakeRect(AXYNE_SIDEBAR, editorTop,
        MAX(0.0, NSWidth(bounds) - AXYNE_SIDEBAR),
        MAX(0.0, bottomTop - editorTop));
    [_editorView setFrame:editorFrame];
}

- (void)drawLabel:(NSString *)label at:(NSPoint)point
             size:(CGFloat)size color:(NSColor *)color family:(NSString *)family
{
    NSDictionary *attributes = @{
        NSFontAttributeName: [NSFont fontWithName:family size:size] ?: [NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName: color
    };
    [label drawAtPoint:point withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = [self bounds];
    CGFloat width = NSWidth(bounds);
    CGFloat height = NSHeight(bounds);
    CGFloat bottomTop = height - AXYNE_STATUS - AXYNE_BOTTOM;
    CGFloat statusTop = height - AXYNE_STATUS;
    NSColor *background = axyne_color(22, 23, 26);
    NSColor *panel = axyne_color(31, 33, 38);
    NSColor *muted = axyne_color(115, 119, 128);
    NSColor *text = axyne_color(199, 201, 206);
    NSColor *border = axyne_color(41, 44, 50);
    NSColor *toolbar = axyne_color(28, 30, 34);

    [background setFill];
    NSRectFill(bounds);
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, 0, width, AXYNE_TOOLBAR));
    [axyne_color(23, 25, 28) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR, width, AXYNE_TABS));
    [panel setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR + AXYNE_TABS,
                          AXYNE_SIDEBAR, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    [toolbar setFill];
    NSRectFill(NSMakeRect(0, bottomTop, width, AXYNE_BOTTOM));
    [axyne_color(25, 27, 30) setFill];
    NSRectFill(NSMakeRect(0, statusTop, width, AXYNE_STATUS));
    [border setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR - 1, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, bottomTop - AXYNE_TOOLBAR - AXYNE_TABS));
    NSRectFill(NSMakeRect(0, bottomTop, width, 1));

    [self drawLabel:@"▱   ▣    ↶   ↷       ▷  Debug · x64             빌드  ⌘B"
                at:NSMakePoint(14, 13) size:12 color:muted family:@"SF Pro Text"];
    NSRect search = NSMakeRect(MAX(400, width - 360), 7, 348, 26);
    [axyne_color(22, 23, 26) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] fill];
    [border setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:search xRadius:4 yRadius:4] stroke];
    [self drawLabel:@"⌕  파일 이동, > 명령 실행"
                at:NSMakePoint(NSMinX(search) + 10, 13) size:11 color:muted family:@"SF Pro Text"];

    [axyne_color(182, 122, 246) setFill];
    NSRectFill(NSMakeRect(AXYNE_SIDEBAR + 20, AXYNE_TOOLBAR + AXYNE_TABS,
                          1, AXYNE_TABS));
    [self drawLabel:@"C   main.c     ×"
                at:NSMakePoint(AXYNE_SIDEBAR + 32, AXYNE_TOOLBAR + 10)
                size:12 color:text family:@"SF Pro Text"];
    [self drawLabel:@"탐색기" at:NSMakePoint(12, AXYNE_TOOLBAR + AXYNE_TABS + 10)
                size:11 color:muted family:@"SF Pro Text"];
    [self drawLabel:@"⌄  axyne" at:NSMakePoint(16, AXYNE_TOOLBAR + AXYNE_TABS + 34)
                size:12 color:text family:@"SF Pro Text"];
    [self drawLabel:@"⌄  src" at:NSMakePoint(32, AXYNE_TOOLBAR + AXYNE_TABS + 57)
                size:12 color:text family:@"SF Pro Text"];
    [axyne_color(47, 52, 60) setFill];
    NSRectFill(NSMakeRect(0, AXYNE_TOOLBAR + AXYNE_TABS + 66,
                          AXYNE_SIDEBAR, 22));
    [self drawLabel:@"C  main.c" at:NSMakePoint(52, AXYNE_TOOLBAR + AXYNE_TABS + 69)
                size:12 color:text family:@"SF Pro Text"];
    [self drawLabel:@"출력     문제 1     터미널"
                at:NSMakePoint(12, bottomTop + 9) size:11 color:muted family:@"SF Pro Text"];
    [self drawLabel:@"✓ 빌드 준비됨"
                at:NSMakePoint(12, statusTop + 6) size:10 color:muted family:@"SF Pro Text"];
    [self drawLabel:@"줄 1, 열 1     UTF-8    C17"
                at:NSMakePoint(MAX(12, width - 250), statusTop + 6)
                size:10 color:muted family:@"SF Pro Text"];

    if (_editorView == nil) {
        [self drawLabel:@"Scintilla framework unavailable"
                    at:NSMakePoint(AXYNE_SIDEBAR + 24, AXYNE_TOOLBAR + AXYNE_TABS + 24)
                    size:12 color:muted family:@"Menlo"];
    }
}

- (void)dealloc
{
    [_editorView release];
    [_scintillaBundle unload];
    [_scintillaBundle release];
    [super dealloc];
}

@end

@interface AxyneApplicationDelegate : NSObject <NSApplicationDelegate> {
    NSWindow *_window;
    NSString *_appName;
}
- (instancetype)initWithAppName:(NSString *)appName;
@end

@implementation AxyneApplicationDelegate

- (instancetype)initWithAppName:(NSString *)appName
{
    self = [super init];
    if (self != nil) {
        _appName = [appName copy];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    NSRect frame = NSMakeRect(0, 0, 1440, 900);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:_appName ?: @"Axyne"];
    [_window setMinSize:NSMakeSize(800, 560)];
    [_window setContentView:[[[AxyneWorkspaceView alloc] initWithFrame:frame] autorelease]];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

- (void)dealloc
{
    [_window release];
    [_appName release];
    [super dealloc];
}

@end

static void axyne_install_menu(NSApplication *application)
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Axyne"
        action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Axyne"];
    [appMenu addItemWithTitle:@"About Axyne" action:nil keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit Axyne" action:@selector(terminate:)
                 keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    NSArray *titles = @[@"File", @"Edit", @"View", @"Build", @"Debug", @"Tools", @"Help"];
    for (NSString *title in titles) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
            action:nil keyEquivalent:@""];
        [item setSubmenu:[[NSMenu alloc] initWithTitle:title]];
        [mainMenu addItem:item];
        [item release];
    }
    [application setMainMenu:mainMenu];
    [mainMenu release];
    [appMenu release];
    [appItem release];
}

int axyne_ui_run(const char *app_name)
{
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        axyne_install_menu(application);
        NSString *title = app_name != NULL
            ? [NSString stringWithUTF8String:app_name] : @"Axyne";
        AxyneApplicationDelegate *delegate =
            [[AxyneApplicationDelegate alloc] initWithAppName:title];
        [application setDelegate:delegate];
        [application run];
        [application setDelegate:nil];
        [delegate release];
    }
    return 0;
}
