#!/bin/bash
# GoTV developer entry point, same commands on every OS:
# Linux (make), Windows (MSYS2/MinGW Makefile.win + Inno Setup),
# macOS (clang core tests + GoTV.app bundle).
#
# Usage: ./gtv.sh <command> [args]
#   build [args...]   build + test for the current OS
#   test [args...]    run test suites for the current OS
#   vet               strict build + test (-Werror where supported)
#   clean             remove build trees (build/, release-pkg/)
#   install           install to the system (see per-OS notes in help)
#   uninstall         remove a previous install (best-effort clean)
#   package [VER]     build the OS-native installer (.deb/.exe/.zip)
#   bump X.Y.Z ["notes"]  bump version everywhere (single writer)
#   help              this help
#
# Windows: run inside MSYS2 MinGW 64-bit (or Git Bash with MinGW tools);
# `gtv.cmd` forwards cmd.exe calls here. Never passes /CLOSEAPPLICATIONS
# to the installer (it once killed Explorer -> black screen); in-use DLLs
# swap via restartreplace, finished by logoff/reboot.
set -euo pipefail
cd -- "$(dirname -- "$0")"

PROG='./gtv.sh'
VERSION_FILE=VERSION
version() { cat "$VERSION_FILE" 2>/dev/null || echo 0.8.0; }

detect_os() {
    case "$(uname -s | tr '[:upper:]' '[:lower:]')" in
        mingw*|msys*|cygwin*) echo windows ;;
        darwin*) echo mac ;;
        *) echo linux ;;
    esac
}
OS="$(detect_os)"

usage() {
    cat <<USAGE
Usage: $PROG <command> [args]   (OS detected: $OS)
  build [args...]   build + test (linux: make | windows: Makefile.win | mac: core + app)
  test [args...]    run test suites
  vet               strict build + test
  clean             remove build/ and release-pkg/
  install           install to system:
                      linux: sudo $PROG install (DESTDIR=... to stage only)
                      windows: build setup.exe + silent install + start app
                      mac: copy GoTV.app to ~/Library/Input Methods
  install-ollama    legacy alias (Ollama suggestions removed; dictionary is built in)
  uninstall         remove installed files + registry/task/profile leftovers
  package [VER]     OS-native installer (linux: .deb | windows: setup.exe | mac: .zip)
  bump X.Y.Z ["notes"]  bump version everywhere (single writer)
  help              this help
USAGE
}

cmd_help() {
    usage
}

# ---------------- Linux (unchanged) ----------------

cmd_build_linux() {
    # Build and test the native C applications without starting a desktop session.
    make build test "$@"
}

cmd_test_linux() {
    make test "$@"
}

cmd_vet_linux() {
    make vet
}

cmd_clean_linux() {
    rm -rf -- build
}

# ---------------- Windows (MSYS2/MinGW) ----------------

# MSVC/Windows-SDK env leaking into MinGW g++ breaks system headers
# (DWORD/LARGE_INTEGER "does not name a type"); drop it for every build.
# MSYS2_ARG_CONV_EXCL keeps ISCC /Q /D... literal (CI does the same).
win_env() {
    unset INCLUDE LIB CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH || true
    export PATH="/mingw64/bin:/usr/bin:$PATH"
    export MSYS2_ARG_CONV_EXCL="*"
}

win_make() {
    win_env
    local make_bin
    if command -v mingw32-make >/dev/null 2>&1; then make_bin=mingw32-make;
    elif command -v make >/dev/null 2>&1; then make_bin=make;
    else echo "gtv.sh: need mingw32-make (MSYS2 MinGW 64-bit)" >&2; exit 1; fi
    "$make_bin" -f Makefile.win "$@"
}

find_iscc() {
    if command -v iscc.exe >/dev/null 2>&1; then command -v iscc.exe; return 0; fi
    if command -v ISCC.exe >/dev/null 2>&1; then command -v ISCC.exe; return 0; fi
    local cands=(
        "$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"
        "/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
        "/c/Program Files/Inno Setup 6/ISCC.exe"
    )
    local c
    for c in "${cands[@]}"; do [[ -x "$c" ]] && { echo "$c"; return 0; }; done
    echo "gtv.sh: Inno Setup (ISCC.exe) not found (choco install innosetup)" >&2
    return 1
}

win_setup_exe() { echo "gotv-$(version)-x64-setup.exe"; }

win_localappdata() {
    powershell.exe -NoProfile -NonInteractive -Command '$env:LOCALAPPDATA' | tr -d '\r'
}

# sha256 of one file, bare hex.
# Not `cmp`: `cmp` is in diffutils, which is NOT part of a base MSYS2 install,
# so it failed with 127 and every successful install was then reported as
# "không có hiệu lực". sha256sum is coreutils and always present.
# The sed matters: the MSYS build prefixes binary-mode output with a backslash
# ("\f16d…  *path"), so a plain `cut -d' ' -f1` yields "\f16d…" for a Windows
# path and "f16d…" for a POSIX one -- equal files, unequal strings.
win_sha256() {
    sha256sum "$1" 2>/dev/null | sed -e 's/^\\//' -e 's/[[:space:]].*$//'
}

