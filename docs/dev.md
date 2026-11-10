# Phát triển

## Build và kiểm thử (mọi nền tảng — một cửa lệnh)

```sh
./gtv.sh build        # Linux: make build test | Win: Makefile.win | mac: core + app
./gtv.sh test         # chạy test suite của OS hiện tại
./gtv.sh vet          # build + test nghiêm (-Werror nếu hỗ trợ)
./gtv.sh clean        # xóa build/ và release-pkg/
# Windows cmd (MSYS2/Git Bash đằng sau): gtv build | test | vet | clean
```

Linux giữ `make ...` trực tiếp vẫn dùng được:

```sh
make build                         # 3 binary + libgotv.a
make test                          # engine + support + thuật toán
make vet                           # build + test với -Werror
make test-ibus                     # vòng đời IBus
make test-ui                       # giao diện setup
make help
```

Mọi lệnh shell gom trong một file duy nhất: `./gtv.sh <build|test|vet|clean|install|uninstall|package|bump|help>`.

`make test` gồm: Telex/VNI, stateful, config, từ điển, gợi ý kết hợp, chính tả, macro/emoji, config mặc định, từ điển học, so version/cập nhật, hoán vị thao tác, 20.000 chuỗi ngẫu nhiên.

Binary trong `build/`: `ibus-engine-gotv` (`--ibus` trong IBus), `ibus-setup-gotv` (GUI/`--tray`/`--cli`), `gotv-demo`, `libgotv.a`.

## Cài đặt, gỡ và đóng gói

```sh
sudo ./gtv.sh install              # Linux: chép file hệ thống + ibus restart
sudo ./gtv.sh uninstall            # Linux: gỡ đúng danh sách install (giữ ~/.config)
./gtv.sh package [VERSION]         # Linux: .deb
make package VERSION=X.Y-1         # tương đương (Linux)
```

Windows (MSYS2 MinGW, hoặc `gtv ...` từ cmd):

```sh
./gtv.sh package                   # staging + iscc -> gotv-X.Y.Z-x64-setup.exe
./gtv.sh install                   # dựng setup (nếu thiếu) + cài silent + chạy app
./gtv.sh uninstall                 # gỡ silent + dọn Run/task/profile thừa
```

Store direct-download installer (Windows/MSYS2 with Inno Setup):

```sh
make -f Makefile.win store-setup  # gotv-X.Y.Z-x64-store-setup.exe
make -f Makefile.win store-staging # payload riêng tại release-pkg-store/
```

