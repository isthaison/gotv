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
    NSTextField *appField;
    NSButton *checks[5];
    GPtrArray *rules;
}
+ (instancetype)shared;
- (void)show;
- (void)refresh;
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
        NSRect frame = NSMakeRect(0, 0, 480, 390);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setTitle:@"GoTV — Từ điển theo từng ứng dụng"];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        NSTextField *note = [NSTextField labelWithString:@"Nhập Bundle ID (vd: com.microsoft.VSCode, com.apple.Terminal):"];
        [note setFrame:NSMakeRect(12, 356, 456, 22)];
        [view addSubview:note];

        NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(12, 165, 456, 185)] autorelease];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSBezelBorder];

        table = [[[NSTableView alloc] initWithFrame:[[scroll contentView] bounds]] autorelease];
        NSTableColumn *colApp = [[[NSTableColumn alloc] initWithIdentifier:@"app"] autorelease];
        [[colApp headerCell] setStringValue:@"Ứng dụng (Bundle ID)"];
        [colApp setWidth:240];
        [table addTableColumn:colApp];

        NSTableColumn *colDict = [[[NSTableColumn alloc] initWithIdentifier:@"dicts"] autorelease];
        [[colDict headerCell] setStringValue:@"Bộ từ điển"];
        [colDict setWidth:195];
        [table addTableColumn:colDict];

        [table setDataSource:self];
        [table setDelegate:self];
        [scroll setDocumentView:table];
        [view addSubview:scroll];

        appField = [[[NSTextField alloc] initWithFrame:NSMakeRect(12, 128, 456, 24)] autorelease];
        [appField setPlaceholderString:@"com.microsoft.VSCode"];
        [view addSubview:appField];

        const char *titles[5] = { "Tiếng Việt", "T-SQL", "C / C++", "C# (.NET)", "Tiếng Anh" };
        for (int i = 0; i < 5; i++) {
            int col = i % 3;
            int row = i / 3;
            checks[i] = [[[NSButton alloc] initWithFrame:NSMakeRect(12 + col * 150, 95 - row * 24, 145, 22)] autorelease];
            [checks[i] setButtonType:NSSwitchButton];
            [checks[i] setTitle:[NSString stringWithUTF8String:titles[i]]];
            [checks[i] setState:(i == 0 ? NSOnState : NSOffState)];
            [view addSubview:checks[i]];
        }

        NSButton *btnAdd = [[[NSButton alloc] initWithFrame:NSMakeRect(12, 18, 160, 28)] autorelease];
        [btnAdd setTitle:@"Thêm / Cập nhật"];
        [btnAdd setButtonType:NSMomentaryPushInButton];
        [btnAdd setBezelStyle:NSRoundedBezelStyle];
        [btnAdd setTarget:self];
        [btnAdd setAction:@selector(addOrUpdate:)];
        [view addSubview:btnAdd];

        NSButton *btnDel = [[[NSButton alloc] initWithFrame:NSMakeRect(182, 18, 130, 28)] autorelease];
        [btnDel setTitle:@"Xóa đã chọn"];
        [btnDel setButtonType:NSMomentaryPushInButton];
        [btnDel setBezelStyle:NSRoundedBezelStyle];
        [btnDel setTarget:self];
        [btnDel setAction:@selector(removeSelected:)];
        [view addSubview:btnDel];
    }
    [self refresh];
    [window center];
    [window makeKeyAndOrderFront:nil];
}

- (void)refresh {
    if (rules) g_ptr_array_unref(rules);
    rules = gtv_app_dicts_load(NULL);
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
    if (r->app_id) [appField setStringValue:[NSString stringWithUTF8String:r->app_id]];
    const char *ids[5] = { "vi", "tsql", "c", "csharp", "en" };
    for (int i = 0; i < 5; i++) {
        [checks[i] setState:(gtv_dict_lang_enabled(r->dicts, ids[i]) ? NSOnState : NSOffState)];
    }
}

