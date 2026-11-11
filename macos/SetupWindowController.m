#import "SetupWindowController.h"
#import <Cocoa/Cocoa.h>
#include "engine.h"
#include "internal.h"

extern void GoTVCheckForUpdates(BOOL manual);

#ifndef NSOnState
#define NSOnState 1
#define NSOffState 0
#endif

#ifndef NSTitledWindowMask
#define NSTitledWindowMask (1 << 0)
#define NSClosableWindowMask (1 << 1)
#define NSMiniaturizableWindowMask (1 << 2)
#endif

#ifndef NSSwitchButton
#define NSSwitchButton 3
#endif

#ifndef NSMomentaryPushInButton
#define NSMomentaryPushInButton 7
#endif

#ifndef NSRoundedBezelStyle
#define NSRoundedBezelStyle 1
#endif

/* -------------------------------------------------------------------------
 * GtvDataWindow: Macro and Emoji table manager
 * ------------------------------------------------------------------------- */
@interface GtvDataWindow : NSObject <NSTableViewDataSource, NSTableViewDelegate> {
    NSWindow *window;
    NSPopUpButton *typePop;
    NSTableView *table;
    NSTableColumn *colKey;
    NSTableColumn *colVal;
    NSTextField *noteLabel;
    NSTextField *keyLabel;
    NSTextField *valLabel;
    NSTextField *keyField;
    NSTextField *valField;
    NSButton *btnAdd;
    NSButton *btnRemove;
    NSButton *btnFolder;
    BOOL emoji;
}
+ (instancetype)shared;
- (void)show;
- (void)refresh;
- (void)applyLanguage;
- (IBAction)typeChanged:(id)sender;
- (IBAction)addOrUpdate:(id)sender;
- (IBAction)removeSelected:(id)sender;
- (IBAction)openFolder:(id)sender;
- (BOOL)saveTables;
@end

@implementation GtvDataWindow

+ (instancetype)shared {
    static GtvDataWindow *shared = nil;
    if (!shared) shared = [[GtvDataWindow alloc] init];
    return shared;
}