# Wait for a Windows process pattern to exit. Inno GUI detaches right
# away, so blocking waits cannot be used; tasklist truncates long image
# names, hence the PowerShell match instead.
win_wait_pattern() {
    local pattern="$1" timeout="${2:-600}" waited=0
    sleep 8 # let the child spawn before the first check
    while (( waited < timeout )); do
        if ! powershell.exe -NoProfile -NonInteractive -Command \
            "Get-Process | Where-Object { \$_.Name -like '*$pattern*' }" 2>/dev/null | grep -q .; then
            return 0
        fi
        sleep 10
        waited=$((waited + 10))
    done
    echo "gtv.sh: timed out waiting for *$pattern*" >&2
    return 1
}

cmd_build_windows() {
    win_make "$@"
    win_make test
}

cmd_test_windows() {
    win_make test "$@"
}

cmd_vet_windows() {
    # Makefile.win has no -Werror mode; same strict flags as CI plus tests.
    win_make test "$@"
}

cmd_clean_windows() {
    win_make clean
    rm -rf -- release-pkg
}

cmd_package_windows() {
    local ver; ver="$(version)"
    local vernum="$ver.0"
    win_make staging
    win_env
    "$(find_iscc)" /Q "/DAppVersion=$ver" "/DAppVerNum=$vernum" "/DSourceDir=..\\release-pkg" windows/installer.iss
    echo "Đã đóng gói: $(win_setup_exe)"
}

# Fail fast when something still holds gtv_tsf.dll: replacing a loaded
# DLL hangs the installer on an unanswerable dialog in silent mode
# (and /CLOSEAPPLICATIONS once killed Explorer -> black screen).
# Prints holder names; returns 0 when free (or check impossible).
# $1 = installed gtv_tsf.dll full path (backslashes). Only that exact file
# blocks the installer; a dev copy under build\win held by IDEs is harmless.
win_dll_holders() {
    # Every $ below is PowerShell's: escape for bash (set -u aborts on $p).
    # Tooling consoles (powershell/conhost/cmd/...) load the TIP transiently
    # on focus and never block for long; real blockers are GUI apps holding
    # the keyboard (browsers, IDEs, explorer, chat clients, the tray itself).
    powershell.exe -NoProfile -NonInteractive -Command \
        "foreach (\$p in (Get-Process -ErrorAction SilentlyContinue)) { try { foreach (\$m in \$p.Modules) { if (\$m.FileName -eq '$1') { \$p.Name; break } } } catch {} }" 2>/dev/null \
        | sort -u | grep -v -i -E '^(setup|unins|powershell|pwsh|conhost|cmd|bash|mintty|windowsterminal)' || true
}

# One-time migration helper: rename the held flat-layout gtv_tsf.dll aside
# (loaded images can be renamed even when they cannot be deleted/replaced),
# so the installer lays a fresh stub without closing anything. Running apps
# keep the old mapping under the new name; the tray deletes the spare when
# free (engine/verdir.c). Falls back to the holder gate when rename fails.
legacy_friendly_swap() {
    local appdir0="$1" dll old
    dll="$appdir0/gtv_tsf.dll"
    old="$appdir0/gtv_tsf.prev.dll"
    if [[ ! -f "$dll" ]]; then return 0; fi
    rm -f "$old" 2>/dev/null || true
    if mv -f "$dll" "$old" 2>/dev/null; then
        echo "Đã dời gtv_tsf.dll cũ sang gtv_tsf.prev.dll (app đang mở giữ bản cũ, bản mới cài cạnh)."
        return 0
    fi
    legacy_holder_gate "$appdir0" || return 1
    return 0
}

# Legacy migration gate: the flat layout's gtv_tsf.dll (full engine) gets
# replaced by the stable stub once, so holders must be gone first.
# Prints holder names; returns 0 when free (or check impossible).
# $1 = app dir (MSYS path). Only used when current.txt is absent.
legacy_holder_gate() {
    local appdir0="$1" dllpath holders
    dllpath="${appdir0//\//\\}\\gtv_tsf.dll"
    holders="$(win_dll_holders "$dllpath")"
    # Only explorer left: a fresh explorer hasn't loaded the TIP yet, so
    # restarting it (taskbar flashes once) usually frees the DLL.
    if [[ "$holders" == "explorer" ]]; then
        echo "Chỉ còn Explorer giữ DLL — restart Explorer rồi cài tiếp..."
        powershell.exe -NoProfile -NonInteractive -Command \
            "Stop-Process -Name explorer -Force" 2>/dev/null || true
        sleep 8
        holders="$(win_dll_holders "$dllpath")"
    fi
    if [[ -n "$holders" ]]; then
        echo "gtv_tsf.dll đang bị giữ bởi:" >&2
        echo "$holders" | sed 's/^/  - /' >&2
        echo "Đóng các app đó (lưu việc trước), restart Explorer nếu cần," >&2
        echo "rồi chạy lại. Kẹt cứng (OpenCode/IDE giữ DLL) -> reboot rồi cài ngay." >&2
        if [[ -t 0 ]]; then
            echo "Enter để kiểm tra lại, Ctrl+C để dừng..." >&2
            read -r _
            holders="$(win_dll_holders "$dllpath")"
            [[ -z "$holders" ]] || { echo "Vẫn còn tiến trình giữ DLL, dừng cài." >&2; return 1; }
        else
            return 1
        fi
    fi
    return 0
}

