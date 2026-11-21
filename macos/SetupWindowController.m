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
 * GtvAppDictsWindow: Per-application dictionary configuration
 * ------------------------------------------------------------------------- */
@interface GtvAppDictsWindow : NSObject <NSTableViewDataSource, NSTableViewDelegate> {
    NSWindow *window;
    NSTableView *table;
    NSPopUpButton *appPop;
    NSButton *checks[5];
    GPtrArray *rules;
}
+ (instancetype)shared;
- (void)show;
- (void)refresh;
- (void)populateApps;
- (IBAction)addOrUpdate:(id)sender;
- (IBAction)removeSelected:(id)sender;
@end

@implementation GtvAppDictsWindow

+ (instancetype)shared {
    static GtvAppDictsWindow *shared = nil;
    if (!shared) shared = [[GtvAppDictsWindow alloc] init];
    return shared;
}

- (void)dealloc {
    if (rules) g_ptr_array_unref(rules);
    [super dealloc];
}

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 500, 410);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        NSTextField *note = [NSTextField labelWithString:[NSString stringWithUTF8String:gtv_i18n(
            "Chọn ứng dụng từ danh sách bên dưới và đánh dấu bộ từ điển tương ứng:",
            "Select an app and choose its dictionary packs:")]];
        [note setFrame:NSMakeRect(16, 372, 468, 22)];
        [view addSubview:note];

        NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(16, 174, 468, 190)] autorelease];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSBezelBorder];

        table = [[[NSTableView alloc] initWithFrame:[[scroll contentView] bounds]] autorelease];
        NSTableColumn *colApp = [[[NSTableColumn alloc] initWithIdentifier:@"app"] autorelease];
        [[colApp headerCell] setStringValue:[NSString stringWithUTF8String:gtv_i18n("Ứng dụng (Bundle ID)", "Application (Bundle ID)")]];
        [colApp setWidth:250];
        [table addTableColumn:colApp];

        NSTableColumn *colDict = [[[NSTableColumn alloc] initWithIdentifier:@"dicts"] autorelease];
        [[colDict headerCell] setStringValue:[NSString stringWithUTF8String:gtv_i18n("Bộ từ điển", "Dictionary Packs")]];
        [colDict setWidth:200];
        [table addTableColumn:colDict];

        [table setDataSource:self];
        [table setDelegate:self];
        [scroll setDocumentView:table];
        [view addSubview:scroll];

        appPop = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, 134, 468, 26) pullsDown:NO] autorelease];
        [view addSubview:appPop];

        const char *titles[5] = {
            gtv_i18n("Tiếng Việt", "Vietnamese"), "T-SQL", "C / C++", "C# (.NET)", gtv_i18n("Tiếng Anh", "English")
        };
        for (int i = 0; i < 5; i++) {
            int col = i % 3;
            int row = i / 3;
            checks[i] = [[[NSButton alloc] initWithFrame:NSMakeRect(16 + col * 156, 100 - row * 24, 150, 22)] autorelease];
            [checks[i] setButtonType:NSSwitchButton];
            [checks[i] setTitle:[NSString stringWithUTF8String:titles[i]]];
            [checks[i] setState:(i == 0 ? NSOnState : NSOffState)];
            [view addSubview:checks[i]];
        }

        NSButton *btnAdd = [[[NSButton alloc] initWithFrame:NSMakeRect(16, 18, 166, 30)] autorelease];
        [btnAdd setTitle:[NSString stringWithUTF8String:gtv_i18n("Thêm / Cập nhật", "Add / Update")]];
        [btnAdd setButtonType:NSMomentaryPushInButton];
        [btnAdd setBezelStyle:NSRoundedBezelStyle];
        if ([btnAdd respondsToSelector:@selector(setBezelColor:)]) {
            [btnAdd setBezelColor:[NSColor colorWithCalibratedRed:0.827 green:0.184 blue:0.184 alpha:1.0]];
        }
        [btnAdd setTarget:self];
        [btnAdd setAction:@selector(addOrUpdate:)];
        [view addSubview:btnAdd];

        NSButton *btnDel = [[[NSButton alloc] initWithFrame:NSMakeRect(192, 18, 136, 30)] autorelease];
        [btnDel setTitle:[NSString stringWithUTF8String:gtv_i18n("Xóa đã chọn", "Delete selected")]];
        [btnDel setButtonType:NSMomentaryPushInButton];
        [btnDel setBezelStyle:NSRoundedBezelStyle];
        [btnDel setTarget:self];
        [btnDel setAction:@selector(removeSelected:)];
        [view addSubview:btnDel];
    }
    [window setTitle:[NSString stringWithUTF8String:gtv_i18n("GoTV — Từ điển theo từng ứng dụng", "GoTV — Per-app dictionaries")]];
    GtvConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    gchar *cfg_dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&cfg, cfg_dir);
    g_free(cfg_dir);
    BOOL isLight = (cfg.ui_theme == GTV_THEME_LIGHT);
    gtv_config_clear(&cfg);
    if (@available(macOS 10.14, *)) {
        [window setAppearance:[NSAppearance appearanceNamed:(isLight ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua)]];
    }
    [window setBackgroundColor:(isLight
        ? [NSColor colorWithCalibratedRed:0.980 green:0.969 blue:0.949 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.094 green:0.086 blue:0.090 alpha:1.0])];
    [self refresh];
    [window center];
    [window makeKeyAndOrderFront:nil];
}