- (void)applyLanguage {
    if (!window) return;
    [window setTitle:[NSString stringWithUTF8String:gtv_i18n("GoTV — Bảng gõ tắt & Emoji", "GoTV — Shortcuts & Emoji")]];
    NSInteger sel = [typePop indexOfSelectedItem];
    [typePop removeAllItems];
    [typePop addItemsWithTitles:[NSArray arrayWithObjects:
        [NSString stringWithUTF8String:gtv_i18n("Macro (gõ tắt)", "Macro (shortcuts)")],
        @"Emoji", nil]];
    if (sel >= 0 && sel < [typePop numberOfItems]) {
        [typePop selectItemAtIndex:sel];
    }
    [noteLabel setStringValue:[NSString stringWithUTF8String:gtv_i18n("Bấm Tab khi gõ để bung từ viết tắt / emoji.", "Press Tab while typing to expand shortcut / emoji.")]];
    [[colKey headerCell] setStringValue:[NSString stringWithUTF8String:gtv_i18n("Từ gõ", "Shortcut")]];
    [[colVal headerCell] setStringValue:[NSString stringWithUTF8String:gtv_i18n("Nội dung thay thế", "Replacement")]];
    [keyLabel setStringValue:[NSString stringWithUTF8String:gtv_i18n("Từ gõ:", "Shortcut:")]];
    [valLabel setStringValue:[NSString stringWithUTF8String:gtv_i18n("Nội dung:", "Content:")]];
    [btnAdd setTitle:[NSString stringWithUTF8String:gtv_i18n("Thêm / Sửa", "Add / Update")]];
    [btnRemove setTitle:[NSString stringWithUTF8String:gtv_i18n("Xóa chọn", "Delete")]];
    [btnFolder setTitle:[NSString stringWithUTF8String:gtv_i18n("Mở thư mục", "Open folder")]];
}

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 480, 440);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        typePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(12, 402, 170, 26) pullsDown:NO];
        [typePop setTarget:self];
        [typePop setAction:@selector(typeChanged:)];
        [view addSubview:typePop];
        [typePop release];

        noteLabel = [NSTextField labelWithString:@""];
        [noteLabel setFrame:NSMakeRect(190, 402, 278, 26)];
        [[noteLabel cell] setTruncatesLastVisibleLine:YES];
        [view addSubview:noteLabel];

        NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(12, 122, 456, 268)] autorelease];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSBezelBorder];
        table = [[NSTableView alloc] initWithFrame:[[scroll contentView] bounds]];
        colKey = [[[NSTableColumn alloc] initWithIdentifier:@"key"] autorelease];
        [colKey setWidth:150];
        colVal = [[[NSTableColumn alloc] initWithIdentifier:@"value"] autorelease];
        [table addTableColumn:colKey];
        [table addTableColumn:colVal];
        [table setDataSource:self];
        [table setDelegate:self];
        [table setAllowsMultipleSelection:NO];
        [scroll setDocumentView:table];
        [table release];
        [view addSubview:scroll];

        keyLabel = [NSTextField labelWithString:@""];
        [keyLabel setFrame:NSMakeRect(12, 88, 60, 22)];
        [view addSubview:keyLabel];
        keyField = [[NSTextField alloc] initWithFrame:NSMakeRect(78, 86, 390, 26)];
        [view addSubview:keyField];
        [keyField release];

        valLabel = [NSTextField labelWithString:@""];
        [valLabel setFrame:NSMakeRect(12, 56, 60, 22)];
        [view addSubview:valLabel];
        valField = [[NSTextField alloc] initWithFrame:NSMakeRect(78, 54, 390, 26)];
        [view addSubview:valField];
        [valField release];

        btnAdd = [[[NSButton alloc] initWithFrame:NSMakeRect(12, 12, 110, 30)] autorelease];
        [btnAdd setButtonType:NSMomentaryPushInButton];
        [btnAdd setBezelStyle:NSRoundedBezelStyle];
        [btnAdd setTarget:self];
        [btnAdd setAction:@selector(addOrUpdate:)];
        [view addSubview:btnAdd];

        btnRemove = [[[NSButton alloc] initWithFrame:NSMakeRect(130, 12, 90, 30)] autorelease];
        [btnRemove setButtonType:NSMomentaryPushInButton];
        [btnRemove setBezelStyle:NSRoundedBezelStyle];
        [btnRemove setTarget:self];
        [btnRemove setAction:@selector(removeSelected:)];
        [view addSubview:btnRemove];

        btnFolder = [[[NSButton alloc] initWithFrame:NSMakeRect(228, 12, 120, 30)] autorelease];
        [btnFolder setButtonType:NSMomentaryPushInButton];
        [btnFolder setBezelStyle:NSRoundedBezelStyle];
        [btnFolder setTarget:self];
        [btnFolder setAction:@selector(openFolder:)];
        [view addSubview:btnFolder];
    }
    [self applyLanguage];
    [self refresh];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
}

- (void)refresh {
    [table reloadData];
}

- (IBAction)typeChanged:(id)sender {
    (void)sender;
    emoji = ([typePop indexOfSelectedItem] == 1);
    [keyField setStringValue:@""];
    [valField setStringValue:@""];
    [self refresh];
}

- (BOOL)saveTables {
    GError *err = NULL;
    if (!gtv_table_save(emoji, &err)) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"GoTV"];
        NSString *defMsg = [NSString stringWithUTF8String:gtv_i18n("Không lưu được file dữ liệu.", "Could not save data file.")];
        NSString *msg = err && err->message
            ? [NSString stringWithUTF8String:err->message]
            : defMsg;
        [alert setInformativeText:(msg ? msg : defMsg)];
        [alert runModal];
        g_clear_error(&err);
        return NO;
    }
    gtv_tables_reload();
    return YES;
}

