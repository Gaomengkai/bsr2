#include "bsr/core.hpp"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <dispatch/dispatch.h>

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr CGFloat kWindowWidth = 560.0;
constexpr CGFloat kWindowHeight = 332.0;
constexpr CGFloat kWindowMinWidth = 520.0;
constexpr CGFloat kWindowMinHeight = 312.0;
constexpr CGFloat kContentInset = 20.0;
constexpr CGFloat kTopInset = 28.0;
constexpr CGFloat kSectionSpacing = 14.0;
constexpr CGFloat kCardSpacing = 12.0;
constexpr CGFloat kActionSpacing = 10.0;
constexpr CGFloat kRunButtonWidth = 116.0;
constexpr CGFloat kCardHeight = 88.0;

std::string nsstring_to_utf8(NSString* value) {
    if (value == nil) {
        return {};
    }

    const char* utf8 = value.UTF8String;
    return utf8 == nullptr ? std::string() : std::string(utf8);
}

NSString* utf8_to_nsstring(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size()
                                  encoding:NSUTF8StringEncoding];
}

std::filesystem::path url_to_path(NSURL* url) {
    if (url == nil || url.path == nil) {
        return {};
    }
    return std::filesystem::path(nsstring_to_utf8(url.path));
}

bool is_directory_url(NSURL* url) {
    if (url == nil || !url.isFileURL) {
        return false;
    }

    NSNumber* is_directory = nil;
    NSError* error = nil;
    const bool ok = [url getResourceValue:&is_directory
                                   forKey:NSURLIsDirectoryKey
                                    error:&error];
    return ok && error == nil && is_directory.boolValue;
}

NSURL* first_directory_url_from_pasteboard(NSPasteboard* pasteboard) {
    if (pasteboard == nil) {
        return nil;
    }

    NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[[NSURL class]]
                                                      options:@{
                                                          NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                                      }];
    for (NSURL* url in urls) {
        if (is_directory_url(url)) {
            return url;
        }
    }
    return nil;
}

NSTextField* make_label(NSString* text, NSFont* font, NSColor* color) {
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = font;
    label.textColor = color;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

void set_hidden(NSView* view, BOOL hidden) {
    view.hidden = hidden;
}

}  // namespace

@interface RoundedPanelView : NSView

@property(nonatomic, assign, getter=isEmphasized) BOOL emphasized;

@end

@implementation RoundedPanelView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self == nil) {
        return nil;
    }

    self.translatesAutoresizingMaskIntoConstraints = NO;
    self.wantsLayer = YES;
    self.layer.cornerRadius = 14.0;
    self.layer.borderWidth = 1.0;
    [self refreshAppearance];
    return self;
}

- (void)setEmphasized:(BOOL)emphasized {
    _emphasized = emphasized;
    [self refreshAppearance];
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self refreshAppearance];
}

- (void)refreshAppearance {
    NSColor* background = [NSColor.controlBackgroundColor colorWithAlphaComponent:0.82];
    NSColor* border = self.isEmphasized ? NSColor.controlAccentColor : NSColor.separatorColor;
    self.layer.backgroundColor = background.CGColor;
    self.layer.borderColor = border.CGColor;
}

@end

@interface DirectorySelectionView : RoundedPanelView <NSDraggingDestination>

@property(nonatomic, copy) void (^onDirectoryChanged)(NSURL* url);
@property(nonatomic, strong, nullable) NSURL* directoryURL;

- (instancetype)initWithTitle:(NSString*)title placeholder:(NSString*)placeholder;
- (void)setChooseTarget:(id)target action:(SEL)action;

@end

@implementation DirectorySelectionView {
    NSTextField* _titleLabel;
    NSTextField* _placeholderLabel;
    NSPathControl* _pathControl;
    NSButton* _chooseButton;
    NSImageView* _iconView;
}

