// macOS shell: NSApplication, window, CAMetalLayer view, events, menus, clipboard, file panels.
// Everything else lives in App (C++). The view renders only when App reports it is dirty,
// driven by a display link that pauses while idle (fanless M3: no busy rendering).
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "app/App.hpp"

#include <sys/sysctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using calc::App;
using calc::CursorKind;
using calc::Key;
using calc::Mods;

namespace {

struct Options {
    std::vector<std::string> exprs;
    std::string screenshot, open;
    int width = 1280, height = 800;
    int select = 0;
    bool hover = false;
    float hoverX = 0, hoverY = 0;
    bool view = false;
    double vx0 = -10, vx1 = 10, vy0 = -10, vy1 = 10;
    bool launchBench = false, check = false, perfHud = false, keypad = false;
    std::vector<std::string> images;
    int styleMenuRow = 0;
    std::vector<std::pair<float, float>> clicks;   // simulated clicks (screenshots / parity checks)
};

Options parseArgs(int argc, const char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--expr") o.exprs.push_back(next());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--open") o.open = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--select") o.select = std::atoi(next().c_str());
        else if (a == "--hover") { o.hover = std::sscanf(next().c_str(), "%f,%f", &o.hoverX, &o.hoverY) == 2; }
        else if (a == "--view") { o.view = std::sscanf(next().c_str(), "%lf,%lf,%lf,%lf", &o.vx0, &o.vx1, &o.vy0, &o.vy1) == 4; }
        else if (a == "--launch-bench") o.launchBench = true;
        else if (a == "--check") o.check = true;
        else if (a == "--perf-hud") o.perfHud = true;
        else if (a == "--keypad") o.keypad = true;
        else if (a == "--image") o.images.push_back(next());
        else if (a == "--style-menu") o.styleMenuRow = std::atoi(next().c_str());
        else if (a == "--click") {
            float cx, cy;
            if (std::sscanf(next().c_str(), "%f,%f", &cx, &cy) == 2) o.clicks.push_back({cx, cy});
        }
    }
    return o;
}

double processAgeMs() {
    struct kinfo_proc info;
    size_t len = sizeof info;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    if (sysctl(mib, 4, &info, &len, nullptr, 0) != 0) return -1;
    struct timeval start = info.kp_proc.p_starttime, now;
    gettimeofday(&now, nullptr);
    return (now.tv_sec - start.tv_sec) * 1000.0 + (now.tv_usec - start.tv_usec) / 1000.0;
}

std::string metallibPath() {
    NSString* p = [[NSBundle mainBundle] pathForResource:@"default" ofType:@"metallib"];
    return p ? std::string(p.UTF8String) : std::string();
}

std::u32string toU32(NSString* s) {
    std::u32string out;
    NSUInteger n = s.length;
    for (NSUInteger i = 0; i < n; ++i) {
        unichar c = [s characterAtIndex:i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            unichar lo = [s characterAtIndex:++i];
            out += char32_t(0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00));
        } else {
            out += char32_t(c);
        }
    }
    return out;
}

void applyOptions(App& app, const Options& o) {
    if (!o.open.empty()) {
        std::string err;
        if (!app.loadFile(o.open, err)) std::fprintf(stderr, "%s\n", err.c_str());
    }
    for (auto& e : o.exprs) app.addExpression(e);
    for (auto& im : o.images)
        if (!app.addImage(im)) std::fprintf(stderr, "couldn't load image %s\n", im.c_str());
    if (o.keypad) app.toggleKeypad();
    if (o.view) app.setView(o.vx0, o.vx1, o.vy0, o.vy1);
    if (o.select > 0) app.focusRow(o.select - 1);
    if (o.styleMenuRow > 0) app.openStyleMenu(o.styleMenuRow - 1, -1);
    app.perfHud = o.perfHud;
}

} // namespace

// ---- view --------------------------------------------------------------------------------------

@interface GraphView : NSView <NSWindowDelegate>
@property(nonatomic) App* app;
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) CADisplayLink* displayLink;
@property(nonatomic, strong) NSTimer* blinkTimer;
@property(nonatomic) BOOL launchBench;
@property(nonatomic) BOOL benchCheck;
@end