- (IBAction)addOrUpdate:(id)sender {
    (void)sender;
    NSString *ks = [[keyField stringValue] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *vs = [[valField stringValue] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (!gtv_table_set(emoji, [ks UTF8String], [vs UTF8String])) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"GoTV"];
        [alert setInformativeText:[NSString stringWithUTF8String:gtv_i18n("Khóa hoặc giá trị không hợp lệ (khóa không chứa dấu cách hay dấu =, giá trị không rỗng).", "Invalid key or value (key must not contain spaces or '=', value must not be empty).")]];
        [alert runModal];
        return;
    }
    if ([self saveTables]) {
        [keyField setStringValue:@""];
        [valField setStringValue:@""];
        [self refresh];
    }
}
    if ([self saveTables]) {
        [keyField setStringValue:@""];
        [valField setStringValue:@""];
        [self refresh];
    }
}

- (IBAction)removeSelected:(id)sender {
    (void)sender;
    NSInteger row = [table selectedRow];
    if (row < 0) return;
    const gchar *k = NULL;
    if (!gtv_table_get(emoji, (guint)row, &k, NULL) || !k) return;
    if (gtv_table_remove(emoji, k) && [self saveTables]) {
        [keyField setStringValue:@""];
        [valField setStringValue:@""];
        [self refresh];
    }
}

- (IBAction)openFolder:(id)sender {
    (void)sender;
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    if (dir) {
        NSString *ns = [NSString stringWithUTF8String:dir];
        if (ns) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns]];
        g_free(dir);
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv {
    (void)tv;
    return (NSInteger)gtv_table_count(emoji);
}

- (id)tableView:(NSTableView *)tv objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    (void)tv;
    const gchar *k = NULL, *v = NULL;
    if (!gtv_table_get(emoji, (guint)row, &k, &v)) return @"";
    BOOL wantKey = [[col identifier] isEqualToString:@"key"];
    const gchar *s = wantKey ? k : v;
    NSString *ns = s ? [NSString stringWithUTF8String:s] : nil;
    return ns ? ns : @"";
}

- (void)tableViewSelectionDidChange:(NSNotification *)n {
    (void)n;
    NSInteger row = [table selectedRow];
    if (row < 0) return;
    const gchar *k = NULL, *v = NULL;
    if (!gtv_table_get(emoji, (guint)row, &k, &v)) return;
    if (k) [keyField setStringValue:[NSString stringWithUTF8String:k]];
    if (v) [valField setStringValue:[NSString stringWithUTF8String:v]];
}

@end

/* -------------------------------------------------------------------------
 * GtvPreferencesWindow: Full preferences panel for GoTV on macOS (Apple IMK)
 * ------------------------------------------------------------------------- */
@interface GtvPreferencesWindow : NSObject {
    NSWindow *window;
    NSTextField *lLang;
    NSPopUpButton *langPop;
    NSTextField *lMode;
    NSPopUpButton *modePop;
    NSTextField *lTone;
    NSButton *modernCheck;
    NSButton *spellCheck;
    NSButton *suggestCheck;
    NSTextField *lDict;
    NSButton *viDictCheck;
    NSButton *enDictCheck;
    NSButton *cDictCheck;
    NSButton *csDictCheck;
    NSButton *tsqlDictCheck;
    NSTextField *lUtil;
    NSButton *btnData;
    NSButton *btnFolder;
    NSButton *btnUpdate;
    NSTextField *verLabel;
}
+ (instancetype)shared;
- (void)show;
- (void)loadFromConfig;
- (void)applyLanguage;
- (IBAction)settingChanged:(id)sender;
- (IBAction)openDataWindow:(id)sender;
- (IBAction)openConfigFolder:(id)sender;
- (IBAction)checkUpdates:(id)sender;
@end

@implementation GtvPreferencesWindow

+ (instancetype)shared {
    static GtvPreferencesWindow *shared = nil;
    if (!shared) shared = [[GtvPreferencesWindow alloc] init];
    return shared;
}

