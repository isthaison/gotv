# Chuyển sang GoTV 0.9.0

Cảm ơn người góp ý ở [issue #1](https://github.com/isthaison/gotv/issues/1).
Dự án của isthaison đổi tên để phân biệt với GoTiengViet của Trần Kỳ Nam;
đây là hai dự án độc lập, không phải bản kế nhiệm hay bản chính thức của nhau.

## Tên và dữ liệu mới

- Windows: `gotv.exe`, `gotv-<version>-x64-setup.exe`, thư mục cài mới `Programs\GoTV`.
- Linux: gói `gotv`, `ibus-engine-gotv`, `ibus-setup-gotv`, engine IBus `gotv`.
- macOS: `GoTV.app`, `gotv-<version>-macos.dmg` / `.pkg`.
- Thư viện lõi: `libgotv.a`. Lệnh phát triển vẫn là `gtv.sh` / `gtv.cmd`.

Khi truy cập cấu hình, dữ liệu hoặc cache lần đầu, GoTV sao chép các file còn thiếu
từ thư mục `gotiengviet` sang `gotv` trong cùng thư mục người dùng.
File đã có ở `gotv` được giữ nguyên; dữ liệu cũ không bị xóa.
File `.legacy-imported` đánh dấu chuyển đổi xong để file người dùng xóa sau này
không bị nhập lại. Nếu sao chép thất bại, app ghi cảnh báo và thử lại lần sau.
Nên đóng bản cũ trước khi dùng GoTV để thay đổi mới không tiếp tục ghi vào dữ liệu cũ.

## Nâng cấp

Windows giữ AppId, CLSID và GUID bàn phím TSF. Bộ cài dùng lại thư mục cài
hiện có, nên bản nâng cấp có thể vẫn nằm trong `Programs\GoTiengViet`.
Các tiến trình đang mở tiếp tục dùng engine cũ tới khi khởi động lại.
Tên mutex, sự kiện takeover và lớp cửa sổ IPC được giữ để các phiên bản liên lạc.
Khóa Run được chuyển sang `GoTV` khi trỏ tới cùng thư mục cài.
Tên scheduled task elevated được giữ để tránh tạo hai tác vụ.
Bản sao `gotiengviet.exe` trong payload mới cho phép launcher cũ chuyển tiếp;
đó là cùng binary GoTV, không có một implementation khác.

Linux: gói `gotv` khai báo Replaces/Breaks/Provides cho gói `gotiengviet` cũ.
Sau cài đặt, khởi động lại IBus/đăng nhập và chọn **GoTV** trong Input Sources.
Nếu bản cũ được cài thủ công thay vì qua gói `.deb`, gỡ bản cũ bằng script
của checkout cũ trước khi cài GoTV, giữ nguyên dữ liệu người dùng.

macOS: gỡ input source và app cũ **của isthaison**, rồi cài `GoTV.app`,
đăng xuất/đăng nhập và chọn GoTV. Không xóa app GoTiengViet của Trần Kỳ Nam.
Cấu hình vẫn được nhập tự động như trên.

## Tương thích GitHub

Repo chính thức là `isthaison/gotv`, website là `https://isthaison.github.io/gotv/`.
URL repo cũ được GitHub chuyển hướng; trình cập nhật dùng curl với `--location`
nên bản đã phát hành vẫn truy cập được release. GitHub Pages không chuyển hướng
đường dẫn website cũ; hãy cập nhật bookmark sang địa chỉ mới.
Từ phiên bản 0.9.9+, các file alias phụ `gotiengviet-*` / `gotiengviet_*` đã được loại bỏ hoàn toàn,
tất cả các bản phát hành chỉ sử dụng duy nhất định dạng chuẩn `gotv-*` / `gotv_*`.

Kiểm chứng local trên Windows không thay thế kiểm chứng cài đặt/nâng cấp trên
Linux và macOS; hai nền tảng đó cần CI và thử input source thực tế.