The Store variant is separate from the GitHub installer and disables the
GitHub self-updater. `store-setup` creates an unsigned local installer; sign
every `.exe`/`.dll` in `release-pkg-store/` and the installer itself with a
Microsoft-trusted code-signing certificate before submission. CI signing
configuration is documented in [Windows Store distribution](windows.md#microsoft-store).

macOS: `./gtv.sh package` (zip `GoTV.app`), `./gtv.sh install` (chép vào
`~/Library/Input Methods`), `./gtv.sh uninstall` (xóa app, giữ `~/.config`).

Giữ nguyên cấu hình người dùng; cuối cài đặt tự `ibus restart` từng phiên desktop (báo chứ không fail nếu không được). Thử không chạm hệ thống: `DESTDIR=/tmp/gotv-staging ./gtv.sh install`. `make clean` chỉ xóa `build/`. Gỡ thủ công: xóa `/usr/libexec/ibus-engine-gotv`, `/usr/libexec/ibus-setup-gotv`, `/usr/bin/gotv*`, `/usr/share/ibus/component/gotv.xml`, rồi `ibus restart`.

## Cấu trúc mã nguồn

```text
engine/           lõi dùng chung (C): compose, charset, phonology, config,
                  spell, macro/emoji, dict/suggest (từ điển offline),
                  learn, update (check version GitHub), text, telex/vni adapter
linux/
  ibus/           adapter IBus: engine.c, gotv.xml/icons
  setup/          GTK, indicator, CLI
windows/          app khay + setup + update (C: main/app/tray/tray_ai/
                  setup/update/tsf_install/win_utf) + tsf/ (C++ TSF Text Service)
macos/            IME InputMethodKit: controller, main, Info.plist (bundle),
                  SetupWindowController (cửa sổ Cài đặt & Quản lý Macro/Emoji)
site/             trang chủ GitHub Pages (HTML/CSS tĩnh + SEO meta),
                  deploy bằng .github/workflows/pages.yml
.github/          CI: build-linux, build-windows, build-macos (test lõi +
                  đóng gói .deb/.exe/.pkg/.dmg), pages (deploy site)
mk/common.mk      danh sách file dùng chung (ENGINE_SRCS/WIN_SRCS/TSF_SRCS)
VERSION           số version duy nhất (Makefile/Makefile.win đọc từ đây;
                  bump bằng ./gtv.sh bump, không sửa tay)
tools/
  demo/           demo terminal dùng chung mọi nền tảng
tests/            kiểm thử C (+ fake curl)
data/             macro, emoji, config, dict-vi (từ điển), seed từ điển học
```

Quy ước đa nền tảng: `engine/` không chứa code riêng OS nào
(chỉ glib/gio); mỗi OS là một adapter mỏng trong `linux/`, `windows/`,
`macos/` gọi vào engine. Thêm file nguồn mới thì khai báo ở
`mk/common.mk` (Windows) — Linux tự glob `engine/*.c`.

## DLL bị inject: `gtv_tsf.dll` + `gtv_engine.dll` (Windows)

Windows nạp TSF text service vào **mọi** process nhận text input. Loader
Windows resolve import **theo tên file**, không quan tâm thư mục: app host đã
nạp `zlib1.dll` của riêng nó thì DLL ta muốn nạp cùng tên sẽ bị chính nó chiếm.
Microsoft Teams (Chromium) đóng gói `zlib1.dll` chỉ export API `z_*`, nên
import `deflate` của glib rơi vào đó và app chết với *"The procedure entry
point deflate could not be located in ...libgio-2.0-0.dll"*.

Vì vậy hai DLL này **không được import DLL của bên thứ ba nào**:
glib/gio/zlib/intl/iconv/pcre2/libffi/libstdc++/libgcc/winpthread đều static
link **vào trong** `gtv_engine.dll` (`Makefile.win`, `GLIB_STATIC_DEFS`); nó
chỉ còn import DLL hệ thống. `gotv.exe` thì vẫn dùng glib dạng shared
(bình thường, nó là process riêng).

Hệ quả bắt buộc khi sửa:

- Engine dùng glib phải biên dịch trong cây riêng `build/win-static/`
  (ABI static khác shared: `dllimport` vs gọi trực tiếp) — không trộn với
  object đi vào `gotv.exe`.
- Thêm thư viện mới cho engine ⇒ thêm vào `GLIB_STATIC_LIBS` (static) và vào
  `STATIC_SYS_LIBS` (DLL hệ thống).
- `make -f Makefile.win check-tsf-isolated` (đã nằm trong `test` và `staging`)
  fail nếu `gtv_tsf.dll`/`gtv_engine.dll` import bất kỳ DLL nào không thuộc
  danh sách hệ thống.

Mọi lệnh shell gom trong `./gtv.sh` duy nhất (`build|test|vet|clean|install|package|bump|help`).

## API thư viện C

Liên kết `build/libgotv.a`, `#include "engine/engine.h"`. Chuỗi/mảng trả về thuộc về caller (`g_free`/`g_ptr_array_unref`).

```c
gtv_init();
gchar *out = gtv_transform("duocjwd", GTV_TELEX, TRUE); // "được"

GtvConfig cfg;
gtv_config_load(&cfg, "/home/ban/.config/gotv");
GtvEngine *e = gtv_engine_new(&cfg);
guint backspaces = 0;
gchar *commit = gtv_engine_process(e, 's', &backspaces); // NULL khi đang soạn
g_free(gtv_engine_buffer(e));
gtv_engine_free(e);

GPtrArray *fix = gtv_suggest_combined(&cfg, "", "kông", TRUE); // offline: ["không", ...]
gboolean ok = spell_word_valid("được"); // ngoại tuyến
gint cmp = gtv_version_compare("v0.6.0", "0.5.3"); // > 0
gtv_config_clear(&cfg);
```

Luồng bất đồng bộ cho UI: `gtv_suggest_combined_async()` / `gtv_suggest_combined_finish()`.

## Cắt release

1. Bump một lệnh (đừng sửa tay từng file — script là nơi duy nhất ghi version): `make bump V=X.Y.Z ["ghi chú"]` (chạy `./gtv.sh bump`). Kiểm tra `git diff`, rồi commit.
2. Commit, push tag `vX.Y.Z`.
3. CI Windows build `gotv-X.Y.Z-x64-setup.exe` (ký self-signed cert RSA 4096, cert lưu trong GitHub secret `WIN_CERT_PFX_B64`),
   CI Linux build `gotv_X.Y.Z-1_<arch>.deb`, CI macOS build `.dmg` + `.pkg`, cả ba đính kèm release để app tự cập nhật.
4. Linux: khay hệ thống có mục **Kiểm tra cập nhật...**

---
Xem thêm: [README](../README.md) · [Cấu hình](config.md) · [Bản Windows](windows.md) ·
[Bản macOS](macos.md) · [Trang chủ](https://isthaison.github.io/gotv/)
