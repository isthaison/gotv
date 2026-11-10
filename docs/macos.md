# Bản macOS (Native InputMethodKit)

> ✅ **Ổn định**: Bộ gõ chạy 100% qua kiến trúc chuẩn [InputMethodKit (IMK)](https://developer.apple.com/documentation/inputmethodkit) của Apple, hỗ trợ đầy đủ cho cả vi xử lý Apple Silicon (M1/M2/M3/M4) và chip Intel (x86_64).

`GoTV.app` là một Input Method Service chạy nền trong hệ thống (LSUIElement agent, không hiển thị icon trên Dock), dùng chung lõi xử lý C với bản Linux và Windows — đồng nhất 100% quy tắc Telex/VNI, từ điển offline, macro và emoji.

---

## 1. Điểm nổi bật & Khắc phục sự cố Terminal (Issue #3)

- **100% Chuẩn Apple IME / IMK (Cơ chế 1)**:
  - Hoạt động đúng chuẩn giao thức `IMKServerInput` và `IMKInputController`.
  - Không dùng cơ chế can thiệp phím giả lập (`CGEventPost`), không đòi hỏi quyền trợ năng (*Accessibility / TCC*), tuân thủ tuyệt đối môi trường Sandbox và chính sách bảo mật macOS.
- **Tương thích hoàn hảo với Terminal (Ghostty, iTerm2, Kitty, Terminal.app)**:
  - Khắc phục triệt để lỗi gạch chân làm mất thẩm mỹ và gián đoạn tính năng autocompletion của Shell bằng thuộc tính `NSUnderlineStyleNone`.
  - Kiểm tra trạng thái vùng đánh dấu an toàn (`reconcileMarkedText:`), không bao giờ bị xóa trắng bộ nhớ đệm phím (buffer wipe) ngay cả khi người dùng tắt marked text trong thiết lập của terminal.
- **Bảng ứng viên Candidate List Native**:
  - Tích hợp lớp `IMKCandidates` với giao diện hàng ngang (`kIMKSingleRowSteppingCandidatePanel`).
  - Tự động hiển thị gợi ý từ điển và sửa lỗi chính tả ngoại tuyến ngay tại vị trí con trỏ nhập liệu.
  - Chọn nhanh từ bằng các phím số `1..5`, phím `Tab` hoặc click chuột. Từ được chọn sẽ tự động được ghi nhớ vào từ điển học cá nhân.
- **Menu Bar Status Menu**:
  - Tích hợp menu ngữ cảnh ngay tại biểu tượng bàn phím trên thanh Menu Bar của macOS:
    - Bật/tắt nhanh giữa kiểu gõ **Telex** và **VNI**.
    - Bật/tắt chế độ **Dấu kiểu mới** (`hòa` vs `hoà`).
    - Bật/tắt **Kiểm tra chính tả ngoại tuyến**.
    - Bật/tắt **Gợi ý từ điển & Sửa lỗi**.
    - Mở nhanh **Cài đặt...** hoặc **Quản lý Dữ liệu...** (Macro/Emoji).
    - **Kiểm tra bản cập nhật mới...** từ GitHub.
- **Giao diện Cài đặt & Dữ liệu native Cocoa**:
  - Cửa sổ Cài đặt (`GtvPreferencesWindow`): Tùy chỉnh trực quan mọi thông số cấu hình của GoTV.
  - Cửa sổ Dữ liệu (`GtvDataWindow`): Trình quản lý danh sách gõ tắt Macro và biểu tượng Emoji.

---

## 2. Hướng dẫn cài đặt

Tải file cài đặt từ trang **[Releases chính thức](https://github.com/isthaison/gotv/releases/latest)**:

### Cách 1: Cài đặt bằng file `.pkg` (Khuyến nghị)
1. Tải `gotv-<version>-macos.pkg`.
2. Nhấp đúp chuột vào file `.pkg` và làm theo các bước hướng dẫn trên màn hình. Trình cài đặt sẽ tự động đặt ứng dụng vào `/Library/Input Methods/GoTV.app`.

### Cách 2: Cài đặt bằng file `.dmg`
1. Tải `gotv-<version>-macos.dmg`.
2. Mở file `.dmg` và kéo thả `GoTV.app` vào thư mục `Input Methods` (hoặc `~/Library/Input Methods/`).

### Kích hoạt bộ gõ trong Hệ thống macOS
1. Đăng xuất và đăng nhập lại tài khoản macOS (hoặc mở Terminal chạy `killall -9 GoTV` để hệ thống tải lại Input Method).
2. Mở **System Settings** (Cài đặt hệ thống) → **Keyboard** (Bàn phím) → **Text Input** (Nhập văn bản) → bấm nút **Edit...** (Sửa...).
3. Bấm nút dấu cộng **(+)** ở góc trái dưới → Tìm ngôn ngữ **Vietnamese** (Tiếng Việt) → Chọn **GoTV** → Bấm **Add** (Thêm).
4. Chuyển đổi giữa các bàn phím bằng phím tắt `Fn/Caps Lock` hoặc `Control + Space`.

> 💡 **Lưu ý về Gatekeeper**: Do GoTV là dự án mã nguồn mở cộng đồng miễn phí chưa đăng ký chứng chỉ thương mại trả phí của Apple, nếu macOS hiện cảnh báo *"Unidentified Developer"*, bạn chỉ cần vào **System Settings → Privacy & Security** (Quyền riêng tư & Bảo mật) → cuộn xuống mục Bảo mật và bấm **Open Anyway** (Vẫn mở).

---

## 3. Quản lý Cấu hình & Dữ liệu

Vị trí lưu trữ dữ liệu cá nhân của GoTV trên máy Mac:
- Thư mục chuẩn macOS: `~/Library/Application Support/gotv/` (hoặc `~/.config/gotv/`).
- Các file cấu hình dạng text UTF-8:
  - `config`: Thiết lập kiểu gõ (`method=telex` hoặc `vni`), chuẩn dấu (`modern=true`), chính tả (`spellcheck=true`), gợi ý (`suggest=true`).
  - `macros.txt`: Danh sách từ gõ tắt cá nhân (`cty=Công ty TNHH`).
  - `emojis.txt`: Bảng mã emoji tùy chỉnh.
  - `learned-words.txt`: Các từ vựng mới mà bộ gõ đã tự học từ thói quen gõ của bạn.

---

## 4. Dành cho nhà phát triển (Build từ Source)

Yêu cầu cài đặt môi trường thông qua Homebrew:
```sh
brew install glib pkg-config
```

### Chạy kiểm thử tự động (Unit Tests)
```sh
./gtv.sh test
```
(Chạy 26 bài test bao phủ toàn bộ lõi C: Telex/VNI engine, kiểm tra chính tả, gợi ý từ điển, cấu hình và hoán vị phím).

### Biên dịch ứng dụng `GoTV.app`
```sh
./gtv.sh build
```
Lệnh trên sẽ biên dịch mã nguồn C và Objective-C Cocoa/IMK thành bundle hoàn chỉnh tại `build/macos/GoTV.app`.

### Cài đặt thử nghiệm vào hệ thống
```sh
./gtv.sh install
```
Ứng dụng sẽ được sao chép vào `~/Library/Input Methods/` của người dùng hiện tại.

---

Xem thêm: [README](../README.md) · [Cấu hình](config.md) · [Phát triển](dev.md) · [Đối chiếu tính năng](parity.md) · [Trang chủ](https://isthaison.github.io/gotv/)
