#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>
#include "axyne/release_notes.h"
#include "axyne/release_ui.h"

static NSMutableDictionary *sWindows;
static NSColor *C(NSInteger r, NSInteger g, NSInteger b) { return [NSColor colorWithCalibratedRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:1]; }
static NSString *S(const char *s) { return s ? [NSString stringWithUTF8String:s] : @""; }
static NSImage *Asset(NSString *name) {
    NSString *path = [[NSBundle mainBundle] pathForResource:name ofType:@"png" inDirectory:@"release"];
    return path ? [[[NSImage alloc] initWithContentsOfFile:path] autorelease] : nil;
}
static NSTextField *Label(NSString *text, CGFloat size, NSColor *color, BOOL bold) {
    NSTextField *f = [[[NSTextField alloc] initWithFrame:NSZeroRect] autorelease];
    [f setStringValue:text ?: @""]; [f setEditable:NO]; [f setSelectable:NO]; [f setBordered:NO]; [f setDrawsBackground:NO];
    [f setFont:bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size]]; [f setTextColor:color]; return f;
}
static NSButton *Button(NSString *title, id target, SEL action) {
    NSButton *b = [[[NSButton alloc] initWithFrame:NSZeroRect] autorelease]; [b setTitle:title]; [b setTarget:target]; [b setAction:action]; [b setBezelStyle:NSBezelStyleRounded]; return b;
}

@interface AxyneReleaseWindow : NSObject <NSWindowDelegate> {
@public NSWindow *window; NSWindow *toast; void *owner; AxyneReleaseCatalog catalog; NSInteger selected; NSInteger filter; BOOL loading;
    NSView *sidebar; NSView *detail; NSView *rows; NSScrollView *historyScroll; NSScrollView *detailScroll; NSButton *showAfter; BOOL checkOnly; BOOL toastDismissed;
}
- (id)initWithOwner:(NSWindow *)o;
- (void)load;
- (void)applyCatalog:(AxyneReleaseCatalog *)c;
- (void)render;
- (void)showToast;
@end

static NSString *Key(NSWindow *w) { return [NSString stringWithFormat:@"%p", w]; }
static AxyneReleaseWindow *Lookup(NSWindow *w) { return [sWindows objectForKey:Key(w)]; }

