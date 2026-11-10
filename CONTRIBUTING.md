# Hướng dẫn đóng góp cho GoTV (Contributing Guide)

Chào mừng và cảm ơn bạn đã quan tâm đóng góp cho **GoTV**! Dự án là phần mềm mã nguồn mở hoàn toàn (giấy phép MIT), phát triển bởi cộng đồng và vì cộng đồng người dùng tiếng Việt.

---

## 🌟 Bạn có thể đóng góp bằng những cách nào?

### 1. Dành cho tất cả mọi người (Không cần biết lập trình)
* **Báo cáo lỗi**: Trải nghiệm và gửi báo cáo lỗi nếu gặp tình trạng dính phím, gõ sai từ trên bất kỳ ứng dụng nào qua [GitHub Issues](https://github.com/isthaison/gotv/issues).
* **Đóng góp từ điển chuyên ngành**: Bộ gõ có tính năng gợi ý từ offline. Bạn có thể bổ sung từ vựng chuyên môn (Y tế, Luật, Kỹ thuật...) trong thư mục `data/dict-*.txt` hoặc mở Issue `[Dict]`.
* **Đóng góp bảng gõ tắt (Macro/Emoji)**: Thêm các cụm từ viết tắt phổ biến trong `data/macros.txt` và `data/emojis.txt`.
* **Lan tỏa dự án**: Tặng **⭐ Star** cho kho lưu trữ trên GitHub để giúp dự án đủ điều kiện xin cấp chứng chỉ số Authenticode và tăng độ tin cậy.

### 2. Dành cho Lập trình viên (Developers)
* Tìm các issue có gắn nhãn **`good first issue`** hoặc **`help wanted`**.
* **Kiến trúc mã nguồn**:
  * `engine/`: Lõi xử lý âm tiết tiếng Việt (C thuần, chuẩn POSIX/C99/C11, độc lập 100% không phụ thuộc GUI/OS).
  * `windows/`: TSF (Text Services Framework) Text Service và khay hệ thống cho Windows 10/11.
  * `macos/`: Native InputMethodKit adapter cho macOS (Apple Silicon & Intel).
  * `linux/`: IBus adapter cho Linux (Ubuntu/GNOME, Fedora, Arch).
  * `web/`: Trang chủ và bộ gõ web demo tương tác.

---

## 🛠️ Quy trình gửi Pull Request (PR)

1. **Fork** repo về tài khoản cá nhân của bạn.
2. Tạo nhánh tính năng mới:
   ```bash
   git checkout -b feature/ten-tinh-nang
   ```
3. Chạy kiểm thử toàn bộ trước khi commit:
   - Trên Windows: `.\gtv.cmd test`
   - Trên Linux/macOS: `./gtv.sh test`
4. Commit thay đổi tuân theo quy ước [Conventional Commits](https://www.conventionalcommits.org/):
   - `feat(...)`: Tính năng mới
   - `fix(...)`: Sửa lỗi
   - `docs(...)`: Cập nhật tài liệu
5. Đẩy lên fork của bạn và tạo **Pull Request** vào nhánh `main`.

Mọi đóng góp dù nhỏ nhất đều được ghi nhận và trân trọng!