- (void)populateApps {
    if (!appPop) return;
    [appPop removeAllItems];
    NSMutableDictionary *entries = [NSMutableDictionary dictionary];

    for (NSRunningApplication *ra in [[NSWorkspace sharedWorkspace] runningApplications]) {
        if ([ra activationPolicy] != NSApplicationActivationPolicyRegular) continue;
        NSString *bid = [[ra bundleIdentifier] lowercaseString];
        if (!bid || [bid length] == 0) continue;
        NSString *name = [ra localizedName];
        NSString *label = (name && [name length] > 0)
            ? [NSString stringWithFormat:@"%@ (%@)", name, bid]
            : bid;
        if (![entries objectForKey:bid]) [entries setObject:label forKey:bid];
    }

    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *dirs = [NSArray arrayWithObjects:
        @"/Applications",
        @"/System/Applications",
        @"/System/Applications/Utilities",
        [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"],
        nil];
    for (NSString *dir in dirs) {
        NSArray *items = [fm contentsOfDirectoryAtPath:dir error:nil];
        for (NSString *item in items) {
            if (![[item pathExtension] isEqualToString:@"app"]) continue;
            NSString *full = [dir stringByAppendingPathComponent:item];
            NSBundle *b = [NSBundle bundleWithPath:full];
            NSString *bid = [[b bundleIdentifier] lowercaseString];
            if (!bid || [bid length] == 0 || [entries objectForKey:bid]) continue;
            NSString *disp = [b objectForInfoDictionaryKey:@"CFBundleDisplayName"];
            if (!disp || [disp length] == 0) disp = [b objectForInfoDictionaryKey:@"CFBundleName"];
            if (!disp || [disp length] == 0) disp = [item stringByDeletingPathExtension];
            [entries setObject:[NSString stringWithFormat:@"%@ (%@)", disp, bid] forKey:bid];
        }
    }

    if (rules) {
        for (guint i = 0; i < rules->len; i++) {
            GtvAppDictRule *r = (GtvAppDictRule *)rules->pdata[i];
            if (!r || !r->app_id) continue;
            NSString *bid = [[NSString stringWithUTF8String:r->app_id] lowercaseString];
            if (bid && ![entries objectForKey:bid]) {
                [entries setObject:bid forKey:bid];
            }
        }
    }

    NSArray *sortedKeys = [[entries allKeys] sortedArrayUsingComparator:^NSComparisonResult(NSString *k1, NSString *k2) {
        return [[entries objectForKey:k1] localizedCaseInsensitiveCompare:[entries objectForKey:k2]];
    }];

    for (NSString *bid in sortedKeys) {
        NSString *label = [entries objectForKey:bid];
        [appPop addItemWithTitle:label];
        NSMenuItem *mi = [appPop lastItem];
        [mi setRepresentedObject:bid];
    }
}

- (void)refresh {
    if (rules) g_ptr_array_unref(rules);
    rules = gtv_app_dicts_load(NULL);
    [self populateApps];
    [table reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv {
    (void)tv;
    return rules ? (NSInteger)rules->len : 0;
}

- (id)tableView:(NSTableView *)tv objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    (void)tv;
    if (!rules || row < 0 || (guint)row >= rules->len) return @"";
    GtvAppDictRule *r = (GtvAppDictRule *)rules->pdata[row];
    if ([[col identifier] isEqualToString:@"app"]) {
        return r->app_id ? [NSString stringWithUTF8String:r->app_id] : @"";
    }
    return r->dicts ? [NSString stringWithUTF8String:r->dicts] : @"";
}

- (void)tableViewSelectionDidChange:(NSNotification *)note {
    (void)note;
    NSInteger row = [table selectedRow];
    if (!rules || row < 0 || (guint)row >= rules->len) return;
    GtvAppDictRule *r = (GtvAppDictRule *)rules->pdata[row];
    if (r->app_id) {
        NSString *target = [[NSString stringWithUTF8String:r->app_id] lowercaseString];
        BOOL found = NO;
        for (NSMenuItem *mi in [appPop itemArray]) {
            if ([[mi representedObject] isEqualToString:target]) {
                [appPop selectItem:mi];
                found = YES;
                break;
            }
        }
        if (!found) {
            [appPop addItemWithTitle:target];
            [[appPop lastItem] setRepresentedObject:target];
            [appPop selectItem:[appPop lastItem]];
        }
    }
    const char *ids[5] = { "vi", "tsql", "c", "csharp", "en" };
    for (int i = 0; i < 5; i++) {
        [checks[i] setState:(gtv_dict_lang_enabled(r->dicts, ids[i]) ? NSOnState : NSOffState)];
    }
}

- (IBAction)addOrUpdate:(id)sender {
    (void)sender;
    NSString *selBid = [[appPop selectedItem] representedObject];
    const gchar *raw = selBid ? [selBid UTF8String] : NULL;
    gchar *norm = gtv_app_dict_normalize_id(raw);
    if (!norm) return;
    const char *ids[5] = { "vi", "tsql", "c", "csharp", "en" };
    GString *sel = g_string_new("");
    for (int i = 0; i < 5; i++) {
        if ([checks[i] state] == NSOnState) {
            if (sel->len > 0) g_string_append_c(sel, ',');
            g_string_append(sel, ids[i]);
        }
    }
    if (sel->len == 0) g_string_assign(sel, "vi");
    if (!rules) rules = gtv_app_dicts_load(NULL);
    gboolean updated = FALSE;
    for (guint i = 0; i < rules->len; i++) {
        GtvAppDictRule *r = (GtvAppDictRule *)rules->pdata[i];
        if (g_strcmp0(r->app_id, norm) == 0) {
            g_free(r->dicts);
            r->dicts = g_string_free(sel, FALSE);
            updated = TRUE;
            break;
        }
    }
    if (!updated) {
        GtvAppDictRule *r = g_new0(GtvAppDictRule, 1);
        r->app_id = norm;
        r->dicts = g_string_free(sel, FALSE);
        g_ptr_array_add(rules, r);
    } else {
        g_free(norm);
    }
    gtv_app_dicts_save(rules, NULL, NULL);
    [self refresh];
}

- (IBAction)removeSelected:(id)sender {
    (void)sender;
    NSInteger row = [table selectedRow];
    if (!rules || row < 0 || (guint)row >= rules->len) return;
    g_ptr_array_remove_index(rules, (guint)row);
    gtv_app_dicts_save(rules, NULL, NULL);
    [self refresh];
}

@end

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
        if ([btnAdd respondsToSelector:@selector(setBezelColor:)]) {
            [btnAdd setBezelColor:[NSColor colorWithCalibratedRed:0.827 green:0.184 blue:0.184 alpha:1.0]];
        }
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
    GtvConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    gchar *cfg_dir = gtv_user_path(g_get_user_config_dir(), NULL);
    gtv_config_load(&cfg, cfg_dir);
    g_free(cfg_dir);
    BOOL isLight = (cfg.ui_theme == GTV_THEME_LIGHT);
    gtv_config_clear(&cfg);
    if (@available(macOS 10.14, *)) {
        [window setAppearance:[NSAppearance appearanceNamed:(isLight ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua)]];
    }
    [window setBackgroundColor:(isLight
        ? [NSColor colorWithCalibratedRed:0.980 green:0.969 blue:0.949 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.094 green:0.086 blue:0.090 alpha:1.0])];
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
    NSTabView *tabView;
    NSButton *navBtns[5];
    NSBox *navActivePill;
    NSBox *navAccentBar;
    NSTextField *pageTitle;

    /* General tab controls */
    NSTextField *gModeTitle;
    NSSwitch *telexSwitch;
    NSSwitch *vniSwitch;
    NSTextField *gDictTitle;
    NSTextField *gDictSub;
    NSSwitch *suggestGenSwitch;
    NSButton *gBtnDictUpdate;
    NSTextField *gMacroTitle;
    NSTextField *gMacroSub;
    NSButton *gBtnMacro;
    NSTextField *gEmojiTitle;
    NSTextField *gEmojiSub;
    NSButton *gBtnEmoji;
    NSTextField *lLang;
    NSPopUpButton *langPop;
    NSPopUpButton *themePop;
    NSBox *sbBg;
    NSMutableArray *cards;
    BOOL isLightTheme;

    /* Typing Modes tab */
    NSTextField *lMode;
    NSPopUpButton *modePop;
    NSTextField *lTone;
    NSButton *modernCheck;
    NSButton *spellCheck;

    /* Dictionaries tab */
    NSButton *suggestCheck;
    NSTextField *lDict;
    NSButton *viDictCheck;
    NSButton *enDictCheck;
    NSButton *cDictCheck;
    NSButton *csDictCheck;
    NSButton *tsqlDictCheck;
    NSButton *btnAppDicts;

    /* Shortcuts & About tabs */
    NSTextField *lUtil;
    NSButton *btnData;
    NSButton *btnFolder;
    NSButton *btnUpdate;
    NSTextField *verLabel;
    NSInteger activeTab;
}
+ (instancetype)shared;
- (void)show;
- (void)loadFromConfig;
- (void)applyLanguage;
- (IBAction)navClicked:(id)sender;
- (IBAction)telexSwitchChanged:(id)sender;
- (IBAction)vniSwitchChanged:(id)sender;
- (IBAction)suggestGenChanged:(id)sender;
- (IBAction)settingChanged:(id)sender;
- (IBAction)openDataWindow:(id)sender;
- (IBAction)openAppDictsWindow:(id)sender;
- (IBAction)openConfigFolder:(id)sender;
- (IBAction)checkUpdates:(id)sender;
@end

@implementation GtvPreferencesWindow

+ (instancetype)shared {
    static GtvPreferencesWindow *shared = nil;
    if (!shared) shared = [[GtvPreferencesWindow alloc] init];
    return shared;
}

- (NSColor *)cBg {
    return isLightTheme
        ? [NSColor colorWithCalibratedRed:0.980 green:0.969 blue:0.949 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.094 green:0.086 blue:0.090 alpha:1.0];
}
- (NSColor *)cSidebar {
    return isLightTheme
        ? [NSColor colorWithCalibratedRed:0.953 green:0.937 blue:0.910 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.075 green:0.067 blue:0.071 alpha:1.0];
}
- (NSColor *)cCard {
    return isLightTheme
        ? [NSColor colorWithCalibratedRed:1.000 green:1.000 blue:1.000 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.129 green:0.118 blue:0.125 alpha:1.0];
}
- (NSColor *)cCardBorder {
    return isLightTheme
        ? [NSColor colorWithCalibratedRed:0.906 green:0.898 blue:0.894 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.196 green:0.176 blue:0.188 alpha:1.0];
}
/* Primary brand accent matching GoTV logo (#D32F2F) */
- (NSColor *)cAccent { return [NSColor colorWithCalibratedRed:0.827 green:0.184 blue:0.184 alpha:1.0]; }
- (NSColor *)cNavSel {
    return isLightTheme
        ? [NSColor colorWithCalibratedRed:0.996 green:0.886 blue:0.886 alpha:1.0]
        : [NSColor colorWithCalibratedRed:0.173 green:0.149 blue:0.157 alpha:1.0];
}

- (void)applyTheme {
    if (!window) return;
    [window setAppearance:[NSAppearance appearanceNamed:(isLightTheme ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua)]];
    [window setBackgroundColor:[self cBg]];
    [sbBg setFillColor:[self cSidebar]];
    [sbBg setBorderColor:[self cCardBorder]];
    [navActivePill setFillColor:[self cNavSel]];
    [navAccentBar setFillColor:[self cAccent]];
    for (NSBox *card in cards) {
        [card setFillColor:[self cCard]];
        [card setBorderColor:[self cCardBorder]];
    }
}

- (NSBox *)makeCardWithFrame:(NSRect)frame {
    NSBox *card = [[[NSBox alloc] initWithFrame:frame] autorelease];
    [card setBoxType:NSBoxCustom];
    [card setBorderType:NSLineBorder];
    [card setTitlePosition:NSNoTitle];
    [card setCornerRadius:10.0];
    [card setBorderWidth:1.0];
    [card setFillColor:[self cCard]];
    [card setBorderColor:[self cCardBorder]];
    if (!cards) cards = [[NSMutableArray alloc] init];
    [cards addObject:card];
    return card;
}

- (NSTextField *)makeLabel:(NSRect)frame size:(CGFloat)sz bold:(BOOL)bold secondary:(BOOL)sec {
    NSTextField *lbl = [NSTextField labelWithString:@""];
    [lbl setFrame:frame];
    [lbl setFont:(bold ? [NSFont boldSystemFontOfSize:sz] : [NSFont systemFontOfSize:sz])];
    [lbl setTextColor:(sec ? [NSColor secondaryLabelColor] : [NSColor labelColor])];
    return lbl;
}

- (void)selectTab:(NSInteger)idx {
    if (idx < 0 || idx >= 5) idx = 0;
    activeTab = idx;
    [tabView selectTabViewItemAtIndex:idx];
    NSRect br = [navBtns[idx] frame];
    [navActivePill setFrame:br];
    [navAccentBar setFrame:NSMakeRect(br.origin.x + 2, br.origin.y + 8, 3, br.size.height - 16)];
    [self applyLanguage];
}

- (IBAction)navClicked:(id)sender {
    [self selectTab:[(NSButton *)sender tag]];
}

- (void)applyLanguage {
    if (!window) return;
    [window setTitle:[NSString stringWithUTF8String:gtv_i18n("GoTV — Cài đặt", "GoTV — Settings")]];
    const char *navVi[5] = { "Tổng quan", "Kiểu gõ", "Từ điển", "Phím tắt", "Thông tin" };
    const char *navEn[5] = { "General", "Typing Modes", "Dictionaries", "Shortcuts", "About" };
    const char *hdrVi[5] = { "Cài đặt GoTV", "Kiểu gõ & Chính tả", "Từ điển & Gợi ý", "Phím tắt & Dữ liệu", "Thông tin GoTV" };
    const char *hdrEn[5] = { "GoTV Settings", "Typing Modes", "Dictionaries", "Shortcuts & Data", "About GoTV" };
    for (int i = 0; i < 5; i++) {
        [navBtns[i] setTitle:[NSString stringWithUTF8String:gtv_i18n(navVi[i], navEn[i])]];
    }
    [pageTitle setStringValue:[NSString stringWithUTF8String:gtv_i18n(hdrVi[activeTab], hdrEn[activeTab])]];

    [gModeTitle setStringValue:[NSString stringWithUTF8String:gtv_i18n("Kiểu gõ", "Input method")]];
    [gDictTitle setStringValue:[NSString stringWithUTF8String:gtv_i18n("Từ điển offline", "Offline dictionary")]];
    [gDictSub setStringValue:[NSString stringWithUTF8String:gtv_i18n("Gợi ý từ thông minh 100% offline", "100% offline smart word suggestions")]];
    [gBtnDictUpdate setTitle:[NSString stringWithUTF8String:gtv_i18n("Tải xuống/Cập nhật", "Download/Update")]];
    [gMacroTitle setStringValue:[NSString stringWithUTF8String:gtv_i18n("Phím tắt macro", "Macro shortcuts")]];
    [gMacroSub setStringValue:[NSString stringWithUTF8String:gtv_i18n("Bấm Tab để bung từ gõ tắt", "Press Tab to expand shortcuts")]];
    [gBtnMacro setTitle:[NSString stringWithUTF8String:gtv_i18n("Cấu hình", "Configure")]];
    [gEmojiTitle setStringValue:[NSString stringWithUTF8String:gtv_i18n("Bảng emoji & Ký tự", "Emoji & Symbols")]];
    [gEmojiSub setStringValue:[NSString stringWithUTF8String:gtv_i18n("Quản lý bảng emoji và ký tự đặc biệt", "Manage emoji and symbol shortcuts")]];
    [gBtnEmoji setTitle:[NSString stringWithUTF8String:gtv_i18n("Mở bảng...", "Open...")]];

    [lLang setStringValue:[NSString stringWithUTF8String:gtv_i18n("Giao diện & Ngôn ngữ", "Theme & Language")]];
    [lMode setStringValue:[NSString stringWithUTF8String:gtv_i18n("Kiểu gõ chính:", "Primary input method:")]];
    [lTone setStringValue:[NSString stringWithUTF8String:gtv_i18n("Quy tắc dấu & Chính tả (Apple IMK):", "Tone rules & Spell check (Apple IMK):")]];
    [modernCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Bỏ dấu kiểu mới (hòa, thúy thay vì hoà, thuý)", "Modern tone placement (hòa, thúy instead of hoà, thuý)")]];
    [spellCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Kiểm tra chính tả tiếng Việt khi gõ", "Check Vietnamese spelling while typing")]];
    [suggestCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Hiển thị gợi ý từ điển & sửa lỗi chính tả", "Show candidate suggestions & spell fix")]];
    [lDict setStringValue:[NSString stringWithUTF8String:gtv_i18n("Gói từ điển gợi ý từ & sửa lỗi:", "Dictionary packages:")]];
    [viDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Tiếng Việt (vi)", "Vietnamese (vi)")]];
    [enDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Tiếng Anh (en)", "English (en)")]];
    [cDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("Lập trình C (c)", "C Programming (c)")]];
    [csDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("C# (csharp)", "C# (csharp)")]];
    [tsqlDictCheck setTitle:[NSString stringWithUTF8String:gtv_i18n("T-SQL (tsql)", "T-SQL (tsql)")]];
    [btnAppDicts setTitle:[NSString stringWithUTF8String:gtv_i18n("Theo từng ứng dụng...", "Per-app...")]];
    [lUtil setStringValue:[NSString stringWithUTF8String:gtv_i18n("Bảng gõ tắt (Macro) & Emoji", "Macro & Emoji Tables")]];
    [btnData setTitle:[NSString stringWithUTF8String:gtv_i18n("Cấu hình...", "Configure...")]];
    [btnFolder setTitle:[NSString stringWithUTF8String:gtv_i18n("Mở thư mục cấu hình", "Open config folder")]];
    [btnUpdate setTitle:[NSString stringWithUTF8String:gtv_i18n("Kiểm tra cập nhật...", "Check for updates...")]];
    [verLabel setStringValue:[NSString stringWithUTF8String:gtv_i18n("GoTV · Thuần Cocoa/C · Mã nguồn mở MIT", "GoTV · Native Cocoa/C · MIT Open Source")]];
}

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 740, 480);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setReleasedWhenClosed:NO];
        [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
        [window setBackgroundColor:[self cBg]];
        [window setTitlebarAppearsTransparent:YES];

        NSView *root = [window contentView];

        /* Left Sidebar */
        sbBg = [[[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 194, 480)] autorelease];
        [sbBg setBoxType:NSBoxCustom];
        [sbBg setBorderType:NSLineBorder];
        [sbBg setTitlePosition:NSNoTitle];
        [sbBg setFillColor:[self cSidebar]];
        [sbBg setBorderColor:[self cCardBorder]];
        [sbBg setBorderWidth:1.0];
        [root addSubview:sbBg];

        NSTextField *brand = [self makeLabel:NSMakeRect(18, 434, 158, 26) size:16 bold:YES secondary:NO];
        [brand setStringValue:@"GoTV"];
        [root addSubview:brand];

        navActivePill = [[[NSBox alloc] initWithFrame:NSMakeRect(10, 382, 174, 34)] autorelease];
        [navActivePill setBoxType:NSBoxCustom];
        [navActivePill setBorderType:NSNoBorder];
        [navActivePill setTitlePosition:NSNoTitle];
        [navActivePill setCornerRadius:6.0];
        [navActivePill setFillColor:[self cNavSel]];
        [root addSubview:navActivePill];

        navAccentBar = [[[NSBox alloc] initWithFrame:NSMakeRect(12, 390, 3, 18)] autorelease];
        [navAccentBar setBoxType:NSBoxCustom];
        [navAccentBar setBorderType:NSNoBorder];
        [navAccentBar setTitlePosition:NSNoTitle];
        [navAccentBar setCornerRadius:1.5];
        [navAccentBar setFillColor:[self cAccent]];
        [root addSubview:navAccentBar];

        for (int i = 0; i < 5; i++) {
            NSButton *nb = [[[NSButton alloc] initWithFrame:NSMakeRect(10, 382 - i * 40, 174, 34)] autorelease];
            [nb setBordered:NO];
            [nb setButtonType:NSMomentaryChangeButton];
            [nb setAlignment:NSTextAlignmentLeft];
            [nb setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium]];
            [nb setTag:i];
            [nb setTarget:self];
            [nb setAction:@selector(navClicked:)];
            navBtns[i] = nb;
            [root addSubview:nb];
        }

        /* Right Content Area */
        pageTitle = [self makeLabel:NSMakeRect(222, 428, 480, 32) size:21 bold:YES secondary:NO];
        [root addSubview:pageTitle];

        tabView = [[[NSTabView alloc] initWithFrame:NSMakeRect(214, 16, 510, 404)] autorelease];
        [tabView setTabViewType:NSNoTabsNoBorder];
        [tabView setDrawsBackground:NO];
        [root addSubview:tabView];

        /* Tab 0: General */
        NSTabViewItem *ti0 = [[[NSTabViewItem alloc] initWithIdentifier:@"0"] autorelease];
        NSView *v0 = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 510, 404)] autorelease];
        [ti0 setView:v0];
        NSBox *cMode = [self makeCardWithFrame:NSMakeRect(8, 332, 490, 56)];
        [v0 addSubview:cMode];
        gModeTitle = [self makeLabel:NSMakeRect(24, 349, 160, 22) size:13 bold:YES secondary:NO];
        [v0 addSubview:gModeTitle];
        NSTextField *tlxL = [self makeLabel:NSMakeRect(240, 349, 44, 22) size:13 bold:NO secondary:NO];
        [tlxL setStringValue:@"Telex"];
        [v0 addSubview:tlxL];
        telexSwitch = [[[NSSwitch alloc] initWithFrame:NSMakeRect(286, 347, 38, 24)] autorelease];
        [telexSwitch setTarget:self];
        [telexSwitch setAction:@selector(telexSwitchChanged:)];
        [v0 addSubview:telexSwitch];
        NSTextField *vniL = [self makeLabel:NSMakeRect(348, 349, 34, 22) size:13 bold:NO secondary:NO];
        [vniL setStringValue:@"VNI"];
        [v0 addSubview:vniL];
        vniSwitch = [[[NSSwitch alloc] initWithFrame:NSMakeRect(384, 347, 38, 24)] autorelease];
        [vniSwitch setTarget:self];
        [vniSwitch setAction:@selector(vniSwitchChanged:)];
        [v0 addSubview:vniSwitch];

        NSBox *cDict = [self makeCardWithFrame:NSMakeRect(8, 258, 490, 64)];
        [v0 addSubview:cDict];
        gDictTitle = [self makeLabel:NSMakeRect(24, 294, 240, 20) size:13 bold:YES secondary:NO];
        [v0 addSubview:gDictTitle];
        gDictSub = [self makeLabel:NSMakeRect(24, 272, 240, 18) size:11 bold:NO secondary:YES];
        [v0 addSubview:gDictSub];
        suggestGenSwitch = [[[NSSwitch alloc] initWithFrame:NSMakeRect(286, 278, 38, 24)] autorelease];
        [suggestGenSwitch setTarget:self];
        [suggestGenSwitch setAction:@selector(suggestGenChanged:)];
        [v0 addSubview:suggestGenSwitch];
        gBtnDictUpdate = [[[NSButton alloc] initWithFrame:NSMakeRect(336, 274, 148, 30)] autorelease];
        [gBtnDictUpdate setBezelStyle:NSRoundedBezelStyle];
        [gBtnDictUpdate setTarget:self];
        [gBtnDictUpdate setAction:@selector(checkUpdates:)];
        [v0 addSubview:gBtnDictUpdate];

        NSBox *cMac = [self makeCardWithFrame:NSMakeRect(8, 190, 490, 58)];
        [v0 addSubview:cMac];
        gMacroTitle = [self makeLabel:NSMakeRect(24, 222, 320, 20) size:13 bold:YES secondary:NO];
        [v0 addSubview:gMacroTitle];
        gMacroSub = [self makeLabel:NSMakeRect(24, 202, 320, 18) size:11 bold:NO secondary:YES];
        [v0 addSubview:gMacroSub];
        gBtnMacro = [[[NSButton alloc] initWithFrame:NSMakeRect(376, 204, 108, 30)] autorelease];
        [gBtnMacro setBezelStyle:NSRoundedBezelStyle];
        [gBtnMacro setTarget:self];
        [gBtnMacro setAction:@selector(openDataWindow:)];
        [v0 addSubview:gBtnMacro];

        NSBox *cEmo = [self makeCardWithFrame:NSMakeRect(8, 122, 490, 58)];
        [v0 addSubview:cEmo];
        gEmojiTitle = [self makeLabel:NSMakeRect(24, 154, 320, 20) size:13 bold:YES secondary:NO];
        [v0 addSubview:gEmojiTitle];
        gEmojiSub = [self makeLabel:NSMakeRect(24, 134, 320, 18) size:11 bold:NO secondary:YES];
        [v0 addSubview:gEmojiSub];
        gBtnEmoji = [[[NSButton alloc] initWithFrame:NSMakeRect(376, 136, 108, 30)] autorelease];
        [gBtnEmoji setBezelStyle:NSRoundedBezelStyle];
        [gBtnEmoji setTarget:self];
        [gBtnEmoji setAction:@selector(openDataWindow:)];
        [v0 addSubview:gBtnEmoji];

        NSBox *cLang = [self makeCardWithFrame:NSMakeRect(8, 54, 490, 58)];
        [v0 addSubview:cLang];
        lLang = [self makeLabel:NSMakeRect(24, 72, 180, 22) size:13 bold:YES secondary:NO];
        [v0 addSubview:lLang];
        themePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(224, 70, 110, 26) pullsDown:NO];
        [themePop addItemsWithTitles:[NSArray arrayWithObjects:@"Dark", @"Light", nil]];
        [themePop setTarget:self];
        [themePop setAction:@selector(settingChanged:)];
        [v0 addSubview:themePop];
        [themePop release];
        langPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(344, 70, 140, 26) pullsDown:NO];
        [langPop addItemsWithTitles:[NSArray arrayWithObjects:@"Tiếng Việt", @"English", nil]];
        [langPop setTarget:self];
        [langPop setAction:@selector(settingChanged:)];
        [v0 addSubview:langPop];
        [langPop release];
        [tabView addTabViewItem:ti0];

        /* Tab 1: Typing Modes */
        NSTabViewItem *ti1 = [[[NSTabViewItem alloc] initWithIdentifier:@"1"] autorelease];
        NSView *v1 = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 510, 404)] autorelease];
        [ti1 setView:v1];
        [v1 addSubview:[self makeCardWithFrame:NSMakeRect(8, 320, 490, 68)]];
        lMode = [self makeLabel:NSMakeRect(24, 344, 180, 22) size:13 bold:YES secondary:NO];
        [v1 addSubview:lMode];
        modePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(324, 340, 160, 26) pullsDown:NO];
        [modePop addItemsWithTitles:[NSArray arrayWithObjects:@"Telex", @"VNI", nil]];
        [modePop setTarget:self];
        [modePop setAction:@selector(settingChanged:)];
        [v1 addSubview:modePop];
        [modePop release];

        [v1 addSubview:[self makeCardWithFrame:NSMakeRect(8, 194, 490, 114)]];
        lTone = [self makeLabel:NSMakeRect(24, 276, 440, 22) size:13 bold:YES secondary:NO];
        [v1 addSubview:lTone];
        modernCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(24, 242, 450, 22)] autorelease];
        [modernCheck setButtonType:NSSwitchButton];
        [modernCheck setTarget:self];
        [modernCheck setAction:@selector(settingChanged:)];
        [v1 addSubview:modernCheck];
        spellCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(24, 212, 450, 22)] autorelease];
        [spellCheck setButtonType:NSSwitchButton];
        [spellCheck setTarget:self];
        [spellCheck setAction:@selector(settingChanged:)];
        [v1 addSubview:spellCheck];
        [tabView addTabViewItem:ti1];

        /* Tab 2: Dictionaries */
        NSTabViewItem *ti2 = [[[NSTabViewItem alloc] initWithIdentifier:@"2"] autorelease];
        NSView *v2 = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 510, 404)] autorelease];
        [ti2 setView:v2];
        [v2 addSubview:[self makeCardWithFrame:NSMakeRect(8, 174, 490, 214)]];
        suggestCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(24, 352, 450, 22)] autorelease];
        [suggestCheck setButtonType:NSSwitchButton];
        [suggestCheck setTarget:self];
        [suggestCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:suggestCheck];
        lDict = [self makeLabel:NSMakeRect(24, 318, 300, 22) size:13 bold:YES secondary:NO];
        [v2 addSubview:lDict];
        viDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(28, 286, 140, 22)] autorelease];
        [viDictCheck setButtonType:NSSwitchButton];
        [viDictCheck setTarget:self];
        [viDictCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:viDictCheck];
        enDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(184, 286, 140, 22)] autorelease];
        [enDictCheck setButtonType:NSSwitchButton];
        [enDictCheck setTarget:self];
        [enDictCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:enDictCheck];
        cDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(338, 286, 140, 22)] autorelease];
        [cDictCheck setButtonType:NSSwitchButton];
        [cDictCheck setTarget:self];
        [cDictCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:cDictCheck];
        csDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(28, 254, 140, 22)] autorelease];
        [csDictCheck setButtonType:NSSwitchButton];
        [csDictCheck setTarget:self];
        [csDictCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:csDictCheck];
        tsqlDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(184, 254, 140, 22)] autorelease];
        [tsqlDictCheck setButtonType:NSSwitchButton];
        [tsqlDictCheck setTarget:self];
        [tsqlDictCheck setAction:@selector(settingChanged:)];
        [v2 addSubview:tsqlDictCheck];
        btnAppDicts = [[[NSButton alloc] initWithFrame:NSMakeRect(24, 194, 200, 30)] autorelease];
        [btnAppDicts setBezelStyle:NSRoundedBezelStyle];
        [btnAppDicts setTarget:self];
        [btnAppDicts setAction:@selector(openAppDictsWindow:)];
        [v2 addSubview:btnAppDicts];
        [tabView addTabViewItem:ti2];

        /* Tab 3: Shortcuts */
        NSTabViewItem *ti3 = [[[NSTabViewItem alloc] initWithIdentifier:@"3"] autorelease];
        NSView *v3 = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 510, 404)] autorelease];
        [ti3 setView:v3];
        [v3 addSubview:[self makeCardWithFrame:NSMakeRect(8, 320, 490, 68)]];
        lUtil = [self makeLabel:NSMakeRect(24, 344, 280, 22) size:13 bold:YES secondary:NO];
        [v3 addSubview:lUtil];
        btnData = [[[NSButton alloc] initWithFrame:NSMakeRect(344, 339, 140, 30)] autorelease];
        [btnData setBezelStyle:NSRoundedBezelStyle];
        [btnData setTarget:self];
        [btnData setAction:@selector(openDataWindow:)];
        [v3 addSubview:btnData];
        [tabView addTabViewItem:ti3];

        /* Tab 4: About */
        NSTabViewItem *ti4 = [[[NSTabViewItem alloc] initWithIdentifier:@"4"] autorelease];
        NSView *v4 = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 510, 404)] autorelease];
        [ti4 setView:v4];
        [v4 addSubview:[self makeCardWithFrame:NSMakeRect(8, 308, 490, 80)]];
        verLabel = [self makeLabel:NSMakeRect(24, 348, 440, 22) size:13 bold:YES secondary:NO];
        [v4 addSubview:verLabel];
        NSTextField *repoL = [self makeLabel:NSMakeRect(24, 322, 440, 18) size:11 bold:NO secondary:YES];
        [repoL setStringValue:@"https://github.com/isthaison/gotv"];
        [v4 addSubview:repoL];
        [v4 addSubview:[self makeCardWithFrame:NSMakeRect(8, 228, 490, 68)]];
        btnFolder = [[[NSButton alloc] initWithFrame:NSMakeRect(24, 247, 210, 30)] autorelease];
        [btnFolder setBezelStyle:NSRoundedBezelStyle];
        [btnFolder setTarget:self];
        [btnFolder setAction:@selector(openConfigFolder:)];
        [v4 addSubview:btnFolder];
        btnUpdate = [[[NSButton alloc] initWithFrame:NSMakeRect(274, 247, 210, 30)] autorelease];
        [btnUpdate setBezelStyle:NSRoundedBezelStyle];
        [btnUpdate setTarget:self];
        [btnUpdate setAction:@selector(checkUpdates:)];
        [v4 addSubview:btnUpdate];
        [tabView addTabViewItem:ti4];

        [self selectTab:0];
    }

    [self loadFromConfig];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
}