@implementation AxyneReleaseWindow
- (id)initWithOwner:(NSWindow *)o {
    if ((self = [super init])) { owner = o; memset(&catalog,0,sizeof(catalog)); selected=0; filter=AXYNE_RELEASE_ALL; loading=YES;
      window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,960,640) styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable) backing:NSBackingStoreBuffered defer:NO];
      [window setTitle:@"Release Notes — Axyne"]; [window setTitlebarAppearsTransparent:YES]; [window setBackgroundColor:C(28,30,34)]; [window setDelegate:self];
      NSView *root=[window contentView]; [root setWantsLayer:YES]; root.layer.backgroundColor=C(28,30,34).CGColor;
      sidebar=[[NSView alloc] initWithFrame:NSMakeRect(0,52,220,554)]; [sidebar setWantsLayer:YES]; sidebar.layer.backgroundColor=C(22,23,26).CGColor; [root addSubview:sidebar];
      NSTextField *st=Label(@"VERSION HISTORY",11,C(139,145,155),YES); [st setFrame:NSMakeRect(16,520,190,20)]; [sidebar addSubview:st];
      historyScroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,220,520)]; [historyScroll setHasVerticalScroller:YES]; [historyScroll setDrawsBackground:NO]; [sidebar addSubview:historyScroll];
      detail=[[NSView alloc] initWithFrame:NSMakeRect(220,52,740,554)]; [detail setWantsLayer:YES]; detail.layer.backgroundColor=C(28,30,34).CGColor; [root addSubview:detail];
      detailScroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,740,554)]; [detailScroll setHasVerticalScroller:YES]; [detailScroll setDrawsBackground:NO]; [detailScroll setBorderType:NSNoBorder]; [detail addSubview:detailScroll];
      NSView *footer=[[[NSView alloc] initWithFrame:NSMakeRect(0,0,960,52)] autorelease]; [footer setWantsLayer:YES]; footer.layer.backgroundColor=C(28,30,34).CGColor; [root addSubview:footer];
      showAfter=[[NSButton alloc] initWithFrame:NSMakeRect(18,15,210,22)]; [showAfter setButtonType:NSSwitchButton]; [showAfter setTitle:@"Show release notes after update"]; [showAfter setFont:[NSFont systemFontOfSize:12]]; [showAfter setTarget:self]; [showAfter setAction:@selector(toggleShow:)]; [footer addSubview:showAfter];
      NSButton *raw=Button(@"All changes (CHANGELOG.md)",self,@selector(showRawNotes:)); [raw setFrame:NSMakeRect(670,14,195,24)]; [raw setBordered:NO]; [raw setContentTintColor:C(201,162,247)]; [footer addSubview:raw];
      NSButton *close=Button(@"Close",self,@selector(close:)); [close setFrame:NSMakeRect(860,11,84,30)]; [close setBordered:NO]; [close setWantsLayer:YES]; close.layer.backgroundColor=C(166,107,240).CGColor; [close setContentTintColor:C(19,20,23)]; [footer addSubview:close];
      NSImage *icon=Asset(@"app"); if(icon){NSImageView *iv=[[[NSImageView alloc]initWithFrame:NSMakeRect(12,614,16,16)]autorelease]; [iv setImage:icon]; [root addSubview:iv];} [[window standardWindowButton:NSWindowCloseButton] setImage:Asset(@"close") ?: [[window standardWindowButton:NSWindowCloseButton] image]];
    } return self;
}
- (void)dealloc { if(toast){[toast orderOut:nil];[toast release];} [window setDelegate:nil]; [window release]; [sidebar release]; [detail release]; [historyScroll release]; [detailScroll release]; [showAfter release]; axyne_release_catalog_clear(&catalog); [super dealloc]; }
- (void)windowWillClose:(NSNotification *)n { (void)n; [[self retain] autorelease]; if(sWindows) [sWindows removeObjectForKey:Key(window)]; }
- (void)close:(id)sender { (void)sender; [window close]; }
- (void)toggleShow:(id)sender { int v=[showAfter state]==NSControlStateValueOn; NSWindow *w=window; dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{ @autoreleasepool { axyne_release_set_show_after_update(NULL,v,NULL); dispatch_async(dispatch_get_main_queue(),^{ AxyneReleaseWindow *u=Lookup(w); if(u) u->catalog.show_after_update=v; }); } }); }
- (void)load { NSWindow *w=window; dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{ @autoreleasepool { AxyneReleaseCatalog *c=calloc(1,sizeof(*c)); axyne_release_fetch(NULL,NULL,c,NULL); dispatch_async(dispatch_get_main_queue(),^{ AxyneReleaseWindow *u=Lookup(w); if(u) [u applyCatalog:c]; else { axyne_release_catalog_clear(c); free(c); } }); } }); }
- (void)applyCatalog:(AxyneReleaseCatalog *)c { if(!c)return; axyne_release_catalog_clear(&catalog); catalog=*c; free(c); loading=NO; [showAfter setState:catalog.show_after_update?NSControlStateValueOn:NSControlStateValueOff]; [self render]; if(checkOnly && !toastDismissed) { if(toast){[toast orderOut:nil];[toast release];toast=nil;} [self showToast]; } }
- (void)selectVersion:(id)sender { selected=[sender tag]; [self render]; }
- (void)selectFilter:(id)sender { filter=[sender tag]; [self render]; }
- (void)showRawNotes:(id)sender { (void)sender; NSString *text=catalog.count?S(catalog.entries[MIN(MAX(selected,0),(NSInteger)catalog.count-1)].notes):S(axyne_release_notes_text()); NSWindow *w=[[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,700,520) styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO]autorelease]; [w setTitle:@"CHANGELOG.md — Release Notes"]; NSScrollView *sc=[[[NSScrollView alloc]initWithFrame:[[w contentView]bounds]]autorelease]; [sc setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable]; [sc setHasVerticalScroller:YES]; NSTextView *tv=[[[NSTextView alloc]initWithFrame:[[w contentView]bounds]]autorelease]; [tv setEditable:NO]; [tv setSelectable:YES]; [tv setFont:[NSFont userFixedPitchFontOfSize:12]]; [tv setTextColor:C(196,200,206)]; [tv setBackgroundColor:C(22,23,26)]; [tv setString:text]; [sc setDocumentView:tv]; [[w contentView]addSubview:sc]; [w center]; [w makeKeyAndOrderFront:nil]; }
- (void)render {
    for(NSView *v in [[historyScroll documentView] subviews]) [v removeFromSuperview];
    CGFloat listHeight=MAX(520,(catalog.count+1)*54); NSView *list=[[[NSView alloc]initWithFrame:NSMakeRect(0,0,220,listHeight)]autorelease]; [historyScroll setDocumentView:list];
    for(size_t i=0;i<catalog.count;i++){AxyneReleaseEntry *e=&catalog.entries[i]; CGFloat y=listHeight-(CGFloat)(i+1)*54; NSButton *b=Button(@"",self,@selector(selectVersion:)); [b setFrame:NSMakeRect(8,y,204,52)]; [b setTag:(NSInteger)i]; [b setBordered:NO]; [b setWantsLayer:YES]; if((NSInteger)i==selected)b.layer.backgroundColor=C(35,38,43).CGColor; [list addSubview:b]; NSTextField *v=Label(S(e->version),13,(NSInteger)i==selected?[NSColor whiteColor]:C(196,200,206),YES); [v setFrame:NSMakeRect(20,y+27,110,18)]; [list addSubview:v]; if(strcmp(e->version,axyne_release_notes_version())==0){NSImageView *check=[[[NSImageView alloc]initWithFrame:NSMakeRect(170,y+28,13,13)]autorelease];[check setImage:Asset(@"checked")];[list addSubview:check];NSTextField *installed=Label(@"Installed",9,C(201,162,247),NO);[installed setFrame:NSMakeRect(128,y+7,70,15)];[list addSubview:installed];} NSTextField *d=Label(e->published_at?S(e->published_at):@"Date unavailable",11,C(139,145,155),NO); [d setFrame:NSMakeRect(20,y+7,112,16)]; [list addSubview:d]; }
    NSView *content=[[[NSView alloc]initWithFrame:NSMakeRect(0,0,720,1000)]autorelease]; [detailScroll setDocumentView:content];
    if(catalog.count==0){ NSTextField *m=Label(loading?@"Loading release history…":@"Release history unavailable",15,C(196,200,206),NO); [m setFrame:NSMakeRect(30,470,500,30)]; [content addSubview:m]; return; }
    AxyneReleaseEntry *e=&catalog.entries[MIN(MAX(selected,0),(NSInteger)catalog.count-1)]; NSTextField *title=Label([NSString stringWithFormat:@"Axyne %@",S(e->version)],24,[NSColor whiteColor],YES); [title setFrame:NSMakeRect(32,936,600,34)]; [content addSubview:title];
    NSString *date=e->published_at?S(e->published_at):@"Date unavailable"; NSTextField *dt=Label(date,12,C(139,145,155),NO); [dt setFrame:NSMakeRect(32,910,400,20)]; [content addSubview:dt];
    NSArray *names=@[@"All",@"Feature",@"Improvement",@"Fix",@"Notice"]; AxyneReleaseCategory cats[]={AXYNE_RELEASE_ALL,AXYNE_RELEASE_FEATURE,AXYNE_RELEASE_IMPROVEMENT,AXYNE_RELEASE_FIX,AXYNE_RELEASE_NOTICE}; CGFloat fx=32; for(NSInteger j=0;j<5;j++){size_t n=j==0?e->change_count:axyne_release_category_count(e,cats[j]);NSButton *b=Button([NSString stringWithFormat:@"%@ · %zu",[names objectAtIndex:j],n],self,@selector(selectFilter:)); [b setTag:j]; [b sizeToFit]; CGFloat fw=MAX(59,NSWidth([b frame])+12); [b setFrame:NSMakeRect(fx,865,fw,28)]; fx+=fw+6; [b setState:filter==j?NSControlStateValueOn:NSControlStateValueOff]; [content addSubview:b];}
    CGFloat y=830; for(size_t i=0;i<e->change_count;i++){ AxyneReleaseChange *ch=&e->changes[i]; if(filter!=AXYNE_RELEASE_ALL && ch->category!=filter)continue; NSString *cat=@"NOTICE"; NSColor *color=C(139,145,155); if(ch->category==AXYNE_RELEASE_FEATURE){cat=@"FEATURE";color=C(201,162,247);}else if(ch->category==AXYNE_RELEASE_IMPROVEMENT){cat=@"IMPROVEMENT";color=C(111,190,164);}else if(ch->category==AXYNE_RELEASE_FIX){cat=@"FIX";color=C(236,139,122);} NSTextField *badge=Label(cat,10,color,YES); [badge setAlignment:NSTextAlignmentCenter]; [badge setWantsLayer:YES]; badge.layer.cornerRadius=4; badge.layer.backgroundColor=C(35,38,43).CGColor; [badge setFrame:NSMakeRect(32,y-20,58,20)]; [content addSubview:badge]; NSTextField *ct=Label(S(ch->title),13,[NSColor whiteColor],YES); [ct setFrame:NSMakeRect(104,y-19,600,19)]; [content addSubview:ct]; NSTextField *body=Label(S(ch->body),12,C(169,174,182),NO); [body setFrame:NSMakeRect(104,y-40,600,18)]; [body setMaximumNumberOfLines:1]; [body setLineBreakMode:NSLineBreakByWordWrapping]; [content addSubview:body]; y-=63; }
    [detailScroll.contentView scrollToPoint:NSMakePoint(0,446)]; [detailScroll reflectScrolledClipView:detailScroll.contentView]; [historyScroll.contentView scrollToPoint:NSMakePoint(0,MAX(0,listHeight-520))]; [historyScroll reflectScrolledClipView:historyScroll.contentView];
}
- (void)showToast { if(toast)return; toast=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,380,108) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO]; [toast setLevel:NSFloatingWindowLevel]; [toast setBackgroundColor:C(35,38,43)]; [toast setOpaque:YES]; [toast setHasShadow:YES]; NSView *v=[toast contentView]; [v setWantsLayer:YES]; v.layer.cornerRadius=8; v.layer.borderWidth=1; v.layer.borderColor=C(58,62,70).CGColor; NSImageView *iv=[[[NSImageView alloc]initWithFrame:NSMakeRect(16,62,28,28)]autorelease]; [iv setImage:Asset(@"app")]; [v addSubview:iv]; NSString *ver=catalog.latest_version?S(catalog.latest_version):@""; NSString *msg=loading?@"Checking for updates…":(catalog.message?S(catalog.message):(catalog.update_available?@"A new release is available.":@"You are up to date.")); NSString *heading=loading?@"Checking for updates…":[NSString stringWithFormat:@"Axyne %@",ver]; NSTextField *t=Label(heading,13,[NSColor whiteColor],YES); [t setFrame:NSMakeRect(54,73,280,20)]; [v addSubview:t]; NSTextField *m=Label(msg,12,C(169,174,182),NO); [m setFrame:NSMakeRect(54,55,290,18)]; [v addSubview:m]; if(!loading && catalog.update_available && catalog.latest_version){NSButton *again=Button(@"Don't show this version again",self,@selector(suppressVersion:)); [again setFrame:NSMakeRect(8,10,190,28)]; [again setBordered:NO]; [v addSubview:again];} NSButton *view=Button(@"View release notes",self,@selector(openHistory:)); [view setFrame:NSMakeRect(207,10,145,28)]; [view setBordered:NO]; [v addSubview:view]; NSButton *dismiss=Button(@"",self,@selector(dismissToast:)); [dismiss setFrame:NSMakeRect(352,76,20,20)]; [dismiss setImage:Asset(@"toast-close")]; [dismiss setTitle:@""]; [dismiss setBordered:NO]; [v addSubview:dismiss]; NSScreen *screen=[NSScreen mainScreen]; NSRect f=[toast frame]; f.origin=NSMakePoint(NSMaxX([screen visibleFrame])-f.size.width-18,NSMinY([screen visibleFrame])+18); [toast setFrame:f display:YES]; [toast orderFront:nil]; }
- (void)dismissToast:(id)sender { (void)sender; toastDismissed=YES; [toast orderOut:nil]; [toast release]; toast=nil; }
- (void)suppressVersion:(id)sender { (void)sender; char *v=catalog.latest_version?strdup(catalog.latest_version):strdup(""); [self dismissToast:nil]; dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{ @autoreleasepool { axyne_release_suppress(NULL,v,NULL); free(v); } }); }
- (void)openHistory:(id)sender { (void)sender; if(toast){[toast orderOut:nil];[toast release];toast=nil;} [window makeKeyAndOrderFront:nil]; [self render]; }
@end