- (instancetype)initWithTitle:(NSString*)title placeholder:(NSString*)placeholder {
    self = [super initWithFrame:NSZeroRect];
    if (self == nil) {
        return nil;
    }

    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    _iconView = [[NSImageView alloc] initWithFrame:NSZeroRect];
    _iconView.translatesAutoresizingMaskIntoConstraints = NO;
    _iconView.image = [NSImage imageNamed:NSImageNameFolder];
    _iconView.contentTintColor = NSColor.secondaryLabelColor;
    _iconView.imageScaling = NSImageScaleProportionallyUpOrDown;

    _titleLabel = make_label(title,
                             [NSFont systemFontOfSize:12.5 weight:NSFontWeightSemibold],
                             NSColor.labelColor);

    _placeholderLabel = make_label(placeholder,
                                   [NSFont systemFontOfSize:12.5 weight:NSFontWeightRegular],
                                   NSColor.tertiaryLabelColor);

    _pathControl = [[NSPathControl alloc] initWithFrame:NSZeroRect];
    _pathControl.translatesAutoresizingMaskIntoConstraints = NO;
    _pathControl.pathStyle = NSPathStyleStandard;
    _pathControl.controlSize = NSControlSizeSmall;
    _pathControl.focusRingType = NSFocusRingTypeNone;
    _pathControl.hidden = YES;

    _chooseButton = [NSButton buttonWithTitle:@"选择…" target:nil action:nil];
    _chooseButton.translatesAutoresizingMaskIntoConstraints = NO;
    _chooseButton.bezelStyle = NSBezelStyleRounded;
    [_chooseButton setContentHuggingPriority:NSLayoutPriorityRequired
                              forOrientation:NSLayoutConstraintOrientationHorizontal];

    [self addSubview:_iconView];
    [self addSubview:_titleLabel];
    [self addSubview:_placeholderLabel];
    [self addSubview:_pathControl];
    [self addSubview:_chooseButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.heightAnchor constraintEqualToConstant:kCardHeight],

        [_iconView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:18.0],
        [_iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [_iconView.widthAnchor constraintEqualToConstant:28.0],
        [_iconView.heightAnchor constraintEqualToConstant:28.0],

        [_chooseButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-18.0],
        [_chooseButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

        [_titleLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:16.0],
        [_titleLabel.leadingAnchor constraintEqualToAnchor:_iconView.trailingAnchor constant:14.0],
        [_titleLabel.trailingAnchor constraintLessThanOrEqualToAnchor:_chooseButton.leadingAnchor constant:-16.0],

        [_placeholderLabel.topAnchor constraintEqualToAnchor:_titleLabel.bottomAnchor constant:5.0],
        [_placeholderLabel.leadingAnchor constraintEqualToAnchor:_titleLabel.leadingAnchor],
        [_placeholderLabel.trailingAnchor constraintLessThanOrEqualToAnchor:_chooseButton.leadingAnchor constant:-16.0],
        [_placeholderLabel.bottomAnchor constraintLessThanOrEqualToAnchor:self.bottomAnchor constant:-16.0],

        [_pathControl.topAnchor constraintEqualToAnchor:_titleLabel.bottomAnchor constant:2.0],
        [_pathControl.leadingAnchor constraintEqualToAnchor:_titleLabel.leadingAnchor],
        [_pathControl.trailingAnchor constraintLessThanOrEqualToAnchor:_chooseButton.leadingAnchor constant:-16.0],
        [_pathControl.bottomAnchor constraintLessThanOrEqualToAnchor:self.bottomAnchor constant:-14.0],
    ]];

    return self;
}

- (void)setDirectoryURL:(NSURL*)directoryURL {
    _directoryURL = directoryURL;
    _pathControl.URL = directoryURL;
    _pathControl.toolTip = directoryURL.path;
    set_hidden(_pathControl, directoryURL == nil);
    set_hidden(_placeholderLabel, directoryURL != nil);
}

- (void)setChooseTarget:(id)target action:(SEL)action {
    _chooseButton.target = target;
    _chooseButton.action = action;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSURL* url = first_directory_url_from_pasteboard(sender.draggingPasteboard);
    self.emphasized = (url != nil);
    return url == nil ? NSDragOperationNone : NSDragOperationCopy;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    NSURL* url = first_directory_url_from_pasteboard(sender.draggingPasteboard);
    self.emphasized = (url != nil);
    return url == nil ? NSDragOperationNone : NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.emphasized = NO;
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    return first_directory_url_from_pasteboard(sender.draggingPasteboard) != nil;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL* url = first_directory_url_from_pasteboard(sender.draggingPasteboard);
    self.emphasized = NO;
    if (url == nil) {
        return NO;
    }

    self.directoryURL = url;
    if (self.onDirectoryChanged != nil) {
        self.onDirectoryChanged(url);
    }
    return YES;
}

- (void)concludeDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.emphasized = NO;
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>