win_appdir() {
    local root; root="$(win_localappdata)/Programs"
    # The legacy directory is ours only when the versioned TSF layout exists.
    if [[ -f "$root/GoTiengViet/current.txt" && -f "$root/GoTiengViet/gtv_tsf.dll" ]]; then
        echo "$root/GoTiengViet"
    else
        echo "$root/GoTV"
    fi
}

cmd_install_windows() {
    local setup; setup="$(win_setup_exe)"
    [[ -f "$setup" ]] || cmd_package_windows
    local appdir0; appdir0="$(win_appdir)"
    local verv; verv="$(version)"
    # Preflight: an install aborted mid-replace leaves gtv_tsf.dll marked for
    # deletion - the file is still listed but every open fails with
    # ERROR_ACCESS_DENIED, so Inno cannot read its VERSIONINFO, decides it
    # must replace the stub, and DeleteFile then fails with code 5. The stub
    # only clears once every process holding it exits.
    if [[ -f "$appdir0/gtv_tsf.dll" ]] && ! head -c 2 "$appdir0/gtv_tsf.dll" >/dev/null 2>&1; then
        echo "gtv.sh: CẢNH BÁO: $appdir0/gtv_tsf.dll đang bị đánh dấu xoá" >&2
        echo "gtv.sh: (còn sót từ lần cài bị hủy). Cần đóng hết app đang dùng bàn phím" >&2
        echo "gtv.sh: hoặc đăng xuất/đăng nhập lại (tốt nhất là reboot), rồi chạy lại install." >&2
    fi
    # New-world layout (current.txt present) only ADDS a ver\<V>\ payload dir,
    # so loaded files can never block the installer: no holder gate, no
    # forced closes, no logoff. The gate below stays for the one-time
    # migration from the legacy flat layout (old gtv_tsf.dll gets replaced
    # by the stable stub).
    if [[ ! -f "$appdir0/current.txt" && -d "$appdir0" ]]; then
        legacy_friendly_swap "$appdir0" || return 1
    fi
    win_env
    # NOTE: launch via Start-Process, never direct exec: Inno run from an
    # MSYS console handle hangs before log init; detached start works.
    # -Wait -PassThru is what makes a failed/rolled-back install visible:
    # Inno exits non-zero after "User canceled"/rollback.
    local windir; windir="$(cygpath -w "$PWD")"
    local log="$PWD/setup-install.log"
    rm -f "$log"
    local rc
    rc="$(powershell.exe -NoProfile -NonInteractive -Command \
        "\$p = Start-Process '$windir\\$setup' -ArgumentList '/VERYSILENT','/NORESTART','/LOG=$windir\\setup-install.log' -Wait -PassThru; \$p.ExitCode" \
        2>/dev/null | tr -dc '0-9')" || rc=""
    if [[ -n "$rc" && "$rc" != "0" ]]; then
        echo "gtv.sh: installer thất bại (exit $rc) — thường là file đang bị tiến trình khóa" >&2
        echo "--- setup log tail ---" >&2
        tail -n 20 "$log" >&2 || true
        exit 1
    fi
    local appdir; appdir="$appdir0"
    local installed="$appdir/ver/$verv/gotv.exe"
    local staged="release-pkg/ver/$verv/gotv.exe"
    if [[ ! -f "$installed" && ! -f "$appdir/gotv.exe" ]]; then
        echo "gtv.sh: install thất bại (thiếu payload $verv trong $appdir)" >&2
        echo "--- setup log tail ---" >&2
        tail -n 20 "$log" >&2 || true
        exit 1
    fi
    # Verify against what we just built. Existence alone proves nothing: a
    # rolled-back or cancelled install leaves the PREVIOUS payload in place,
    # and reporting success there starts the old tray. An unreadable hash is
    # treated as "cannot verify", never as a mismatch -- guessing wrong here
    # tells the user a good install failed.
    if [[ -f "$installed" && -f "$staged" ]]; then
        local sum_built sum_installed
        sum_built="$(win_sha256 "$staged")"
        sum_installed="$(win_sha256 "$installed")"
        if [[ -n "$sum_built" && -n "$sum_installed" && "$sum_built" != "$sum_installed" ]]; then
            echo "gtv.sh: install KHÔNG có hiệu lực: payload $verv trong $appdir không khớp bản vừa build" >&2
            echo "gtv.sh: installer đã rollback, hoặc tiến trình đang khóa file." >&2
            echo "--- setup log tail ---" >&2
            tail -n 20 "$log" >&2 || true
            exit 1
        fi
    fi
    echo "Đã cài: $appdir"
    # Startup entries point at the versioned tray (stale ones self-forward);
    # fall back to the legacy flat path when upgrading an old install.
    local winapp
    if [[ -f "$appdir/ver/$verv/gotv.exe" ]]; then
        winapp="${appdir//\//\\}\\ver\\${verv}\\gotv.exe"
    else
        winapp="${appdir//\//\\}\\gotv.exe"
    fi
    # Takeover (not a second instance): if the postinstall entry already
    # started the tray, this just signals it and exits without any window.
    powershell.exe -NoProfile -NonInteractive -Command "Start-Process '$winapp' -ArgumentList '--takeover'" || true
    echo "App khay đã chạy (bản mới tự tiếp quản, không cần đăng xuất)."
}