- (void)applyLanguage {
    if (!window) return;
    [window setTitle:[NSString stringWithUTF8String:gtv_i18n("GoTV — Cài đặt cấu hình", "GoTV — Preferences")]];
    [lLang setStringValue:[NSString stringWithUTF8String:gtv_i18n("Ngôn ngữ giao diện:", "Interface language:")]];
    [lMode setStringValue:[NSString stringWithUTF8String:gtv_i18n("Kiểu gõ chính:", "Primary input method:")]];
    [lTone setStringValue:[NSString stringWithUTF8String:gtv_i18n("Quy tắc dấu & Chính tả (Apple IMK):", "Tone rules & Spell check (Apple IMK):")]];
    [modernCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Bỏ dấu kiểu mới tự do (hòa, thúy thay vì hoà, thuý)", "Modern tone placement (hòa, thúy instead of hoà, thuý)")]];
    [spellCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Kiểm tra chính tả tiếng Việt khi gõ", "Check Vietnamese spelling while typing")]];
    [suggestCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Hiển thị gợi ý từ điển & sửa lỗi chính tả (Candidate list)", "Show candidate suggestions & spell fix (Candidate list)")]];
    [lDict setStringValue:[NSString stringWithUTF8String:gtv_i18n("Gói từ điển gợi ý từ & sửa lỗi:", "Dictionary packages:")]];
    [viDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Tiếng Việt (vi)", "Vietnamese (vi)")]];
    [enDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Tiếng Anh (en)", "English (en)")]];
    [cDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Lập trình C (c)", "C Programming (c)")]];
    [csDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("C# (csharp)", "C# (csharp)")]];
    [tsqlDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("T-SQL (tsql)", "T-SQL (tsql)")]];
    [lUtil setStringValue:[NSString stringWithUTF8String:gtv_i18n("Dữ liệu & Cập nhật:", "Data & Updates:")]];
    [btnData setTitle:[NSString stringWithUTF8String:gtv_i18n("Bảng gõ tắt & Emoji...", "Shortcuts & Emoji table...")]];
    [btnFolder setTitle:[NSString stringWithUTF8String:gtv_i18n("Mở thư mục cấu hình", "Open config folder")]];
    [btnUpdate setTitle:[NSString stringWithUTF8String:gtv_i18n("Kiểm tra cập nhật ngay...", "Check for updates now...")]];
    [verLabel setStringValue:[NSString stringWithUTF8String:gtv_i18n("GoTV · Mã nguồn mở MIT", "GoTV · Open-source MIT")]];
}

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 480, 520);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        int y = 475;

        // Group 0: Ngôn ngữ giao diện
        lLang = [NSTextField labelWithString:@""];
        [lLang setFrame:NSMakeRect(20, y, 140, 22)];
        [lLang setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lLang];

        langPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(165, y - 2, 160, 26) pullsDown:NO];
        [langPop addItemsWithTitles:[NSArray arrayWithObjects:@"Tiếng Việt", @"English", nil]];
        [langPop setTarget:self];
        [langPop setAction:@selector(settingChanged:)];
        [view addSubview:langPop];
        [langPop release];

        y -= 42;

        // Group 1: Kiểu gõ (Telex / VNI)
        lMode = [NSTextField labelWithString:@""];
        [lMode setFrame:NSMakeRect(20, y, 140, 22)];
        [lMode setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lMode];

        modePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(165, y - 2, 160, 26) pullsDown:NO];
        [modePop addItemsWithTitles:[NSArray arrayWithObjects:@"Telex", @"VNI", nil]];
        [modePop setTarget:self];
        [modePop setAction:@selector(settingChanged:)];
        [view addSubview:modePop];
        [modePop release];

        y -= 42;

        // Group 2: Tùy chọn gõ dấu & Chính tả
        lTone = [NSTextField labelWithString:@""];
        [lTone setFrame:NSMakeRect(20, y, 350, 22)];
        [lTone setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lTone];

        y -= 28;
        modernCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [modernCheck setButtonType:NSSwitchButton];
        [modernCheck setTarget:self];
        [modernCheck setAction:@selector(settingChanged:)];
        [view addSubview:modernCheck];

        y -= 26;
        spellCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [spellCheck setButtonType:NSSwitchButton];
        [spellCheck setTarget:self];
        [spellCheck setAction:@selector(settingChanged:)];
        [view addSubview:spellCheck];

        y -= 26;
        suggestCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [suggestCheck setButtonType:NSSwitchButton];
        [suggestCheck setTarget:self];
        [suggestCheck setAction:@selector(settingChanged:)];
        [view addSubview:suggestCheck];

        y -= 40;

        // Group 3: Gói từ điển gợi ý
        lDict = [NSTextField labelWithString:@""];
        [lDict setFrame:NSMakeRect(20, y, 300, 22)];
        [lDict setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lDict];

        y -= 26;
        viDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 130, 22)] autorelease];
        [viDictCheck setButtonType:NSSwitchButton];
        [viDictCheck setTarget:self];
        [viDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:viDictCheck];

        enDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(180, y, 130, 22)] autorelease];
        [enDictCheck setButtonType:NSSwitchButton];
        [enDictCheck setTarget:self];
        [enDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:enDictCheck];

        cDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(330, y, 120, 22)] autorelease];
        [cDictCheck setButtonType:NSSwitchButton];
        [cDictCheck setTarget:self];
        [cDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:cDictCheck];

        y -= 24;
        csDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 130, 22)] autorelease];
        [csDictCheck setButtonType:NSSwitchButton];
        [csDictCheck setTarget:self];
        [csDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:csDictCheck];

        tsqlDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(180, y, 130, 22)] autorelease];
        [tsqlDictCheck setButtonType:NSSwitchButton];
        [tsqlDictCheck setTarget:self];
        [tsqlDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:tsqlDictCheck];

        y -= 42;

        // Group 4: Tiện ích & Dữ liệu
        lUtil = [NSTextField labelWithString:@""];
        [lUtil setFrame:NSMakeRect(20, y, 250, 22)];
        [lUtil setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lUtil];

        y -= 34;
        btnData = [[[NSButton alloc] initWithFrame:NSMakeRect(28, y, 200, 30)] autorelease];
        [btnData setButtonType:NSMomentaryPushInButton];
        [btnData setBezelStyle:NSRoundedBezelStyle];
        [btnData setTarget:self];
        [btnData setAction:@selector(openDataWindow:)];
        [view addSubview:btnData];

        btnFolder = [[[NSButton alloc] initWithFrame:NSMakeRect(240, y, 200, 30)] autorelease];
        [btnFolder setButtonType:NSMomentaryPushInButton];
        [btnFolder setBezelStyle:NSRoundedBezelStyle];
        [btnFolder setTarget:self];
        [btnFolder setAction:@selector(openConfigFolder:)];
        [view addSubview:btnFolder];

        y -= 34;
        btnUpdate = [[[NSButton alloc] initWithFrame:NSMakeRect(28, y, 200, 30)] autorelease];
        [btnUpdate setButtonType:NSMomentaryPushInButton];
        [btnUpdate setBezelStyle:NSRoundedBezelStyle];
        [btnUpdate setTarget:self];
        [btnUpdate setAction:@selector(checkUpdates:)];
        [view addSubview:btnUpdate];

        verLabel = [NSTextField labelWithString:@""];
        [verLabel setFrame:NSMakeRect(245, y + 4, 200, 22)];
        [verLabel setTextColor:[NSColor secondaryLabelColor]];
        [view addSubview:verLabel];
    }

    [self loadFromConfig];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
}