@implementation GraphView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }

- (CALayer*)makeBackingLayer {
    CAMetalLayer* l = [CAMetalLayer layer];
    l.device = self.device;
    l.pixelFormat = MTLPixelFormatBGRA8Unorm;
    l.framebufferOnly = YES;
    l.maximumDrawableCount = 3;
    l.displaySyncEnabled = YES;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    l.colorspace = cs;
    CGColorSpaceRelease(cs);
    l.opaque = YES;
    l.needsDisplayOnBoundsChange = NO;
    return l;
}

- (CAMetalLayer*)metalLayer { return (CAMetalLayer*)self.layer; }

- (void)startDisplayLink {
    self.displayLink = [self displayLinkWithTarget:self selector:@selector(onFrame:)];
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    self.displayLink.paused = YES;
}

- (void)requestFrame {
    if (self.app->needsRedraw()) self.displayLink.paused = NO;
    [self updateBlinkTimer];
    self.window.documentEdited = self.app->modified();
    NSString* t = [NSString stringWithUTF8String:self.app->title().c_str()];
    if (![self.window.title isEqualToString:t]) self.window.title = t;
}

- (void)updateBlinkTimer {
    bool want = self.app->wantsBlinkTimer();
    if (want && !self.blinkTimer) {
        __weak GraphView* weakSelf = self;
        self.blinkTimer = [NSTimer scheduledTimerWithTimeInterval:calc::theme::CursorBlinkSeconds
                                                          repeats:YES
                                                            block:^(NSTimer*) {
            GraphView* s = weakSelf;
            if (!s) return;
            s.app->blinkTick();
            [s requestFrame];
        }];
        self.blinkTimer.tolerance = 0.05;
    } else if (!want && self.blinkTimer) {
        [self.blinkTimer invalidate];
        self.blinkTimer = nil;
    }
}

- (void)renderNow {
    CAMetalLayer* l = [self metalLayer];
    id<CAMetalDrawable> d = [l nextDrawable];
    if (!d) return;
    bool wait = self.launchBench;
    self.app->render((__bridge MTL::Texture*)d.texture, (__bridge CA::MetalDrawable*)d, wait);
    if (self.launchBench) {
        double ms = processAgeMs();
        bool ok = ms >= 0 && ms < 150.0;
        std::printf("launch_to_first_frame_ms=%.1f target<150 %s\n", ms, ok ? "OK" : "MISS");
        std::fflush(stdout);
        std::exit(self.benchCheck && !ok ? 1 : 0);
    }
}

- (void)onFrame:(CADisplayLink*)link {
    if (self.app->needsRedraw()) [self renderNow];
    else link.paused = YES;
}

- (void)updateSize {
    NSRect b = self.bounds;
    CGFloat scale = self.window ? self.window.backingScaleFactor : 2.0;
    [self metalLayer].contentsScale = scale;
    [self metalLayer].drawableSize = CGSizeMake(b.size.width * scale, b.size.height * scale);
    self.app->setSize(float(b.size.width), float(b.size.height), float(scale));
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (!self.app) return;
    [self updateSize];
    [self renderNow];  // live resize: draw synchronously so the content never lags the frame
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    if (!self.app) return;
    [self updateSize];
    [self requestFrame];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                               NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                         owner:self
                                                      userInfo:nil]];
}

// ---- pointer ----

- (NSPoint)pt:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }

- (Mods)mods:(NSEvent*)e {
    Mods m;
    NSEventModifierFlags f = e.modifierFlags;
    m.shift = f & NSEventModifierFlagShift;
    m.cmd = f & NSEventModifierFlagCommand;
    m.alt = f & NSEventModifierFlagOption;
    m.ctrl = f & NSEventModifierFlagControl;
    return m;
}