- (IBAction)addOrUpdate:(id)sender {
    (void)sender;
    const gchar *raw = [[appField stringValue] UTF8String];
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
    NSTextField *keyField;
    NSTextField *valField;
    BOOL emoji;
}
+ (instancetype)shared;
- (void)show;
- (void)refresh;
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

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 480, 440);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setTitle:@"GoTV — Bảng gõ tắt & Emoji"];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        typePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(12, 402, 170, 26) pullsDown:NO];
        [typePop addItemsWithTitles:[NSArray arrayWithObjects:@"Macro (gõ tắt)", @"Emoji", nil]];
        [typePop setTarget:self];
        [typePop setAction:@selector(typeChanged:)];
        [view addSubview:typePop];
        [typePop release];

        NSTextField *note = [NSTextField labelWithString:@"Bấm Tab khi gõ để bung từ viết tắt / emoji."];
        [note setFrame:NSMakeRect(190, 402, 278, 26)];
        [[note cell] setTruncatesLastVisibleLine:YES];
        [view addSubview:note];

        NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(12, 122, 456, 268)] autorelease];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSBezelBorder];
        table = [[NSTableView alloc] initWithFrame:[[scroll contentView] bounds]];
        NSTableColumn *ck = [[[NSTableColumn alloc] initWithIdentifier:@"key"] autorelease];
        [ck setWidth:150];
        [[ck headerCell] setStringValue:@"Từ gõ"];
        NSTableColumn *cv = [[[NSTableColumn alloc] initWithIdentifier:@"value"] autorelease];
        [[cv headerCell] setStringValue:@"Nội dung thay thế"];
        [table addTableColumn:ck];
        [table addTableColumn:cv];
        [table setDataSource:self];
        [table setDelegate:self];
        [table setAllowsMultipleSelection:NO];
        [scroll setDocumentView:table];
        [table release];
        [view addSubview:scroll];

        NSTextField *kl = [NSTextField labelWithString:@"Từ gõ:"];
        [kl setFrame:NSMakeRect(12, 88, 60, 22)];
        [view addSubview:kl];
        keyField = [[NSTextField alloc] initWithFrame:NSMakeRect(78, 86, 390, 26)];
        [view addSubview:keyField];
        [keyField release];

        NSTextField *vl = [NSTextField labelWithString:@"Nội dung:"];
        [vl setFrame:NSMakeRect(12, 56, 60, 22)];
        [view addSubview:vl];
        valField = [[NSTextField alloc] initWithFrame:NSMakeRect(78, 54, 390, 26)];
        [view addSubview:valField];
        [valField release];

        NSArray *titles = [NSArray arrayWithObjects:@"Thêm / Sửa", @"Xóa chọn", @"Mở thư mục", nil];
        NSRect rects[3] = {
            NSMakeRect(12, 12, 110, 30),
            NSMakeRect(130, 12, 90, 30),
            NSMakeRect(228, 12, 120, 30),
        };
        SEL actions[3] = { @selector(addOrUpdate:), @selector(removeSelected:), @selector(openFolder:) };
        for (NSUInteger i = 0; i < 3; i++) {
            NSButton *b = [[[NSButton alloc] initWithFrame:rects[i]] autorelease];
            [b setTitle:[titles objectAtIndex:i]];
            [b setButtonType:NSMomentaryPushInButton];
            [b setBezelStyle:NSRoundedBezelStyle];
            [b setTarget:self];
            [b setAction:actions[i]];
            [view addSubview:b];
        }
    }
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
        NSString *msg = err && err->message
            ? [NSString stringWithUTF8String:err->message]
            : @"Không lưu được file dữ liệu.";
        [alert setInformativeText:(msg ? msg : @"Không lưu được file dữ liệu.")];
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
        [alert setInformativeText:@"Khóa hoặc giá trị không hợp lệ (khóa không chứa dấu cách hay dấu =, giá trị không rỗng)."];
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
    NSPopUpButton *modePop;
    NSButton *modernCheck;
    NSButton *spellCheck;
    NSButton *suggestCheck;
    NSButton *viDictCheck;
    NSButton *enDictCheck;
    NSButton *cDictCheck;
    NSButton *csDictCheck;
    NSButton *tsqlDictCheck;
}
+ (instancetype)shared;
- (void)show;
- (void)loadFromConfig;
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