- (void)loadFromConfig {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    GtvConfig config;
    memset(&config, 0, sizeof(config));
    gtv_config_load(&config, dir);
    g_free(dir);

    gtv_i18n_set_lang(config.ui_lang);
    [langPop selectItemAtIndex:(config.ui_lang == GTV_LANG_EN ? 1 : 0)];
    [self applyLanguage];

    [modePop selectItemAtIndex:(config.mode == GTV_VNI ? 1 : 0)];
    [modernCheck setState:(config.modern ? NSOnState : NSOffState)];
    [spellCheck setState:(config.spellcheck ? NSOnState : NSOffState)];
    [suggestCheck setState:(config.suggest_enabled ? NSOnState : NSOffState)];

    const gchar *langs = config.dicts[0] ? config.dicts : gtv_dict_get_langs();
    [viDictCheck setState:(gtv_dict_lang_enabled(langs, "vi") ? NSOnState : NSOffState)];
    [enDictCheck setState:(gtv_dict_lang_enabled(langs, "en") ? NSOnState : NSOffState)];
    [cDictCheck setState:(gtv_dict_lang_enabled(langs, "c") ? NSOnState : NSOffState)];
    [csDictCheck setState:(gtv_dict_lang_enabled(langs, "csharp") ? NSOnState : NSOffState)];
    [tsqlDictCheck setState:(gtv_dict_lang_enabled(langs, "tsql") ? NSOnState : NSOffState)];

    gtv_config_clear(&config);
}

