#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>
#include "engine.h"
#include "SetupWindowController.h"

static const char *kConnectionName = "GoTV_Connection";

static gboolean mac_asset_match(const gchar *asset, const gchar *tagver, gpointer unused) {
    (void)unused;
    gchar *want_dmg = g_strdup_printf("gotv-%s-macos.dmg", tagver);
    gchar *want_pkg = g_strdup_printf("gotv-%s-macos.pkg", tagver);
    gchar *want_zip = g_strdup_printf("gotv-%s-macos.zip", tagver);
    gboolean ok = (strcmp(asset, want_dmg) == 0 || strcmp(asset, want_pkg) == 0 || strcmp(asset, want_zip) == 0);
    g_free(want_dmg);
    g_free(want_pkg);
    g_free(want_zip);
    return ok;
}

static NSString *bundleVersion(void) {
    NSString *v = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleShortVersionString"];
    return (v && [v length]) ? v : @"0.0.0";
}

static void showAvailableAlert(NSString *tag, NSString *url) {
    NSString *msg = [NSString stringWithFormat:
        @"Có bản mới %@. Tải về file cài đặt ngay?",
        tag];
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:@"GoTV cập nhật"];
    [alert setInformativeText:msg];
    [alert addButtonWithTitle:@"Tải về"];
    [alert addButtonWithTitle:@"Để sau"];
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSString *ext = [url pathExtension];
    if (![ext length]) ext = @"dmg";
    NSString *dest = [NSString stringWithFormat:@"/tmp/gotv-%@-macos.%@", tag, ext];
    if (gtv_update_download([url UTF8String], [dest UTF8String])) {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:dest]];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:
            [NSArray arrayWithObject:[NSURL fileURLWithPath:dest]]];
    } else {
        NSAlert *err = [[[NSAlert alloc] init] autorelease];
        [err setMessageText:@"GoTV cập nhật"];
        [err setInformativeText:@"Không tải được bản mới (cần mạng và curl)."];
        [err runModal];
    }
}

/* No Blocks anywhere in this file on purpose: it compiles with plain clang
 * (no -fblocks, no ARC), so background work uses NSThread and UI hops use
 * performSelectorOnMainThread:. */
@interface GtvUpdater : NSObject
+ (instancetype)sharedUpdater;
- (void)checkInBackground:(NSDictionary *)args;
- (void)presentResult:(NSDictionary *)info;
- (void)showSettingsNotification:(NSNotification *)note;
@end

@implementation GtvUpdater

+ (instancetype)sharedUpdater {
    static GtvUpdater *shared = nil;
    if (!shared) shared = [[GtvUpdater alloc] init];
    return shared;
}

- (void)checkInBackground:(NSDictionary *)args {
    @autoreleasepool {
        BOOL manual = [[args objectForKey:@"manual"] boolValue];
        NSString *current = [args objectForKey:@"current"];
        gchar *tag = NULL, *url = NULL;
        GtvUpdateStatus st = gtv_update_check_full(NULL, [current UTF8String],
                                                   mac_asset_match, NULL, &tag, &url);
        /* Dictionary refresh rides the same (throttled) check. */
        gtv_dict_update_check(NULL);
        NSString *nstag = tag ? [NSString stringWithUTF8String:tag] : nil;
        NSString *nsurl = url ? [NSString stringWithUTF8String:url] : nil;
        g_free(tag);
        g_free(url);
        NSMutableDictionary *info = [NSMutableDictionary dictionary];
        [info setObject:[NSNumber numberWithInt:(int)st] forKey:@"status"];
        [info setObject:[NSNumber numberWithBool:manual] forKey:@"manual"];
        [info setObject:current forKey:@"current"];
        if (nstag) [info setObject:nstag forKey:@"tag"];
        if (nsurl) [info setObject:nsurl forKey:@"url"];
        [[GtvUpdater sharedUpdater] performSelectorOnMainThread:@selector(presentResult:)
                                                     withObject:info
                                                  waitUntilDone:NO];
    }
}

- (void)presentResult:(NSDictionary *)info {
    GtvUpdateStatus st = (GtvUpdateStatus)[[info objectForKey:@"status"] intValue];
    BOOL manual = [[info objectForKey:@"manual"] boolValue];
    NSString *current = [info objectForKey:@"current"];
    NSString *nstag = [info objectForKey:@"tag"];
    NSString *nsurl = [info objectForKey:@"url"];
    if (st == GTV_UPDATE_AVAILABLE && nstag && nsurl) {
        showAvailableAlert(nstag, nsurl);
    } else if (manual) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"GoTV cập nhật"];
        [alert setInformativeText:(st == GTV_UPDATE_CURRENT)
            ? [NSString stringWithFormat:@"Đang dùng bản mới nhất (%@).", current]
            : @"Không kiểm tra được bản mới. Thử lại sau."];
        [alert runModal];
    }
}

- (void)showSettingsNotification:(NSNotification *)note {
    (void)note;
    GoTVShowSettings();
}

@end

/* Network off the main thread, alerts back on it. Manual mode always
 * reports; auto mode is silent unless an update exists. */
static void checkForUpdates(BOOL manual) {
    if (!manual) {
        if (!gtv_update_should_autocheck()) return;
        gtv_update_mark_checked();
    }
    NSString *current = bundleVersion();
    NSDictionary *args = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithBool:manual], @"manual",
        current, @"current", nil];
    [NSThread detachNewThreadSelector:@selector(checkInBackground:)
                             toTarget:[GtvUpdater sharedUpdater]
                           withObject:args];
}

void GoTVCheckForUpdates(BOOL manual) {
    checkForUpdates(manual);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    gtv_init();

    /* Bundled tables live in Contents/Resources/data (packaged by make);
     * user files in ~/.config/gotv win as usual. */
    NSString *res = [[NSBundle mainBundle] resourcePath];
    if (res) {
        setenv("GTV_DATA_DIR",
               [[res stringByAppendingPathComponent:@"data"] UTF8String],
               1);
    }

    /* Second launch opens Settings instead of doubling the agent. */
    NSString *bid = [[NSBundle mainBundle] bundleIdentifier];
    NSArray *running = [NSRunningApplication runningApplicationsWithBundleIdentifier:bid];
    if ([running count] > 1) {
        [[NSDistributedNotificationCenter defaultCenter]
            postNotificationName:@"vn.gotv.ShowSettings" object:nil];
        return 0;
    }
    [[NSDistributedNotificationCenter defaultCenter]
        addObserver:[GtvUpdater sharedUpdater]
           selector:@selector(showSettingsNotification:)
               name:@"vn.gotv.ShowSettings"
             object:nil];

    IMKServer *server = [[IMKServer alloc]
        initWithName:[NSString stringWithUTF8String:kConnectionName]
        bundleIdentifier:bid];
    (void)server;

    checkForUpdates(NO);

    [[NSApplication sharedApplication] run];
    return 0;
}