- (void)updateCursorAt:(NSPoint)p {
    switch (self.app->cursorAt(p.x, p.y)) {
    case CursorKind::IBeam: [[NSCursor IBeamCursor] set]; break;
    case CursorKind::Pointer: [[NSCursor pointingHandCursor] set]; break;
    case CursorKind::Grab: [[NSCursor resizeLeftRightCursor] set]; break;
    case CursorKind::Grabbing: [[NSCursor closedHandCursor] set]; break;
    default: [[NSCursor arrowCursor] set]; break;
    }
}

- (void)mouseDown:(NSEvent*)e {
    NSPoint p = [self pt:e];
    if (self.app->isWindowDragRegion(p.x, p.y)) {
        if (e.clickCount == 2) [self.window performZoom:nil];
        else [self.window performWindowDragWithEvent:e];
        return;
    }
    self.app->mouseDown(p.x, p.y, int(e.clickCount), [self mods:e]);
    [self updateCursorAt:p];
    [self requestFrame];
}

- (void)rightMouseDown:(NSEvent*)e {
    NSPoint p = [self pt:e];
    Mods m = [self mods:e];
    m.ctrl = true;  // right-click on a colour icon cycles its colour
    self.app->mouseDown(p.x, p.y, 1, m);
    self.app->mouseUp(p.x, p.y);
    [self requestFrame];
}

- (void)mouseDragged:(NSEvent*)e {
    NSPoint p = [self pt:e];
    self.app->mouseDragged(p.x, p.y);
    [self updateCursorAt:p];
    [self requestFrame];
}

- (void)mouseUp:(NSEvent*)e {
    NSPoint p = [self pt:e];
    self.app->mouseUp(p.x, p.y);
    [self updateCursorAt:p];
    [self requestFrame];
}

- (void)mouseMoved:(NSEvent*)e {
    NSPoint p = [self pt:e];
    self.app->mouseMoved(p.x, p.y);
    [self updateCursorAt:p];
    [self requestFrame];
}

- (void)mouseExited:(NSEvent*)e {
    self.app->mouseExited();
    [[NSCursor arrowCursor] set];
    [self requestFrame];
}

- (void)scrollWheel:(NSEvent*)e {
    NSPoint p = [self pt:e];
    self.app->scroll(p.x, p.y, float(e.scrollingDeltaX), float(e.scrollingDeltaY), e.hasPreciseScrollingDeltas);
    [self requestFrame];
}

- (void)magnifyWithEvent:(NSEvent*)e {
    NSPoint p = [self pt:e];
    self.app->magnify(p.x, p.y, float(e.magnification));
    [self requestFrame];
}

// ---- keyboard ----

- (void)keyDown:(NSEvent*)e {
    Mods m = [self mods:e];
    if (m.cmd) { [super keyDown:e]; return; }
    Key k = Key::Char;
    switch (e.keyCode) {
    case 123: k = Key::Left; break;
    case 124: k = Key::Right; break;
    case 125: k = Key::Down; break;
    case 126: k = Key::Up; break;
    case 51: k = Key::Backspace; break;
    case 117: k = Key::Delete; break;
    case 36: case 76: k = Key::Enter; break;
    case 53: k = Key::Escape; break;
    case 48: k = Key::Tab; break;
    case 115: k = Key::Home; break;
    case 119: k = Key::End; break;
    default: break;
    }
    if (k == Key::Char) {
        std::u32string text = toU32(e.characters ?: @"");
        std::u32string printable;
        for (char32_t c : text)
            if (c >= 0x20 && c != 0x7F && (c < 0xF700 || c > 0xF8FF)) printable += c;
        if (printable.empty()) return;
        self.app->key(Key::Char, m, printable);
    } else {
        self.app->key(k, m);
    }
    [self requestFrame];
}

// ---- menu actions ----