- (void)show {
    if (!window) {
        NSRect frame = NSMakeRect(0, 0, 480, 470);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [window setTitle:@"GoTV — Cài đặt cấu hình"];
        [window setReleasedWhenClosed:NO];
        NSView *view = [window contentView];

        int y = 425;

        // Group 1: Kiểu gõ (Telex / VNI)
        NSTextField *lMode = [NSTextField labelWithString:@"Kiểu gõ chính:"];
        [lMode setFrame:NSMakeRect(20, y, 120, 22)];
        [lMode setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lMode];

        modePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150, y - 2, 160, 26) pullsDown:NO];
        [modePop addItemsWithTitles:[NSArray arrayWithObjects:@"Telex", @"VNI", nil]];
        [modePop setTarget:self];
        [modePop setAction:@selector(settingChanged:)];
        [view addSubview:modePop];
        [modePop release];

        y -= 45;

        // Group 2: Tùy chọn gõ dấu & Chính tả
        NSTextField *lTone = [NSTextField labelWithString:@"Quy tắc dấu & Chính tả (Apple IMK):"];
        [lTone setFrame:NSMakeRect(20, y, 300, 22)];
        [lTone setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lTone];

        y -= 28;
        modernCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [modernCheck setButtonType:NSSwitchButton];
        [modernCheck setTitle:@"Bỏ dấu kiểu mới tự do (hòa, thúy thay vì hoà, thuý)"];
        [modernCheck setTarget:self];
        [modernCheck setAction:@selector(settingChanged:)];
        [view addSubview:modernCheck];

        y -= 26;
        spellCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [spellCheck setButtonType:NSSwitchButton];
        [spellCheck setTitle:@"Kiểm tra chính tả tiếng Việt khi gõ"];
        [spellCheck setTarget:self];
        [spellCheck setAction:@selector(settingChanged:)];
        [view addSubview:spellCheck];

        y -= 26;
        suggestCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 420, 22)] autorelease];
        [suggestCheck setButtonType:NSSwitchButton];
        [suggestCheck setTitle:@"Hiển thị gợi ý từ điển & sửa lỗi chính tả (Candidate list)"];
        [suggestCheck setTarget:self];
        [suggestCheck setAction:@selector(settingChanged:)];
        [view addSubview:suggestCheck];

        y -= 42;

        // Group 3: Gói từ điển gợi ý
        NSTextField *lDict = [NSTextField labelWithString:@"Gói từ điển gợi ý từ & sửa lỗi:"];
        [lDict setFrame:NSMakeRect(20, y, 260, 22)];
        [lDict setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lDict];

        y -= 26;
        viDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 130, 22)] autorelease];
        [viDictCheck setButtonType:NSSwitchButton];
        [viDictCheck setTitle:@"Tiếng Việt (vi)"];
        [viDictCheck setTarget:self];
        [viDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:viDictCheck];

        enDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(180, y, 130, 22)] autorelease];
        [enDictCheck setButtonType:NSSwitchButton];
        [enDictCheck setTitle:@"Tiếng Anh (en)"];
        [enDictCheck setTarget:self];
        [enDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:enDictCheck];

        cDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(330, y, 120, 22)] autorelease];
        [cDictCheck setButtonType:NSSwitchButton];
        [cDictCheck setTitle:@"Lập trình C (c)"];
        [cDictCheck setTarget:self];
        [cDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:cDictCheck];

        y -= 24;
        csDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(30, y, 130, 22)] autorelease];
        [csDictCheck setButtonType:NSSwitchButton];
        [csDictCheck setTitle:@"C# (csharp)"];
        [csDictCheck setTarget:self];
        [csDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:csDictCheck];

        tsqlDictCheck = [[[NSButton alloc] initWithFrame:NSMakeRect(180, y, 120, 22)] autorelease];
        [tsqlDictCheck setButtonType:NSSwitchButton];
        [tsqlDictCheck setTitle:@"T-SQL (tsql)"];
        [tsqlDictCheck setTarget:self];
        [tsqlDictCheck setAction:@selector(settingChanged:)];
        [view addSubview:tsqlDictCheck];

        NSButton *btnAppDicts = [[[NSButton alloc] initWithFrame:NSMakeRect(305, y - 2, 155, 26)] autorelease];
        [btnAppDicts setTitle:@"Theo từng ứng dụng..."];
        [btnAppDicts setButtonType:NSMomentaryPushInButton];
        [btnAppDicts setBezelStyle:NSRoundedBezelStyle];
        [btnAppDicts setTarget:self];
        [btnAppDicts setAction:@selector(openAppDictsWindow:)];
        [view addSubview:btnAppDicts];

        y -= 45;

        // Group 4: Tiện ích & Dữ liệu
        NSTextField *lUtil = [NSTextField labelWithString:@"Dữ liệu & Cập nhật:"];
        [lUtil setFrame:NSMakeRect(20, y, 200, 22)];
        [lUtil setFont:[NSFont boldSystemFontOfSize:13]];
        [view addSubview:lUtil];

        y -= 34;
        NSButton *btnData = [[[NSButton alloc] initWithFrame:NSMakeRect(28, y, 200, 30)] autorelease];
        [btnData setTitle:@"Bảng gõ tắt & Emoji..."];
        [btnData setButtonType:NSMomentaryPushInButton];
        [btnData setBezelStyle:NSRoundedBezelStyle];
        [btnData setTarget:self];
        [btnData setAction:@selector(openDataWindow:)];
        [view addSubview:btnData];

        NSButton *btnFolder = [[[NSButton alloc] initWithFrame:NSMakeRect(240, y, 200, 30)] autorelease];
        [btnFolder setTitle:@"Mở thư mục cấu hình"];
        [btnFolder setButtonType:NSMomentaryPushInButton];
        [btnFolder setBezelStyle:NSRoundedBezelStyle];
        [btnFolder setTarget:self];
        [btnFolder setAction:@selector(openConfigFolder:)];
        [view addSubview:btnFolder];

        y -= 34;
        NSButton *btnUpdate = [[[NSButton alloc] initWithFrame:NSMakeRect(28, y, 200, 30)] autorelease];
        [btnUpdate setTitle:@"Kiểm tra cập nhật ngay..."];
        [btnUpdate setButtonType:NSMomentaryPushInButton];
        [btnUpdate setBezelStyle:NSRoundedBezelStyle];
        [btnUpdate setTarget:self];
        [btnUpdate setAction:@selector(checkUpdates:)];
        [view addSubview:btnUpdate];

        NSTextField *verLabel = [NSTextField labelWithString:@"GoTV · Mã nguồn mở MIT"];
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