@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) DirectorySelectionView* subtitleSelectionView;
@property(nonatomic, strong) DirectorySelectionView* videoSelectionView;
@property(nonatomic, strong) NSButton* duplicateCheckbox;
@property(nonatomic, strong) NSButton* runButton;
@property(nonatomic, strong) NSProgressIndicator* progressIndicator;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, assign, getter=isRunning) BOOL running;

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [self buildMenu];
    [self buildWindow];
    [self refreshRunAvailability];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)buildMenu {
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];

    NSMenu* appMenu = [[NSMenu alloc] init];
    NSString* appName = NSProcessInfo.processInfo.processName;
    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"退出 %@", appName]
                                                      action:@selector(terminate:)
                                               keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appItem setSubmenu:appMenu];
    NSApp.mainMenu = menuBar;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(0.0, 0.0, kWindowWidth, kWindowHeight);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable |
                                                         NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"字幕改名器";
    self.window.titleVisibility = NSWindowTitleHidden;
    self.window.titlebarAppearsTransparent = YES;
    self.window.minSize = NSMakeSize(kWindowMinWidth, kWindowMinHeight);
    if (@available(macOS 11.0, *)) {
        self.window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
    }
    [self.window center];

    NSVisualEffectView* background = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
    background.translatesAutoresizingMaskIntoConstraints = NO;
    background.material = NSVisualEffectMaterialUnderWindowBackground;
    background.state = NSVisualEffectStateActive;
    self.window.contentView = background;

    NSTextField* headerLabel = make_label(@"选择要匹配的文件夹",
                                          [NSFont systemFontOfSize:20.0 weight:NSFontWeightSemibold],
                                          NSColor.labelColor);
    NSTextField* subtitleLabel = make_label(@"拖入文件夹，或点按“选择…”来指定位置。",
                                            [NSFont systemFontOfSize:12.5 weight:NSFontWeightRegular],
                                            NSColor.secondaryLabelColor);

    self.subtitleSelectionView = [[DirectorySelectionView alloc] initWithTitle:@"字幕"
                                                                    placeholder:@"拖入字幕文件夹"];
    self.videoSelectionView = [[DirectorySelectionView alloc] initWithTitle:@"视频"
                                                                 placeholder:@"拖入视频文件夹"];

    __weak typeof(self) weakSelf = self;
    [self.subtitleSelectionView setChooseTarget:self action:@selector(chooseSubtitleDirectory:)];
    [self.videoSelectionView setChooseTarget:self action:@selector(chooseVideoDirectory:)];
    self.subtitleSelectionView.onDirectoryChanged = ^(NSURL* url) {
        (void)url;
        [weakSelf updateStatusMessage:@"已填入字幕文件夹"];
        [weakSelf refreshRunAvailability];
    };
    self.videoSelectionView.onDirectoryChanged = ^(NSURL* url) {
        (void)url;
        [weakSelf updateStatusMessage:@"已填入视频文件夹"];
        [weakSelf refreshRunAvailability];
    };

    self.duplicateCheckbox = [NSButton checkboxWithTitle:@"同时复制到视频目录" target:nil action:nil];
    self.duplicateCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.duplicateCheckbox.state = NSControlStateValueOn;

    self.progressIndicator = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    self.progressIndicator.translatesAutoresizingMaskIntoConstraints = NO;
    self.progressIndicator.style = NSProgressIndicatorStyleSpinning;
    self.progressIndicator.controlSize = NSControlSizeSmall;
    self.progressIndicator.displayedWhenStopped = NO;
    [self.progressIndicator setHidden:YES];

    self.statusLabel = make_label(@"选择两个文件夹后即可开始。",
                                  [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular],
                                  NSColor.secondaryLabelColor);
    self.statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;

    NSStackView* statusStack = [NSStackView stackViewWithViews:@[
        self.progressIndicator,
        self.statusLabel
    ]];
    statusStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    statusStack.spacing = 6.0;
    statusStack.alignment = NSLayoutAttributeCenterY;
    statusStack.translatesAutoresizingMaskIntoConstraints = NO;
    [statusStack setContentHuggingPriority:NSLayoutPriorityDefaultLow
                            forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.runButton = [NSButton buttonWithTitle:@"开始处理" target:self action:@selector(runRename:)];
    self.runButton.translatesAutoresizingMaskIntoConstraints = NO;
    self.runButton.bezelStyle = NSBezelStyleRounded;
    self.runButton.keyEquivalent = @"\r";
    [self.runButton.widthAnchor constraintEqualToConstant:kRunButtonWidth].active = YES;

    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    spacer.translatesAutoresizingMaskIntoConstraints = NO;
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* actionRow = [NSStackView stackViewWithViews:@[
        self.duplicateCheckbox,
        spacer,
        statusStack,
        self.runButton
    ]];
    actionRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    actionRow.alignment = NSLayoutAttributeCenterY;
    actionRow.spacing = kActionSpacing;
    actionRow.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView* stack = [NSStackView stackViewWithViews:@[
        headerLabel,
        subtitleLabel,
        self.subtitleSelectionView,
        self.videoSelectionView,
        actionRow
    ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = kSectionSpacing;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [stack setCustomSpacing:6.0 afterView:headerLabel];
    [stack setCustomSpacing:kCardSpacing afterView:self.subtitleSelectionView];
    [stack setCustomSpacing:18.0 afterView:self.videoSelectionView];

    [self.window.contentView addSubview:stack];

    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:self.window.contentView.leadingAnchor constant:kContentInset],
        [stack.trailingAnchor constraintEqualToAnchor:self.window.contentView.trailingAnchor constant:-kContentInset],
        [stack.topAnchor constraintEqualToAnchor:self.window.contentView.topAnchor constant:kTopInset],
        [stack.bottomAnchor constraintLessThanOrEqualToAnchor:self.window.contentView.bottomAnchor constant:-kContentInset],

        [self.subtitleSelectionView.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
        [self.videoSelectionView.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
        [actionRow.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
    ]];
}

- (void)chooseSubtitleDirectory:(id)sender {
    (void)sender;
    [self chooseDirectoryForSelectionView:self.subtitleSelectionView
                                   status:@"已选择字幕文件夹"];
}

- (void)chooseVideoDirectory:(id)sender {
    (void)sender;
    [self chooseDirectoryForSelectionView:self.videoSelectionView
                                   status:@"已选择视频文件夹"];
}

- (void)chooseDirectoryForSelectionView:(DirectorySelectionView*)selectionView
                                 status:(NSString*)statusText {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.canCreateDirectories = NO;
    panel.prompt = @"选择";
    panel.directoryURL = selectionView.directoryURL;

    __weak typeof(self) weakSelf = self;
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || panel.URL == nil) {
            return;
        }
        selectionView.directoryURL = panel.URL;
        [weakSelf updateStatusMessage:statusText];
        [weakSelf refreshRunAvailability];
    }];
}