- (void)undo:(id)s { self.app->undo(); [self requestFrame]; }
- (void)redo:(id)s { self.app->redo(); [self requestFrame]; }
- (void)selectAll:(id)s { self.app->selectAll(); [self requestFrame]; }
- (void)copy:(id)s {
    std::string t = self.app->copyText();
    if (t.empty()) return;
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] setString:[NSString stringWithUTF8String:t.c_str()] forType:NSPasteboardTypeString];
}
- (void)cut:(id)s { [self copy:s]; self.app->cut(); [self requestFrame]; }
- (void)paste:(id)s {
    NSString* t = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (t) self.app->pasteText(t.UTF8String);
    [self requestFrame];
}
- (void)zoomIn:(id)s { self.app->zoomIn(); [self requestFrame]; }
- (void)zoomOut:(id)s { self.app->zoomOut(); [self requestFrame]; }
- (void)zoomReset:(id)s { self.app->resetZoom(); [self requestFrame]; }
- (void)toggleKeypad:(id)s { self.app->toggleKeypad(); [self requestFrame]; }
- (void)insertImage:(id)s { if (self.app->onRequestImage) self.app->onRequestImage(); }
- (void)togglePerfHud:(id)s { self.app->perfHud = !self.app->perfHud; self.app->setNeedsRedraw(); [self requestFrame]; }
- (void)newDocument:(id)s { self.app->newGraph(); [self requestFrame]; }

- (void)openDocument:(id)s {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.allowedContentTypes = @[ UTTypeJSON ];
    if ([p runModal] != NSModalResponseOK) return;
    std::string err;
    if (!self.app->loadFile(p.URL.path.UTF8String, err)) [self showError:err];
    [self requestFrame];
}

- (BOOL)saveTo:(NSString*)path {
    std::string err;
    if (!self.app->saveFile(path.UTF8String, err)) { [self showError:err]; return NO; }
    [self requestFrame];
    return YES;
}

- (void)saveDocumentAs:(id)s {
    NSSavePanel* p = [NSSavePanel savePanel];
    p.allowedContentTypes = @[ UTTypeJSON ];
    p.nameFieldStringValue = [NSString stringWithUTF8String:(self.app->title() + ".json").c_str()];
    if ([p runModal] == NSModalResponseOK) [self saveTo:p.URL.path];
}

- (void)saveDocument:(id)s {
    if (self.app->filePath().empty()) [self saveDocumentAs:s];
    else [self saveTo:[NSString stringWithUTF8String:self.app->filePath().c_str()]];
}

- (void)showError:(const std::string&)msg {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = [NSString stringWithUTF8String:msg.c_str()];
    [a runModal];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(undo:)) return self.app->canUndo();
    if (item.action == @selector(redo:)) return self.app->canRedo();
    return YES;
}

@end

// ---- app delegate ------------------------------------------------------------------------------

// Device + App::init (pipelines, font warm-up) run on a background thread while AppKit starts.
struct EarlyInit {
    dispatch_group_t group = dispatch_group_create();
    id<MTLDevice> device;
    bool ok = false;
    std::string error;
};

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic) EarlyInit* early;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) GraphView* view;
@property(nonatomic) App* app;
@property(nonatomic) Options* options;
@end

@implementation AppDelegate

- (void)applicationWillFinishLaunching:(NSNotification*)n {
    if (self.options->launchBench) std::printf("  stage willFinishLaunching %.1f ms\n", processAgeMs());
}

- (NSMenuItem*)item:(NSString*)title action:(SEL)a key:(NSString*)k mods:(NSEventModifierFlags)m {
    NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:title action:a keyEquivalent:k];
    i.keyEquivalentModifierMask = m;
    return i;
}