- (IBAction)telexSwitchChanged:(id)sender {
    (void)sender;
    BOOL on = ([telexSwitch state] == NSOnState);
    [modePop selectItemAtIndex:(on ? 0 : 1)];
    [self settingChanged:nil];
}

- (IBAction)vniSwitchChanged:(id)sender {
    (void)sender;
    BOOL on = ([vniSwitch state] == NSOnState);
    [modePop selectItemAtIndex:(on ? 1 : 0)];
    [self settingChanged:nil];
}

- (IBAction)suggestGenChanged:(id)sender {
    (void)sender;
    [suggestCheck setState:[suggestGenSwitch state]];
    [self settingChanged:nil];
}

- (void)loadFromConfig {
    gchar *dir = gtv_user_path(g_get_user_config_dir(), NULL);
    GtvConfig config;
    memset(&config, 0, sizeof(config));
    gtv_config_load(&config, dir);
    g_free(dir);

    gtv_i18n_set_lang(config.ui_lang);
    isLightTheme = (config.ui_theme == GTV_THEME_LIGHT);
    [themePop selectItemAtIndex:(isLightTheme ? 1 : 0)];
    [langPop selectItemAtIndex:(config.ui_lang == GTV_LANG_EN ? 1 : 0)];
    [self applyTheme];
    [self applyLanguage];

    [modePop selectItemAtIndex:(config.mode == GTV_VNI ? 1 : 0)];
    [telexSwitch setState:(config.mode == GTV_TELEX ? NSOnState : NSOffState)];
    [vniSwitch setState:(config.mode == GTV_VNI ? NSOnState : NSOffState)];
    [modernCheck setState:(config.modern ? NSOnState : NSOffState)];
    [spellCheck setState:(config.spellcheck ? NSOnState : NSOffState)];
    [suggestCheck setState:(config.suggest_enabled ? NSOnState : NSOffState)];
    [suggestGenSwitch setState:(config.suggest_enabled ? NSOnState : NSOffState)];

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
    config.ui_theme = ([themePop indexOfSelectedItem] == 1) ? GTV_THEME_LIGHT : GTV_THEME_DARK;
    isLightTheme = (config.ui_theme == GTV_THEME_LIGHT);
    [self applyTheme];
    gtv_i18n_set_lang(config.ui_lang);
    [self applyLanguage];

    config.mode = ([modePop indexOfSelectedItem] == 1) ? GTV_VNI : GTV_TELEX;
    [telexSwitch setState:(config.mode == GTV_TELEX ? NSOnState : NSOffState)];
    [vniSwitch setState:(config.mode == GTV_VNI ? NSOnState : NSOffState)];
    config.modern = ([modernCheck state] == NSOnState);
    config.spellcheck = ([spellCheck state] == NSOnState);
    config.suggest_enabled = ([suggestCheck state] == NSOnState);
    [suggestGenSwitch setState:(config.suggest_enabled ? NSOnState : NSOffState)];

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

- (IBAction)openAppDictsWindow:(id)sender {
    (void)sender;
    [[GtvAppDictsWindow shared] show];
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
