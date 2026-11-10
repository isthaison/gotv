# Đối chiếu tính năng theo nền tảng

Mục đích: một chỗ duy nhất để thấy tính năng nào có/thiếu ở đâu, khỏi mò từng thư mục.  
Kiến trúc chung: `engine/` (C, dùng chung 100%) + adapter native tương thích từng nền tảng (`linux/`, `windows/`, `macos/`).

Ký hiệu: ✅ có · ⚠️ một phần · ❌ chưa có · ➖ hệ điều hành lo.

## Gõ (lõi engine C dùng chung)

| Tính năng | Linux (IBus) | Windows (TSF) | macOS (InputMethodKit) |
|---|---|---|---|
| Telex / VNI / dấu hiện đại-truyền thống | ✅ | ✅ | ✅ |
| Hoàn tác phím tắt (`ww`→`w`, `dd`→`d`) | ✅ | ✅ | ✅ |
| Macro/emoji mở rộng bằng phím Tab | ✅ | ✅ | ✅ |
| Giữ phần gõ dở khi click chuột nơi khác | ✅ | ✅ (TSF composition) | ✅ (marked text) |
| Tương thích Terminal (Ghostty, iTerm2, Kitty...) | ✅ | ✅ | ✅ (NSUnderlineStyleNone) |

## Hiển thị và gợi ý

| Tính năng | Linux (IBus) | Windows (TSF) | macOS (InputMethodKit) |
|---|---|---|---|
| Bảng gợi ý inline / Candidate List | ✅ (IBus lookup table) | ✅ (overlay tray balloon) | ✅ (IMKCandidates native) |
| Gợi ý sửa lỗi chính tả ngoại tuyến | ✅ | ✅ | ✅ |
| Nhận gợi ý + học từ đã chọn | ✅ (phím số / Tab) | ✅ (click / Tab) | ✅ (phím 1..5 / Tab / click) |
| Enter không áp gợi ý (chỉ commit từ) | ✅ | ✅ | ✅ |
| Gợi ý emoji khi gõ tiền tố `:sm` | ✅ | ✅ | ✅ |
| Gạch chân chính tả gõ sai | ✅ (gạch đỏ) | ❌ (dùng balloon) | ❌ (dùng Candidate List) |

## Cấu hình & Giao diện

| Tính năng | Linux (IBus) | Windows (TSF) | macOS (InputMethodKit) |
|---|---|---|---|
| Đổi Telex ↔ VNI trong UI | ✅ (tray + Setup) | ✅ (tray + Setup) | ✅ (Menu Bar + Preferences) |
| Chuẩn dấu / Chính tả trong UI | ✅ (Setup) | ✅ (Setup) | ✅ (Preferences + Menu Bar) |
| Bật/tắt gợi ý từ điển trong UI | ✅ (Setup) | ✅ (Setup) | ✅ (Preferences + Menu Bar) |
| Quản lý macro/emoji trong UI | ❌ (sửa file text) | ❌ (sửa file text) | ✅ (Cửa sổ GtvDataWindow) |

## Khay, menu, hệ thống

| Tính năng | Linux (IBus) | Windows (TSF) | macOS (InputMethodKit) |
|---|---|---|---|
| Menu trạng thái nhanh | ✅ AppIndicator | ✅ Tray icon Windows | ✅ Menu Bar input extra |
| Phím tắt chuyển bàn phím | ✅ `Super + Space` | ✅ `Win + Space` | ✅ `Control + Space` / `Fn` |
| Tuân thủ Sandbox & bảo mật TCC | ✅ | ✅ | ✅ (100% chuẩn Apple IMK) |
| Tự khởi động cùng hệ điều hành | ✅ xdg autostart | ✅ registry / task | ➖ Hệ thống tự quản lý IMK |
| Bỏ qua ô mật khẩu an toàn | ✅ | ✅ | ✅ (SecureInput) |

## Cập nhật theo release GitHub

| Tính năng | Linux (IBus) | Windows (TSF) | macOS (InputMethodKit) |
|---|---|---|---|
| Tự kiểm tra định kỳ | ✅ | ✅ | ✅ |
| Kiểm tra thủ công trong UI | ✅ menu tray | ✅ menu tray | ✅ Menu Bar |
| Nâng cấp tự động | ✅ `pkexec dpkg -i` | ✅ Silent in-place | ⚠️ Tải .pkg / .dmg về mở Finder |