cmd_uninstall_windows() {
    local appdir unins
    appdir="$(win_appdir)"
    unins="$appdir/unins000.exe"
    if [[ -x "$unins" ]]; then
        taskkill //F //IM gotv.exe 2>/dev/null || true
        "$unins" /VERYSILENT /NORESTART &
        win_wait_pattern "unins000" 300 || true
    else
        echo "gtv.sh: không thấy $unins (chưa cài? vẫn dọn leftovers bên dưới)"
    fi
    # Old uninstallers predate the hardened [UninstallRun]: clean leftovers.
    powershell.exe -NoProfile -NonInteractive -Command \
        "Remove-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Run' -Name GoTV -ErrorAction SilentlyContinue" || true
    reg delete "HKCU\\Software\\Microsoft\\CTF\\TIP\\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}" /f 2>/dev/null || true
    if schtasks //Query //TN GoTiengViet 2>/dev/null | grep -qi GoTiengViet; then
        schtasks //Delete //TN GoTiengViet //F 2>/dev/null || \
            echo "(task cũ còn sót, cần quyền admin: schtasks /Delete /TN GoTiengViet /F)"
    fi
    powershell.exe -NoProfile -NonInteractive -Command \
        "Remove-Item '$appdir' -Recurse -Force -ErrorAction SilentlyContinue" || true
    if [[ -e "$appdir" ]]; then
        echo "Còn file bị khóa (DLL đang load) — đăng xuất/đăng nhập hoặc reboot để xóa nốt: $appdir"
    else
        echo "Đã gỡ sạch."
    fi
}

# ---------------- macOS ----------------

