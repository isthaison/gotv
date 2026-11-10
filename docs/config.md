# Cấu hình

Thư mục cấu hình cá nhân:
- **Linux**: `$XDG_CONFIG_HOME/gotv/` (mặc định `~/.config/gotv/`).
- **Windows**: `%LOCALAPPDATA%\gotv\` (hoặc `%APPDATA%\gotv\`).
- **macOS**: `~/Library/Application Support/gotv/` (hoặc `~/.config/gotv/`).

| File | Nội dung |
|---|---|
| `config` | Kiểu gõ (`telex` / `vni`), kiểu đặt dấu (`modern=true`), chính tả (`spellcheck=false`), gợi ý từ điển (`suggest=true`) |
| `macros.txt`, `emojis.txt` | Bảng gõ tắt và emoji riêng do người dùng định nghĩa |
| `learned-words.txt`, `learned-corrections.txt` | Từ điển học thông minh (tự động cập nhật khi bạn chọn từ) |

Mặc định khi chạy: Kiểu gõ Telex, `modern=true` (dấu mới như `hòa`), `spellcheck=false` (tắt gạch đỏ kiểm tra chính tả mặc định để gõ mượt mà), `suggest=true`.

Khi chưa có file người dùng, bộ gõ dùng dữ liệu mặc định trong thư mục `data/` của ứng dụng.

---

## Macro và Emoji

Macro (gõ tắt) mở rộng khi bấm phím **Tab**:
- Gõ `vn` rồi nhấn **Tab** → `Việt Nam`
- `hn` + **Tab** → `Hà Nội`
- `dc` + **Tab** → `được`
- `ko` + **Tab** → `không`

Bấm phím **Space**, dấu câu hoặc phím **Enter** sẽ giữ nguyên chữ viết tắt (không tự động bung gõ tắt, giúp bạn hoàn toàn chủ động khi viết mã code hoặc viết tắt thông thường).

Emoji cũng bung bằng phím **Tab**:
- `:smile:` + **Tab** → 😊
- `:)` + **Tab** → 😊
- `<3` + **Tab** → ❤️

Đang gõ tiền tố `:` (ví dụ `:sm`), bộ gõ tự động gợi ý tối đa 5 emoji phù hợp trong bảng ứng viên.

Định dạng file text: `key=value` mỗi dòng, phân tách bằng dấu `=` đầu tiên, ký tự `#` ở đầu dòng là chú thích, bảng mã UTF-8. Ví dụ:
```ini
cty=Công ty TNHH
dc=được
```

---

## Chính tả và Gợi ý từ điển Ngoại tuyến

Ba lớp phối hợp chặt chẽ, không chặn phím gõ, chạy ngoại tuyến 100% trên máy:
1. **Kiểm tra chính tả ngoại tuyến**: Thuần quy tắc ngữ âm tiếng Việt (âm đầu, vần, phụ âm cuối, thanh điệu) kết hợp từ điển học. Chữ chưa hoàn thành nguyên âm/dấu luôn được cho qua để không ngắt quãng nhịp gõ.
2. **Sửa lỗi chính tả**: Từ gõ sai (ví dụ `kông`, `khoog`) được tự động gợi ý từ đúng (`không`) nhờ thuật toán tìm kiếm khoảng cách ký tự kết hợp từ điển ~7.000 từ.
3. **Hoàn thành tiền tố**: Đang gõ `thong` sẽ gợi ý các từ hoàn chỉnh `thông`, `thống`... Nhận từ bằng phím **Tab**, phím số **1..5** hoặc click chuột vào bảng ứng viên (Candidate List).

Chỉ những từ bạn **chủ động chọn** mới được ghi nhớ vào từ điển học cá nhân (`learned-words.txt`). Lần sau, từ này sẽ được ưu tiên đưa lên đầu danh sách gợi ý.

---

Xem thêm: [README](../README.md) · [Bản Windows](windows.md) · [Bản macOS](macos.md) · [Phát triển](dev.md) · [Đối chiếu tính năng](parity.md) · [Trang chủ](https://isthaison.github.io/gotv/)