- (void)buildMenu {
    NSMenu* bar = [[NSMenu alloc] init];
    const auto cmd = NSEventModifierFlagCommand;
    const auto cmdShift = NSEventModifierFlagCommand | NSEventModifierFlagShift;

    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"GraphingCalc"];
    [appMenu addItem:[self item:@"About GraphingCalc" action:@selector(orderFrontStandardAboutPanel:) key:@"" mods:0]];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:[self item:@"Hide GraphingCalc" action:@selector(hide:) key:@"h" mods:cmd]];
    [appMenu addItem:[self item:@"Quit GraphingCalc" action:@selector(terminate:) key:@"q" mods:cmd]];

    NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItem:[self item:@"New Graph" action:@selector(newDocument:) key:@"n" mods:cmd]];
    [file addItem:[self item:@"Open…" action:@selector(openDocument:) key:@"o" mods:cmd]];
    [file addItem:[NSMenuItem separatorItem]];
    [file addItem:[self item:@"Save" action:@selector(saveDocument:) key:@"s" mods:cmd]];
    [file addItem:[self item:@"Save As…" action:@selector(saveDocumentAs:) key:@"s" mods:cmdShift]];
    [file addItem:[NSMenuItem separatorItem]];
    [file addItem:[self item:@"Close Window" action:@selector(performClose:) key:@"w" mods:cmd]];

    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItem:[self item:@"Undo" action:@selector(undo:) key:@"z" mods:cmd]];
    [edit addItem:[self item:@"Redo" action:@selector(redo:) key:@"z" mods:cmdShift]];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItem:[self item:@"Cut" action:@selector(cut:) key:@"x" mods:cmd]];
    [edit addItem:[self item:@"Copy" action:@selector(copy:) key:@"c" mods:cmd]];
    [edit addItem:[self item:@"Paste" action:@selector(paste:) key:@"v" mods:cmd]];
    [edit addItem:[self item:@"Select All" action:@selector(selectAll:) key:@"a" mods:cmd]];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItem:[self item:@"Insert Image…" action:@selector(insertImage:) key:@"i" mods:cmdShift]];

    NSMenu* view = [[NSMenu alloc] initWithTitle:@"View"];
    [view addItem:[self item:@"Zoom In" action:@selector(zoomIn:) key:@"=" mods:cmd]];
    [view addItem:[self item:@"Zoom Out" action:@selector(zoomOut:) key:@"-" mods:cmd]];
    [view addItem:[self item:@"Default Zoom" action:@selector(zoomReset:) key:@"0" mods:cmd]];
    [view addItem:[NSMenuItem separatorItem]];
    [view addItem:[self item:@"Show Keypad" action:@selector(toggleKeypad:) key:@"k" mods:cmd]];
    [view addItem:[self item:@"Performance Overlay" action:@selector(togglePerfHud:) key:@"p"
                        mods:NSEventModifierFlagCommand | NSEventModifierFlagOption]];
    [view addItem:[self item:@"Enter Full Screen" action:@selector(toggleFullScreen:) key:@"f"
                        mods:NSEventModifierFlagCommand | NSEventModifierFlagControl]];

    NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
    [window addItem:[self item:@"Minimize" action:@selector(performMiniaturize:) key:@"m" mods:cmd]];
    [window addItem:[self item:@"Zoom" action:@selector(performZoom:) key:@"" mods:0]];

    for (NSMenu* m in @[ appMenu, file, edit, view, window ]) {
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:m.title action:nil keyEquivalent:@""];
        top.submenu = m;
        [bar addItem:top];
    }
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = window;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
    if (self.options->launchBench) std::printf("  stage didFinishLaunching %.1f ms\n", processAgeMs());
    [self buildMenu];
    const Options& o = *self.options;
    NSRect frame = NSMakeRect(0, 0, o.width, o.height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                              NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView;
    self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    self.window.titlebarAppearsTransparent = YES;
    self.window.titleVisibility = NSWindowTitleHidden;
    self.window.toolbar = [[NSToolbar alloc] initWithIdentifier:@"main"];
    self.window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
    self.window.backgroundColor = [NSColor colorWithSRGBRed:0x2a / 255.0 green:0x2a / 255.0 blue:0x2a / 255.0 alpha:1];
    self.window.minSize = NSMakeSize(640, 420);
    self.window.tabbingMode = NSWindowTabbingModeDisallowed;
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];  // light traffic-light glyphs on the dark header
    [self.window center];
    self.window.frameAutosaveName = @"GraphingCalcMain";

    dispatch_group_wait(self.early->group, DISPATCH_TIME_FOREVER);
    self.view = [[GraphView alloc] initWithFrame:frame];
    self.view.device = self.early->device;
    self.view.wantsLayer = YES;
    self.view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
    self.view.launchBench = o.launchBench;
    self.view.benchCheck = o.check;

    std::string err = self.early->error;
    if (o.launchBench) std::printf("  stage window (+early init joined) %.1f ms\n", processAgeMs());
    if (!self.early->ok) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"GraphingCalc couldn't start";
        a.informativeText = [NSString stringWithUTF8String:err.c_str()];
        [a runModal];
        [NSApp terminate:nil];
        return;
    }
    self.view.app = self.app;
    self.window.contentView = self.view;
    __weak GraphView* weakView = self.view;
    // Interactive: heavy curves refine in the background while the old samples are shown.
    self.app->graph().asyncSampling = true;
    self.app->onRedrawNeeded = [weakView] {
        GraphView* v = weakView;
        if (v) [v requestFrame];
    };
    self.app->onRequestImage = [weakView] {
        GraphView* v = weakView;
        if (!v) return;
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.allowedContentTypes = @[ UTTypeImage ];
        if ([p runModal] != NSModalResponseOK) return;
        if (!v.app->addImage(p.URL.path.UTF8String)) [v showError:"Couldn't open that image"];
        [v requestFrame];
    };
    applyOptions(*self.app, o);
    [self.view updateSize];
    [self.view startDisplayLink];
    [self.view renderNow];  // first frame before the window appears: no flash of empty content
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
    [self.view requestFrame];
}

- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename {
    std::string err;
    if (!self.app->loadFile(filename.UTF8String, err)) return NO;
    [self.view requestFrame];
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    if (!self.app->modified()) return NSTerminateNow;
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"Save changes to this graph?";
    [a addButtonWithTitle:@"Save"];
    [a addButtonWithTitle:@"Don't Save"];
    [a addButtonWithTitle:@"Cancel"];
    NSModalResponse r = [a runModal];
    if (r == NSAlertFirstButtonReturn) {
        [self.view saveDocument:nil];
        return self.app->modified() ? NSTerminateCancel : NSTerminateNow;
    }
    return r == NSAlertSecondButtonReturn ? NSTerminateNow : NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }

@end

// ---- entry -------------------------------------------------------------------------------------

int main(int argc, const char** argv) {
    @autoreleasepool {
        Options opts = parseArgs(argc, argv);
        if (opts.launchBench) std::printf("  stage main %.1f ms\n", processAgeMs());
        static App app;

        if (!opts.screenshot.empty()) {
            // Headless: render one frame offscreen at 2x and write a PNG.
            id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
            std::string err;
            if (!app.init((__bridge MTL::Device*)dev, metallibPath(), err)) {
                std::fprintf(stderr, "init failed: %s\n", err.c_str());
                return 1;
            }
            app.setSize(float(opts.width), float(opts.height), 2.0f);
            applyOptions(app, opts);
            if (opts.select <= 0) app.focusRow(-1);
            for (auto [cx, cy] : opts.clicks) {
                app.screenshot("/dev/null");  // lay out first so the click hits real geometry
                app.mouseDown(cx, cy, 1, {});
                app.mouseUp(cx, cy);
            }
            if (opts.hover) app.hoverAt(opts.hoverX, opts.hoverY);
            if (!app.screenshot(opts.screenshot)) {
                std::fprintf(stderr, "failed to write %s\n", opts.screenshot.c_str());
                return 1;
            }
            return 0;
        }

        static EarlyInit early;
        dispatch_group_async(early.group, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
            early.device = MTLCreateSystemDefaultDevice();
            early.ok = app.init((__bridge MTL::Device*)early.device, metallibPath(), early.error);
            if (early.ok) app.warmUp(2.0f);
        });
        NSApplication* nsapp = [NSApplication sharedApplication];
        if (opts.launchBench) std::printf("  stage sharedApplication %.1f ms\n", processAgeMs());
        nsapp.activationPolicy = NSApplicationActivationPolicyRegular;
        if (opts.launchBench) std::printf("  stage activationPolicy %.1f ms\n", processAgeMs());
        AppDelegate* del = [[AppDelegate alloc] init];
        del.app = &app;
        del.options = &opts;
        del.early = &early;
        nsapp.delegate = del;
        [nsapp run];
    }
    return 0;
}