- (IBAction)settingChanged:(id)sender {
    (void)sender;
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    GtvConfig config;
    memset(&config, 0, sizeof(config));
    gtv_config_load(&config, dir);

    config.ui_lang = ([langPop indexOfSelectedItem] == 1) ? GTV_LANG_EN : GTV_LANG_VI;
    gtv_i18n_set_lang(config.ui_lang);
    [self applyLanguage];

    config.mode = ([modePop indexOfSelectedItem] == 1) ? GTV_VNI : GTV_TELEX;
    config.modern = ([modernCheck state] == NSOnState);
    config.spellcheck = ([spellCheck state] == NSOnState);
    config.suggest_enabled = ([suggestCheck state] == NSOnState);

    // Build dicts string
    gchar *langs = g_strdup(config.dicts[0] ? config.dicts : gtv_dict_get_langs());
    gchar *t1 = gtv_dict_lang_toggle(langs, "vi", ([viDictCheck state] == NSOnState));
    g_free(langs);
    gchar *t2 = gtv_dict_lang_toggle(t1, "en", ([enDictCheck state] == NSOnState));
    g_free(t1);
    gchar *t3 = gtv_dict_lang_toggle(t2, "c", ([cDictCheck state] == NSOnState));
    g_free(t2);
    gchar *t4 = gtv_dict_lang_toggle(t3, "csharp", ([csDictCheck state] == NSOnState));
    g_free(t3);
    gchar *t5 = gtv_dict_lang_toggle(t4, "tsql", ([tsqlDictCheck state] == NSOnState));
    g_free(t4);
    g_strlcpy(config.dicts, t5 ? t5 : "vi", sizeof(config.dicts));
    g_free(t5);

    gtv_dict_set_langs(config.dicts);
    gtv_config_save(&config, dir, NULL);
    g_free(dir);
    gtv_config_clear(&config);

    [[NSNotificationCenter defaultCenter] postNotificationName:@"GoTVConfigDidChange" object:nil];
}

- (IBAction)openDataWindow:(id)sender {
    (void)sender;
    [[GtvDataWindow shared] show];
}

- (IBAction)openConfigFolder:(id)sender {
    (void)sender;
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    if (dir) {
        NSString *ns = [NSString stringWithUTF8String:dir];
        if (ns) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns]];
        g_free(dir);
    }
}

- (IBAction)checkUpdates:(id)sender {
    (void)sender;
    GoTVCheckForUpdates(YES);
}

@end

/* -------------------------------------------------------------------------
 * Public entry points
 * ------------------------------------------------------------------------- */
void GoTVShowSettings(void) {
    [[GtvPreferencesWindow shared] show];
}

void GoTVShowDataWindow(void) {
    [[GtvDataWindow shared] show];
}
