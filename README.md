# GoTV — Bộ gõ tiếng Việt Telex/VNI cho Linux, Windows & macOS

[![Build Linux](https://github.com/isthaison/gotv/actions/workflows/build-linux.yml/badge.svg)](https://github.com/isthaison/gotv/actions/workflows/build-linux.yml)
[![Build Windows](https://github.com/isthaison/gotv/actions/workflows/build-windows.yml/badge.svg)](https://github.com/isthaison/gotv/actions/workflows/build-windows.yml)
[![Build macOS](https://github.com/isthaison/gotv/actions/workflows/build-macos.yml/badge.svg)](https://github.com/isthaison/gotv/actions/workflows/build-macos.yml)
[![Release](https://img.shields.io/github/v/release/isthaison/gotv)](https://github.com/isthaison/gotv/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
🌐 Trang chủ: **<https://isthaison.github.io/gotv/>**

Từ phiên bản 0.9.0, dự án của **isthaison** đổi tên từ GoTiengViet thành **GoTV**
để tránh nhầm với phần mềm GoTiengViet của Trần Kỳ Nam. Hai dự án độc lập.
Xem [hướng dẫn chuyển đổi](docs/rename-gotv.md) cho người đang dùng bản cũ.

Bộ gõ tiếng Việt mã nguồn mở: một **lõi gõ duy nhất viết bằng C** (không Go, không CGO, không runtime ngoài),
chạy trên cả ba hệ điều hành qua adapter mỏng của từng nền tảng —
[IBus](https://github.com/ibus/ibus) trên Linux, [TSF Text Service](https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework) trên Windows,
[InputMethodKit](https://developer.apple.com/documentation/inputmethodkit) trên macOS.

> **English:** GoTV is a free, open-source **Vietnamese input method (Telex & VNI)** for
> Linux (IBus), Windows 10/11 (TSF) and macOS (InputMethodKit) — a lightweight Unikey/EVKey
> alternative with offline spellcheck, text macros/emoji and an offline dictionary
> (word suggestions + typo corrections, auto-updated from GitHub).

```text
$ build/gotv-demo --transform telex duocjwd
được
```

## Tính năng

- **Một thuật toán chung** cho Telex và VNI — chỉ khác bảng ánh xạ phím; kiểu đặt dấu hiện đại (`hòa`) hoặc truyền thống (`hoà`).
- **Gõ mượt mà, không gạch đỏ làm phiền**: Giao diện phẳng sạch sẽ trên cả Linux, Windows và macOS; loại bỏ hoàn toàn cảnh báo gõ sai âm tiết hay gạch chân đỏ để người dùng thoải mái gõ xen kẽ tiếng Việt, tiếng Anh và viết mã code.
- **macOS chuẩn Native IMK**: tuân thủ 100% kiến trúc Apple InputMethodKit, không cần cấp quyền Accessibility; gõ trơn tru không gạch chân trên mọi Terminal (Ghostty, iTerm2, Kitty...) và GUI app; có menu trạng thái thanh tác vụ và bảng ứng viên gợi ý.
- **Gợi ý từ điển ngoại tuyến**: hoàn thành tiền tố theo bộ từ điển tiếng Việt chính quy (~6.700 từ chuẩn mực theo quy chuẩn của Viện Ngôn ngữ học, Bộ GD&ĐT và chuẩn quốc tế Hunspell vi_VN) kết hợp từ điển tự học riêng của bạn, hoạt động offline 100%.
- **Macro/emoji bằng file text**: tự thêm `cty=Công ty TNHH` (gõ `cty` rồi nhấn **Tab** để bung (macro/emoji đều cần Tab), phím Space giữ nguyên chữ), `:smile:`→😊, `(y)`→👍. Có sẵn bảng quản lý trực quan trên macOS.
- **Nhẹ, riêng tư**: lõi C thuần + glib; không runtime nặng nề, không gửi dữ liệu phím đi đâu, mọi gợi ý chạy offline trên máy.
- **Tự cập nhật**: kiểm tra release GitHub định kỳ, nâng cấp đè giữ nguyên cấu hình cá nhân.

## Trạng thái nền tảng

| Nền tảng | Cách gõ | Trạng thái |
|---|---|---|
| Linux (Ubuntu/GNOME) | IBus engine + app khay (indicator) | ✅ Ổn định — `.deb` ở [Releases](https://github.com/isthaison/gotv/releases/latest) |
| Windows 10/11 | TSF Text Service + app khay | ✅ Ổn định — `gotv-*-x64-setup.exe` ở [Releases](https://github.com/isthaison/gotv/releases/latest) |
| macOS (Apple Silicon & Intel) | InputMethodKit `.app` (Native IMK) | ✅ Ổn định — `gotv-*-macos.pkg` / `.dmg` ở [Releases](https://github.com/isthaison/gotv/releases/latest) |

## Bắt đầu nhanh

**Linux** (Ubuntu/GNOME):

```sh
sudo apt install build-essential pkg-config libibus-1.0-dev libgtk-3-dev \
  libayatana-appindicator3-dev librsvg2-bin
./gtv.sh build
sudo ./gtv.sh install
```

Rồi vào **Settings → Keyboard → Input Sources** thêm **GoTV**
(một nguồn duy nhất; chuyển Telex/VNI trong app indicator hoặc Setup),
chuyển bằng `Super+Space`. Chi tiết: [docs/dev.md](docs/dev.md).

**Windows 10/11**:
- Cài qua Windows Package Manager (**Winget**):
  ```cmd
  winget install isthaison.GoTV
  ```
- Hoặc tải trực tiếp bộ cài `gotv-<version>-x64-setup.exe` tại [Releases](https://github.com/isthaison/gotv/releases/latest) (cài đặt per-user nhanh chóng, không bắt buộc quyền admin).
- Đăng xuất/đăng nhập lại hoặc khởi động app → chuyển đổi bằng phím tắt `Ctrl+Shift` / `Alt+Z` hoặc `Win+Space`. Chi tiết: [docs/windows.md](docs/windows.md).

**macOS**: tải `gotv-<version>-macos.pkg` hoặc `.dmg` ở trang
[Releases](https://github.com/isthaison/gotv/releases/latest).
- Với gói `.pkg`: Nhấp đúp chuột để chạy cài đặt tự động vào `/Library/Input Methods/`.
- Với file `.dmg`: Kéo thả `GoTV.app` vào thư mục `Input Methods`.
Đăng xuất/đăng nhập lại → vào **System Settings → Keyboard → Input Sources** bấm **+** thêm **GoTV**.
Chuyển đổi nguồn gõ bằng `Fn/Caps Lock` hoặc `Control+Space`. Chi tiết: [docs/macos.md](docs/macos.md).

## Quy tắc gõ (Telex/VNI)

| Thao tác | Telex | VNI |
|---|---|---|
| Sắc, huyền, hỏi, ngã, nặng | `s f r x j` | `1 2 3 4 5` |
| Mũ (`â ê ô`) | lặp nguyên âm (`aa`) | `6` |
| Móc/trăng (`ơ ư`, `ă`) | `w` | `7` / `8` |
| Gạch chữ `đ` | `dd` | `9` |
| Xóa dấu thanh | `z` | `0` |
| Chữ thô (không dấu) | gõ lặp (`ass` → `as`) | gõ lặp tương tự |

Ví dụ: `bawst` → `bắt`, `duocjwd` → `được`, `hoaf` → `hoà`/`hòa` (tùy kiểu đặt dấu).

## So với Unikey / EVKey / OpenKey

|  | GoTV | Unikey | EVKey/OpenKey |
|---|---|---|---|
| Telex + VNI | ✅ | ✅ | ✅ |
| Linux (IBus) / Windows / macOS | ✅ cả 3 (Native IMK) | Win + Linux | Win + macOS |
| Gợi ý từ điển offline | ✅ (hoàn thành từ, không ép sửa lỗi) | ❌ | ❌ |
| Mã nguồn mở | ✅ MIT | ✅ | ✅ |

## Tài liệu

- [Cấu hình & macro/emoji/từ điển](docs/config.md) · [Bản Windows](docs/windows.md) ·
  [Bản macOS](docs/macos.md) · [Phát triển & đóng gói](docs/dev.md) ·
  [Đối chiếu tính năng theo nền tảng](docs/parity.md) ·
  [Trang chủ](https://isthaison.github.io/gotv/)

## Lệnh thường dùng

```sh
build/gotv-demo --transform telex duocjwd   # thử thuật toán
build/gotv-demo --suggest thong            # hoàn thành tiền tố (offline)
build/gotv-demo --correct kông            # sửa lỗi (offline)
ibus-setup-gotv            # GUI Linux (tự bật tray nền)
ibus-setup-gotv --tray     # chỉ indicator
ibus-setup-gotv --cli      # terminal
./gtv.sh build | test | vet | install | uninstall | package | bump | help
# Windows (MSYS2 MinGW hoặc cmd):  gtv build | test | install | uninstall | package
# macOS: ./gtv.sh build | test | install | uninstall | package  (app vào ~/Library/Input Methods)
```

## Xử lý sự cố

- **Không thấy GoTV trong Input Sources (Linux):** `ibus restart`, đăng xuất/đăng nhập lại,
  kiểm tra `/usr/share/ibus/component/gotv.xml`.
- **Gõ không ra dấu (Linux):** đang ở engine `gotv`? Kiểm tra `Super+Space` và
  `method` trong `~/.config/gotv/config`.
- **Không thấy GoTV sau khi cài (Windows):** đăng xuất/đăng nhập lại;
  Settings → Time & Language → Language → Options → Add a keyboard → GoTV.
- **Không thấy GoTV sau khi cài (macOS):** đăng xuất/đăng nhập lại (hoặc chạy `killall -9 GoTV` trong Terminal);
  System Settings → Keyboard → Text Input → Edit → Bấm (+) → Vietnamese → GoTV.
- **Crash (Linux):** xem `~/.cache/gotv/crash.log`, báo kèm phiên bản và log liên quan.
- **Updater (Windows):** xem `%APPDATA%\gotv\update.log`.

## Đóng góp & Cộng đồng (Community & Contributing)

Dự án phát triển hoàn toàn vì cộng đồng và rất trân trọng mọi sự chung tay:
- Xem **[Hướng dẫn đóng góp (CONTRIBUTING.md)](CONTRIBUTING.md)** để biết cách gửi báo cáo lỗi, đóng góp từ điển, macro hoặc mã nguồn.
- Thảo luận và góp ý tính năng mới tại **[GitHub Discussions](https://github.com/isthaison/gotv/discussions)** hoặc [GitHub Issues](https://github.com/isthaison/gotv/issues).
- Mọi đóng góp code cần vượt qua kiểm thử: `./gtv.sh test` (Linux/macOS) hoặc `.\gtv.cmd test` (Windows).

> 💡 **Gợi ý topics cho repo** (mục About → ⚙️): `vietnamese` `tieng-viet` `telex` `vni`
> `input-method` `ime` `ibus` `tsf` `unikey` `linux` `windows` `macos` `dictionary` `c`

## Star History & Stargazers

<a href="https://star-history.com/#isthaison/gotv&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=isthaison/gotv&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=isthaison/gotv&type=Date" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=isthaison/gotv&type=Date" />
 </picture>
</a>

Cảm ơn các bạn đã ủng hộ dự án! Xem danh sách chi tiết những người đã star tại **[GitHub Stargazers](https://github.com/isthaison/gotv/stargazers)**.

Giấy phép: [MIT](LICENSE).