- (void)refreshRunAvailability {
    const BOOL ready = (self.subtitleSelectionView.directoryURL != nil &&
                        self.videoSelectionView.directoryURL != nil &&
                        !self.isRunning);
    self.runButton.enabled = ready;
    if (!self.isRunning && !ready) {
        self.statusLabel.stringValue = @"选择两个文件夹后即可开始。";
    }
}

- (void)setRunning:(BOOL)running {
    _running = running;
    self.subtitleSelectionView.alphaValue = running ? 0.85 : 1.0;
    self.videoSelectionView.alphaValue = running ? 0.85 : 1.0;
    self.duplicateCheckbox.enabled = !running;
    if (running) {
        [self.progressIndicator startAnimation:nil];
        self.progressIndicator.hidden = NO;
    } else {
        [self.progressIndicator stopAnimation:nil];
        self.progressIndicator.hidden = YES;
    }
    [self refreshRunAvailability];
}

- (void)updateStatusMessage:(NSString*)message {
    if (message.length == 0) {
        return;
    }
    self.statusLabel.stringValue = message;
}

- (void)showError:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleCritical;
    alert.messageText = @"处理失败";
    alert.informativeText = message;
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)runRename:(id)sender {
    (void)sender;

    NSURL* subtitleURL = self.subtitleSelectionView.directoryURL;
    NSURL* videoURL = self.videoSelectionView.directoryURL;
    if (subtitleURL == nil || videoURL == nil) {
        [self updateStatusMessage:@"选择两个文件夹后即可开始。"];
        return;
    }

    const std::filesystem::path subtitlePath = url_to_path(subtitleURL);
    const std::filesystem::path videoPath = url_to_path(videoURL);
    if (!std::filesystem::exists(subtitlePath) || !std::filesystem::is_directory(subtitlePath)) {
        [self showError:@"字幕文件夹不存在。"];
        [self updateStatusMessage:@"字幕文件夹不可用"];
        return;
    }
    if (!std::filesystem::exists(videoPath) || !std::filesystem::is_directory(videoPath)) {
        [self showError:@"视频文件夹不存在。"];
        [self updateStatusMessage:@"视频文件夹不可用"];
        return;
    }

    const bool willCopy = (self.duplicateCheckbox.state == NSControlStateValueOn);
    self.running = YES;
    [self updateStatusMessage:@"正在匹配字幕…"];

    __weak typeof(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString* errorMessage = nil;
        NSString* statusMessage = @"没有找到可匹配的字幕";

        try {
            const std::vector<std::filesystem::path> created =
                bsr::core::rename_subtitles(videoPath, subtitlePath, willCopy);
            if (!created.empty()) {
                statusMessage = [NSString stringWithFormat:@"已处理 %lu 个文件",
                                 static_cast<unsigned long>(created.size())];
            }
        } catch (const std::exception& ex) {
            errorMessage = utf8_to_nsstring(ex.what());
            if (errorMessage == nil) {
                errorMessage = @"发生未知错误。";
            }
            statusMessage = @"处理失败";
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            weakSelf.running = NO;
            [weakSelf updateStatusMessage:statusMessage];
            if (errorMessage != nil) {
                [weakSelf showError:errorMessage];
            }
        });
    });
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