void axyne_release_ui_open(void *owner, int check_only) {
    if(![NSThread isMainThread]) { NSWindow *w=(NSWindow *)owner; dispatch_async(dispatch_get_main_queue(),^{ axyne_release_ui_open(w,check_only); }); return; }
    if(!sWindows)sWindows=[[NSMutableDictionary alloc]init]; NSWindow *w=(NSWindow *)owner; AxyneReleaseWindow *ui=Lookup(w);
    if(!ui){ui=[[[AxyneReleaseWindow alloc]initWithOwner:w]autorelease];[sWindows setObject:ui forKey:Key(w)];[ui->window center];}
    ui->checkOnly=check_only!=0; if(check_only) ui->toastDismissed=NO;
    if(check_only){ui->loading=YES; [ui showToast]; [ui load];}
    else { [ui->window makeKeyAndOrderFront:nil]; [ui load]; }
}
void axyne_release_ui_close_owner(void *owner) { if(![NSThread isMainThread]){NSWindow *w=(NSWindow *)owner;dispatch_async(dispatch_get_main_queue(),^{axyne_release_ui_close_owner(w);});return;} if(!sWindows)return; AxyneReleaseWindow *ui=Lookup((NSWindow *)owner); if(ui){[ui->window close];[sWindows removeObjectForKey:Key((NSWindow *)owner)];} }