cmd_build_mac() {
    mkdir -p build/macos
    # shellcheck disable=SC2046
    clang -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
        $(pkg-config --cflags glib-2.0 gio-2.0) -Iengine \
        tests/test_engine.c engine/*.c $(pkg-config --libs glib-2.0 gio-2.0) -lm \
        -o build/macos/test-engine
    # shellcheck disable=SC2046
    clang -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
        $(pkg-config --cflags glib-2.0 gio-2.0) -Iengine \
        tests/test_support.c engine/*.c $(pkg-config --libs glib-2.0 gio-2.0) -lm \
        -o build/macos/test-support
    GTV_DATA_DIR="$PWD/data" ./build/macos/test-engine
    GTV_DATA_DIR="$PWD/data" ./build/macos/test-support
    mac_build_app
}

mac_build_app() {
    local app=build/macos/GoTV.app/Contents
    mkdir -p "$app/MacOS" "$app/Resources/data"
    # shellcheck disable=SC2046
    clang -O2 -g -Wall \
        $(pkg-config --cflags glib-2.0 gio-2.0) -Iengine \
        -framework Cocoa -framework InputMethodKit \
        engine/*.c macos/*.m $(pkg-config --libs glib-2.0 gio-2.0) -lm \
        -o "$app/MacOS/GoTV"
    cp macos/Info.plist "$app/Info.plist"
    printf 'APPL????' > "$app/PkgInfo"
    cp data/macros.txt data/emojis.txt data/config data/dict-vi.txt \
        data/learned-corrections.txt \
        data/learned-words.txt "$app/Resources/data/"
    echo "Đã build: build/macos/GoTV.app"
}

cmd_test_mac() {
    [[ -x build/macos/test-engine ]] || cmd_build_mac
    GTV_DATA_DIR="$PWD/data" ./build/macos/test-engine
    GTV_DATA_DIR="$PWD/data" ./build/macos/test-support
}

cmd_vet_mac() {
    echo "vet(mac): strict core rebuild (-Werror)"
    mkdir -p build/macos/strict
    # shellcheck disable=SC2046
    clang -O2 -g -std=gnu11 -Wall -Wextra -Werror \
        $(pkg-config --cflags glib-2.0 gio-2.0) -Iengine \
        tests/test_engine.c engine/*.c $(pkg-config --libs glib-2.0 gio-2.0) -lm \
        -o build/macos/strict/test-engine
    GTV_DATA_DIR="$PWD/data" ./build/macos/strict/test-engine
}

cmd_clean_mac() {
    rm -rf -- build/macos
}

cmd_package_mac() {
    [[ -d build/macos/GoTV.app ]] || cmd_build_mac
    local out="gotv-$(version)-macos.zip"
    (cd build/macos && rm -f "$OLDPWD/$out" && zip -qry "$OLDPWD/$out" GoTV.app)
    echo "Đã đóng gói: $out"
}

cmd_install_mac() {
    [[ -d build/macos/GoTV.app ]] || cmd_build_mac
    local dest="$HOME/Library/Input Methods/GoTV.app"
    rm -rf -- "$dest"
    cp -R build/macos/GoTV.app "$dest"
    echo "Đã cài: $dest (đăng xuất/đăng nhập lại rồi thêm bàn phím GoTV)"
}

cmd_uninstall_mac() {
    rm -rf -- "$HOME/Library/Input Methods/GoTV.app"
    echo "Đã gỡ GoTV.app (giữ ~/.config/gotv)."
}

# ---------------- dispatch ----------------

cmd_build() {
    case "$OS" in
        windows) cmd_build_windows "$@" ;;
        mac) cmd_build_mac "$@" ;;
        *) cmd_build_linux "$@" ;;
    esac
}

cmd_test() {
    case "$OS" in
        windows) cmd_test_windows "$@" ;;
        mac) cmd_test_mac "$@" ;;
        *) cmd_test_linux "$@" ;;
    esac
}

cmd_vet() {
    case "$OS" in
        windows) cmd_vet_windows ;;
        mac) cmd_vet_mac ;;
        *) cmd_vet_linux ;;
    esac
}

cmd_clean() {
    case "$OS" in
        windows) cmd_clean_windows ;;
        mac) cmd_clean_mac ;;
        *) cmd_clean_linux ;;
    esac
    # release-pkg/ is Windows staging; never system files or releases.
    rm -rf -- release-pkg
}

# DESTDIR supports package staging and verification without modifying the system.
cmd_install() {
    case "$OS" in
        windows) cmd_install_windows "$@" ; return ;;
        mac) cmd_install_mac "$@" ; return ;;
    esac
    cmd_install_linux "$@"
}

cmd_uninstall() {
    case "$OS" in
        windows) cmd_uninstall_windows "$@" ; return ;;
        mac) cmd_uninstall_mac "$@" ; return ;;
    esac
    cmd_uninstall_linux "$@"
}

cmd_package() {
    case "$OS" in
        windows) cmd_package_windows "$@" ; return ;;
        mac) cmd_package_mac "$@" ; return ;;
    esac
    cmd_package_linux "$@"
}

cmd_install_linux() {
    : "${BUILD_DIR:=build}"
    : "${DESTDIR:=}"
    if [[ -z "$DESTDIR" && "$EUID" -ne 0 ]]; then
        echo "Cần sudo: sudo $PROG install" >&2
        exit 1
    fi
    for binary in ibus-engine-gotv ibus-setup-gotv gotv-demo; do
        if [[ ! -f "$BUILD_DIR/$binary" ]]; then
            echo "Thiếu $BUILD_DIR/$binary. Chạy $PROG build trước." >&2
            exit 1
        fi
    done
    install -Dm755 "$BUILD_DIR/ibus-engine-gotv" "$DESTDIR/usr/libexec/ibus-engine-gotv"
    install -Dm755 "$BUILD_DIR/ibus-setup-gotv" "$DESTDIR/usr/libexec/ibus-setup-gotv"
    install -Dm755 "$BUILD_DIR/gotv-demo" "$DESTDIR/usr/bin/gotv-demo"
    install -Dm644 linux/ibus/gotv.xml "$DESTDIR/usr/share/ibus/component/gotv.xml"
    if [[ -f linux/ibus/gotv.metainfo.xml ]]; then
        install -Dm644 linux/ibus/gotv.metainfo.xml "$DESTDIR/usr/share/metainfo/gotv.metainfo.xml"
    fi
    if [[ -d linux/ibus/screenshots ]]; then
        install -d "$DESTDIR/usr/share/gotv/screenshots"
        for f in linux/ibus/screenshots/*.png; do
            [[ -f "$f" ]] && install -Dm644 "$f" "$DESTDIR/usr/share/gotv/screenshots/$(basename "$f")"
        done
    fi
    install -Dm644 linux/ibus/icons/gotv.svg "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv.svg"
    install -Dm644 linux/ibus/icons/gotv-telex.svg "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv-telex.svg"
    install -Dm644 linux/ibus/icons/gotv-vni.svg "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv-vni.svg"
    install -Dm644 linux/ibus/icons/gotv.svg "$DESTDIR/usr/share/gotv/icons/gotv.svg"
    install -Dm644 linux/ibus/icons/gotv-telex.svg "$DESTDIR/usr/share/gotv/icons/gotv-telex.svg"
    install -Dm644 linux/ibus/icons/gotv-vni.svg "$DESTDIR/usr/share/gotv/icons/gotv-vni.svg"
    install -Dm644 data/macros.txt "$DESTDIR/usr/share/gotv/macros.txt"
    install -Dm644 data/emojis.txt "$DESTDIR/usr/share/gotv/emojis.txt"
    install -Dm644 data/config "$DESTDIR/usr/share/gotv/config"
    install -Dm644 data/dict-vi.txt "$DESTDIR/usr/share/gotv/dict-vi.txt"
    install -Dm644 data/learned-corrections.txt "$DESTDIR/usr/share/gotv/learned-corrections.txt"
    install -Dm644 data/learned-words.txt "$DESTDIR/usr/share/gotv/learned-words.txt"
    for size in 16 22 24 32 48 64 128 256; do
        directory="$DESTDIR/usr/share/icons/hicolor/${size}x${size}/apps"
        mkdir -p "$directory"
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv.svg -o "$directory/gotv.png"
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv-telex.svg -o "$directory/gotv-telex.png"
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv-vni.svg -o "$directory/gotv-vni.png"
    done
    for size in 48 256; do
        name=gotv.png
        [[ "$size" == 256 ]] && name=gotv-256.png
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv.svg -o "$DESTDIR/usr/share/gotv/icons/$name"
    done
    # Icon khay theo mode (T đỏ = Telex, V đỏ = VNI như bản Windows):
    # GNOME chỉ hiện icon nên nhìn là biết mode, kèm label Telex/VNI bên cạnh.
    for size in 48 256; do
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv-telex.svg -o "$DESTDIR/usr/share/gotv/icons/gotv-telex-$size.png"
        rsvg-convert -w "$size" -h "$size" linux/ibus/icons/gotv-vni.svg -o "$DESTDIR/usr/share/gotv/icons/gotv-vni-$size.png"
    done
    mkdir -p "$DESTDIR/usr/bin" "$DESTDIR/usr/share/applications" "$DESTDIR/etc/xdg/autostart"
    ln -sfn /usr/libexec/ibus-setup-gotv "$DESTDIR/usr/bin/gotv"
    if [[ -z "$DESTDIR" ]]; then
        # Clean up legacy manual/package gotiengviet system files on upgrade.
        if dpkg -s gotiengviet >/dev/null 2>&1; then
            dpkg --remove gotiengviet >/dev/null 2>&1 || true
        fi
        rm -f /usr/libexec/ibus-engine-gotiengviet \
            /usr/libexec/ibus-setup-gotiengviet \
            /usr/bin/gotiengviet-demo \
            /usr/bin/gotiengviet \
            /usr/local/bin/gotiengviet \
            /usr/local/bin/gotiengviet-demo \
            /usr/share/ibus/component/gotiengviet.xml \
            /usr/share/metainfo/gotiengviet.metainfo.xml \
            /usr/share/icons/hicolor/scalable/apps/gotiengviet.svg \
            /usr/share/icons/hicolor/scalable/apps/gotiengviet-telex.svg \
            /usr/share/icons/hicolor/scalable/apps/gotiengviet-vni.svg \
            /usr/share/applications/gotiengviet.desktop \
            /etc/xdg/autostart/gotiengviet.desktop
        rm -rf /usr/share/gotiengviet
        for size in 16 22 24 32 48 64 128 256; do
            rm -f "/usr/share/icons/hicolor/${size}x${size}/apps/gotiengviet.png"
            rm -f "/usr/share/icons/hicolor/${size}x${size}/apps/gotiengviet-telex.png"
            rm -f "/usr/share/icons/hicolor/${size}x${size}/apps/gotiengviet-vni.png"
        done
        mkdir -p /usr/local/bin
        ln -sfn /usr/bin/gotv /usr/local/bin/gotv
        ln -sfn /usr/bin/gotv-demo /usr/local/bin/gotv-demo
    fi
    cat > "$DESTDIR/usr/share/applications/gotv.desktop" <<'DESKTOP'
[Desktop Entry]
Name=GoTV
GenericName=Vietnamese Input
Comment=Gõ tiếng Việt Telex/VNI
Exec=/usr/bin/gotv
Icon=gotv
Terminal=false
Type=Application
Categories=Utility;
Keywords=unikey;telex;vni;vietnamese;gotv;
StartupNotify=true
StartupWMClass=gotv
DESKTOP
    cat > "$DESTDIR/etc/xdg/autostart/gotv.desktop" <<'DESKTOP'
[Desktop Entry]
Name=GoTV
Comment=GoTV indicator
Exec=/usr/bin/gotv --tray
Icon=gotv
Terminal=false
Type=Application
X-GNOME-Autostart-enabled=true
DESKTOP
    if [[ -z "$DESTDIR" ]]; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
        update-desktop-database /usr/share/applications || true
        restart_ibus_sessions
        echo 'Đã cài. Mở GoTV rồi chọn bằng Super+Space.'
    else
        echo "Đã tạo cây cài đặt: $DESTDIR"
    fi
}

# Restart IBus trong từng phiên desktop đang hoạt động để nhận engine mới
# mà không cần đăng xuất. Không bao giờ làm fail cài đặt; báo cáo từng user.
restart_ibus_sessions() {
    local sock rundir uid user t u dup
    local -a candidates=() users=()
    command -v ibus >/dev/null 2>&1 || {
        echo 'Chưa cài gói ibus nên bỏ qua restart (cài ibus rồi chạy: ibus restart).'
        return 0
    }
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != root ]]; then
        candidates+=("$SUDO_USER")
    else
        # Đăng nhập root trực tiếp: quét mọi session bus đang hoạt động.
        for sock in /run/user/*/bus; do
            [[ -S "$sock" ]] || continue
            uid=$(stat -c %u "$sock" 2>/dev/null || true)
            [[ -n "$uid" && "$uid" != 0 ]] || continue
            user=$(id -nu "$uid" 2>/dev/null || true)
            [[ -n "$user" ]] && candidates+=("$user")
        done
    fi
    if [[ ${#candidates[@]} -gt 0 ]]; then
        for t in "${candidates[@]}"; do
            dup=""
            for u in "${users[@]}"; do [[ "$u" == "$t" ]] && dup=1; done
            [[ -z "$dup" ]] && users+=("$t")
        done
    fi
    if [[ ${#users[@]} -eq 0 ]]; then
        echo 'Không thấy phiên desktop nào để restart IBus (đăng nhập desktop rồi chạy: ibus restart).'
        return 0
    fi
    for u in "${users[@]}"; do
        uid=$(id -u "$u" 2>/dev/null || true)
        [[ -n "$uid" ]] || continue
        rundir="/run/user/$uid"
        [[ -S "$rundir/bus" ]] || continue
        echo "Restart IBus cho $u..."
        if sudo -u "$u" env XDG_RUNTIME_DIR="$rundir" \
                DBUS_SESSION_BUS_ADDRESS="unix:path=$rundir/bus" \
                ibus restart >/dev/null 2>&1 \
            && sudo -u "$u" env XDG_RUNTIME_DIR="$rundir" \
                DBUS_SESSION_BUS_ADDRESS="unix:path=$rundir/bus" \
                ibus list-engine 2>/dev/null | grep -q gotv; then
            echo "IBus đã nhận engine GoTV cho $u."
        else
            echo "Chưa xong cho $u (thử đăng xuất/đăng nhập lại, hoặc chạy tay: ibus restart)."
        fi
    done
    return 0
}

# Mirror of cmd_install_linux: remove exactly what install laid down.
# User data (~/.config/gotv) is kept.
cmd_uninstall_linux() {
    : "${DESTDIR:=}"
    if [[ -z "$DESTDIR" && "$EUID" -ne 0 ]]; then
        echo "Cần sudo: sudo $PROG uninstall" >&2
        exit 1
    fi
    rm -f "$DESTDIR/usr/libexec/ibus-engine-gotv" \
        "$DESTDIR/usr/libexec/ibus-setup-gotv" \
        "$DESTDIR/usr/bin/gotv-demo" \
        "$DESTDIR/usr/bin/gotv" \
        "$DESTDIR/usr/local/bin/gotv" \
        "$DESTDIR/usr/local/bin/gotv-demo" \
        "$DESTDIR/usr/share/ibus/component/gotv.xml" \
        "$DESTDIR/usr/share/metainfo/gotv.metainfo.xml" \
        "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv.svg" \
        "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv-telex.svg" \
        "$DESTDIR/usr/share/icons/hicolor/scalable/apps/gotv-vni.svg" \
        "$DESTDIR/usr/share/applications/gotv.desktop" \
        "$DESTDIR/etc/xdg/autostart/gotv.desktop"
    rm -rf "$DESTDIR/usr/share/gotv"
    for size in 16 22 24 32 48 64 128 256; do
        rm -f "$DESTDIR/usr/share/icons/hicolor/${size}x${size}/apps/gotv.png"
        rm -f "$DESTDIR/usr/share/icons/hicolor/${size}x${size}/apps/gotv-telex.png"
        rm -f "$DESTDIR/usr/share/icons/hicolor/${size}x${size}/apps/gotv-vni.png"
    done
    if [[ -z "$DESTDIR" ]]; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
        update-desktop-database /usr/share/applications || true
        restart_ibus_sessions
        echo 'Đã gỡ. Giữ nguyên ~/.config/gotv.'
    else
        echo "Đã gỡ khỏi cây staging: $DESTDIR"
    fi
}

cmd_package_linux() {
    version=${1:-0.9.0-1}
    architecture=$(dpkg --print-architecture)
    dpkg --validate-version "$version"
    make build test
    staging=$(mktemp -d /tmp/gotv-package.XXXXXX)
    chmod 755 "$staging"
    trap 'rm -rf -- "$staging"' EXIT
    DESTDIR="$staging" cmd_install
    mkdir -p "$staging/DEBIAN"
    cat > "$staging/DEBIAN/control" <<CONTROL
Package: gotv
Replaces: gotiengviet
Conflicts: gotiengviet
Provides: gotiengviet
Version: $version
Section: utils
Priority: optional
Architecture: $architecture
Depends: ibus, libibus-1.0-5, libglib2.0-0, libgtk-3-0, libgdk-pixbuf-2.0-0, libayatana-appindicator3-1
Recommends: curl, libnotify-bin
Maintainer: GoTV Project <https://github.com/isthaison/gotv>
Homepage: https://github.com/isthaison/gotv
Description: Vietnamese Telex/VNI input method in native C
  Shared C input engine, GTK settings, indicator, spelling and macro support.
  Offline dictionary suggestions, updated from GitHub; no Go runtime is required.
CONTROL
    cat > "$staging/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null; then
    update-desktop-database /usr/share/applications || true
fi
rm -f /usr/local/bin/gotiengviet /usr/local/bin/gotiengviet-demo 2>/dev/null || true
mkdir -p /usr/local/bin 2>/dev/null || true
ln -sfn /usr/bin/gotv /usr/local/bin/gotv 2>/dev/null || true
ln -sfn /usr/bin/gotv-demo /usr/local/bin/gotv-demo 2>/dev/null || true
POST
    chmod -R u=rwX,go=rX "$staging"
    chmod 755 "$staging/DEBIAN"
    chmod 755 "$staging/DEBIAN/postinst"
    output="gotv_${version}_${architecture}.deb"
    dpkg-deb --root-owner-group --build "$staging" "$output"
    echo "Đã đóng gói: $output"
}

# Single writer for every place that carries the release version, so a
# release can no longer ship with half-bumped files. Idempotent.
cmd_bump() {
    local ver="${1:?Usage: $PROG bump X.Y.Z [\"release notes\"]}"
    [[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "bump: version must be X.Y.Z, got '$ver'" >&2; exit 1; }
    local notes="${2:-Phát hành $ver.}"
    local today csv old
    today=$(date +%F)
    csv="$(echo "$ver" | tr . ,),0"    # 0,7,0,0 for VERSIONINFO
    old=$(sed -n 's/^#define GTV_VERSION "\(.*\)"/\1/p' windows/version.h)
    [[ -n "$old" ]] || { echo "bump: cannot read current version from windows/version.h" >&2; exit 1; }

    printf '%s\n' "$ver" > VERSION
    sed -i "s/#define GTV_VERSION \"[^\"]*\"/#define GTV_VERSION \"$ver\"/" windows/version.h
    sed -i "s/Tags like v[0-9.]*/Tags like v$ver/" windows/version.h
    sed -i -E "s/^FILEVERSION[ \t]+[0-9,]+/FILEVERSION     $csv/" windows/resource.rc
    sed -i -E "s/^PRODUCTVERSION[ \t]+[0-9,]+/PRODUCTVERSION  $csv/" windows/resource.rc
    sed -i -E "s/(VALUE \"(File|Product)Version\", \")[0-9.]+/\\1$ver.0/" windows/resource.rc
    sed -i "s|<version>[^<]*</version>|<version>$ver</version>|" linux/ibus/gotv.xml
    sed -i 's/"[0-9][0-9.]*","GPL"/"'"$ver"'","GPL"/' linux/ibus/engine.c

    if grep -q "<release version=\"$ver\"" linux/ibus/gotv.metainfo.xml; then
        echo "(metainfo already has $ver, keeping existing entry)"
    else
        local esc_notes
        esc_notes=$(printf '%s' "$notes" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g')
        awk -v ver="$ver" -v date="$today" -v notes="$esc_notes" '
            /<releases>/ {
                print
                print "    <release version=\"" ver "\" date=\"" date "\">"
                print "      <description>"
                print "        <p>" notes "</p>"
                print "      </description>"
                print "    </release>"
                next
            }
            1' linux/ibus/gotv.metainfo.xml > linux/ibus/gotv.metainfo.xml.new
        mv linux/ibus/gotv.metainfo.xml.new linux/ibus/gotv.metainfo.xml
    fi

    sed -i "s/^VERSION ?= .*/VERSION ?= \$(GTV_VERSION)/" Makefile.win
    sed -i "s/^VERSION ?= .*/VERSION ?= \$(GTV_VERSION)-1/" Makefile
    sed -i 's/^\([ \t]*\)version=${1:-.*}/\1version=${1:-'"$ver"'-1}/' "$0"
    sed -i "s/#define AppVersion \".*\"/#define AppVersion \"$ver\"/" windows/installer.iss
    sed -i "s/#define AppVerNum \".*\"/#define AppVerNum \"$ver.0\"/" windows/installer.iss
    sed -i "/CFBundleShortVersionString/,+1 s|<string>[^<]*</string>|<string>$ver</string>|" macos/Info.plist
    sed -i "/CFBundleVersion/,+1 s|<string>[^<]*</string>|<string>$ver</string>|" macos/Info.plist
    # installer.iss carries Vietnamese shortcut names: keep UTF-8 with BOM.
    if [ "$(head -c 3 windows/installer.iss)" != "$(printf '\xef\xbb\xbf')" ]; then
        printf '\xef\xbb\xbf' | cat - windows/installer.iss > windows/installer.iss.new
        mv windows/installer.iss.new windows/installer.iss
        echo "(restored UTF-8 BOM in windows/installer.iss)"
    fi
    sed -i 's/\[ -n "\$VER" \] || VER="[^"]*"/[ -n "$VER" ] || VER="'"$ver"'"/' .github/workflows/build-windows.yml

    echo "--- bumped $old -> $ver ---"
    echo "--- leftover $old refs (history/tests/examples are fine) ---"
    grep -rnF "$old" --include="*.c" --include="*.h" --include="*.xml" --include="*.yml" \
        --include="*.iss" --include="Makefile*" --include="*.sh" . 2>/dev/null \
        | grep -v "^\./\.git" | grep -v "tests/" | grep -v "metainfo" || true
    echo "--- done (review with: git diff --stat) ---"
}

cmd_install_ollama() {
    echo "gtv.sh: Ollama suggestions were removed; word suggestions now come"
    echo "from the built-in offline dictionary (data/dict-vi.txt). Nothing to install."
}

command="${1:-help}"
case "$command" in
    build)   shift; cmd_build "$@" ;;
    test)    shift; cmd_test "$@" ;;
    vet)     cmd_vet ;;
    clean)   cmd_clean ;;
    install) cmd_install ;;
    install-ollama) cmd_install_ollama ;;
    uninstall) cmd_uninstall ;;
    package) shift; cmd_package "${1:-}" ;;
    bump)    shift; cmd_bump "${1:?Usage: $PROG bump X.Y.Z [\"notes\"]}" "${2:-}" ;;
    help|--help|-h) cmd_help ;;
    *) echo "Unknown command '$command'. Usage: $PROG <build|test|vet|clean|install|install-ollama|uninstall|package|bump|help>" >&2; exit 1 ;;
esac
