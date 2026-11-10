# Tài liệu các bài viết truyền thông cộng đồng (Community Posts)

Dưới đây là nội dung các bài viết đã được soạn thảo kỹ lưỡng, phù hợp với phong cách và đối tượng độc giả của từng cộng đồng. Bạn có thể sao chép và đăng tải trực tiếp.

---

## 📌 Bài 1: Đăng nhóm J2TEAM Community (Facebook)
> **Đối tượng:** Lập trình viên, người làm công nghệ, người dùng quan tâm đến bảo mật & hiệu năng.  
> **Hình ảnh đính kèm gợi ý:** Video/Ảnh chụp GoTV đang gõ mượt mà trên VS Code/Terminal, hoặc ảnh so sánh kiến trúc TSF vs Hook.

```markdown
[Góc chia sẻ / Open-source] Mình tự viết một bộ gõ tiếng Việt Native cho Windows, macOS & Linux bằng C thuần vì quá mệt mỏi với lỗi gõ code.

Chào anh em J2TEAM,

Chắc hẳn nhiều anh em làm lập trình hay dùng Terminal (VS Code, Cursor, Ghostty, Kitty...) đều từng gặp cảnh gõ phím tiếng Việt bị dính chữ, gạch chân đỏ làm phiền, hoặc nhảy con trỏ lung tung.

Đa phần các bộ gõ tiếng Việt phổ biến hiện nay trên Windows đều ra đời từ nhiều năm trước, sử dụng kỹ thuật Global Keyboard Hook (`SetWindowsHookEx`) — tức là "chặn phím thô ở tầng thấp rồi tự gửi phím ảo đè lại". Cơ chế này trên Windows 10/11 hiện đại rất dễ bị:
1. Xung đột với các phần mềm bảo mật/Antivirus (bị nghi ngờ keylogger).
2. Hoạt động chập chờn trên các app đời mới, Electron hay IDE lập trình.
3. Trên macOS thì bắt buộc phải cấp quyền Accessibility (trợ năng) — một quyền rất nhạy cảm về bảo mật.

Sau một thời gian trăn trở, mình quyết định phát triển dự án mã nguồn mở **GoTV** (giấy phép MIT) với mục tiêu: **Hiện đại - Chuẩn Native - Siêu nhẹ - Bảo mật tuyệt đối**.

🎯 Những điểm khác biệt chính của GoTV:
1. **Lõi C thuần duy nhất**: Toàn bộ thuật toán Telex/VNI được viết bằng C nguyên bản, không dùng Go runtime, không CGO, tối ưu tuyệt đối bộ nhớ và độ trễ.
2. **Chuẩn Native trên từng OS**:
   - Trên **Windows**: Tích hợp trực tiếp qua Text Services Framework (TSF) chính quy của Microsoft. Không hook bàn phím, gõ trực tiếp vào TextStore của ứng dụng.
   - Trên **macOS**: Tuân thủ 100% chuẩn Apple InputMethodKit (IMK), KHÔNG đòi quyền Accessibility.
   - Trên **Linux**: Tích hợp chuẩn IBus engine.
3. **Thân thiện tối đa với Coder**: Gõ xen kẽ tiếng Anh, tiếng Việt, biến code mà không bị gạch chân đỏ hay nuốt phím.
4. **Offline 100% & Riêng tư**: Có sẵn bộ gợi ý từ điển (~6.700 từ chuẩn) và viết tắt macro chạy hoàn toàn offline trên máy, không gửi bất kỳ gói tin nào ra ngoài.
5. **Đã hỗ trợ Winget**: Anh em Windows có thể cài đặt chính quy.

Dự án hiện đã hoàn thiện và phát hành bản ổn định v0.9.13. Dự án hoàn toàn phi lợi nhuận vì cộng đồng.

👉 Mã nguồn GitHub: https://github.com/isthaison/gotv
👉 Trang chủ tải về: https://isthaison.github.io/gotv/

Rất mong nhận được trải nghiệm thực tế, góp ý và gạch đá từ anh em để bộ gõ ngày càng hoàn thiện hơn! Nếu anh em thấy dự án hữu ích, cho mình xin 1 ⭐ Star trên GitHub để lan tỏa nhé. Cảm ơn mọi người!
```

---

## 📌 Bài 2: Đăng Diễn đàn VOZ (Khu Lập trình & CNTT)
> **Đối tượng:** Thành viên Voz (thích sự thẳng thắn, chi tiết kỹ thuật, link tải nhanh, mã nguồn minh bạch).  
> **Tiêu đề:** `[Phần mềm Việt] GoTV — Bộ gõ tiếng Việt mã nguồn mở chuẩn TSF cho Windows, macOS & Linux (Lõi C thuần, không hook)`

```markdown
Chào các thím,

Dùng Windows 11 với Mac thời gian gần đây chắc nhiều thím cũng ức chế với vụ gõ tiếng Việt trong Terminal, VS Code hay mấy app hiện đại dính lỗi gạch chân, nhảy chữ hoặc lag phím.

Mình vừa hoàn thiện dự án **GoTV** — bộ gõ tiếng Việt mã nguồn mở viết lại từ đầu bằng C thuần, giải quyết triệt để các vấn đề kiến trúc cũ:

**1. Khác gì so với UniKey / EVKey?**
- **Windows**: Không dùng cơ chế hook bàn phím cổ điển (`SetWindowsHookEx`) gửi phím ảo, mà làm đúng chuẩn **TSF Text Service** của Microsoft (giống bộ gõ mặc định của Windows nhưng thuật toán Telex/VNI chuẩn của người Việt).
- **macOS**: Dùng chuẩn Apple InputMethodKit, không cần cấp quyền Accessibility (trợ năng) như các bộ gõ bên thứ ba khác.
- **Linux**: Có sẵn IBus engine cho Ubuntu/Debian/Arch.
- **Lõi C thuần**: Không có runtime Go hay thư viện nặng, ăn cực ít RAM, độ trễ tiệm cận 0.

**2. Tính năng nổi bật:**
- Đầy đủ Telex, VNI, đặt dấu mới (hòa, thúy) / cũ (hoà, thuý).
- Phím tắt chuyển E/V (Ctrl+Shift hoặc Alt+Z) đồng bộ trực tiếp với Win+Space.
- Gợi ý từ điển offline ~6.700 từ chuẩn (không ép sửa chữ bậy bạ).
- Hỗ trợ gõ tắt (Macro) và Emoji bằng file text nhẹ nhàng.
- Hoàn toàn offline, không kết nối mạng gửi dữ liệu phím.

**3. Tải về & Cài đặt:**
- GitHub Release: https://github.com/isthaison/gotv/releases/latest
- Trang chủ: https://isthaison.github.io/gotv/
- Windows có thể cài qua Winget (sắp lên kho chính thức).

Code mở 100% theo giấy phép MIT tại: https://github.com/isthaison/gotv

Mời các thím tải về trải nghiệm thử và cho mình xin feedback, gạch đá thoải mái để hoàn thiện tiếp. Thím nào ưng thì ném cho repo 1 ⭐ star ủng hộ tinh thần mã nguồn mở của dev Việt nhé!
```

---

## 📌 Bài 3: Đăng Viblo.asia / Dev.to (Bài viết Kỹ thuật Chuyên sâu)
> **Tiêu đề:** `Xây dựng bộ gõ tiếng Việt chuẩn TSF trên Windows 11 và InputMethodKit trên macOS: Tại sao nên từ bỏ SetWindowsHookEx?`

```markdown
### Mở đầu
Bộ gõ tiếng Việt đã đồng hành cùng người dùng máy tính Việt Nam hơn 30 năm qua, từ thời VietKey, UniKey đến EVKey, OpenKey. Tuy nhiên, phần lớn các bộ gõ bên thứ ba trên Windows hiện nay vẫn dựa trên cơ chế hook bàn phím toàn cục (`SetWindowsHookEx`) được thiết kế từ thời Windows 95/XP.

Trong bài viết này, mình sẽ phân tích các hạn chế cố hữu của kiến trúc hook cũ trên Windows 10/11 hiện đại, và kiến trúc Text Services Framework (TSF) mà dự án mã nguồn mở GoTV áp dụng để mang lại trải nghiệm gõ mượt mà, không xung đột.

### 1. Vấn đề của SetWindowsHookEx trong kỷ nguyên ứng dụng hiện đại
*(Nội dung phân tích kỹ thuật: cơ chế can thiệp tầng thấp, độ trễ khi qua IPC, vấn đề UIPI integrity level khi gõ vào app quyền Admin, hiện tượng nuốt phím và gạch đỏ trong trình soạn thảo modern...)*

### 2. Kiến trúc TSF (Text Services Framework) của GoTV
- TSF hoạt động như một In-process COM Server nạp trực tiếp vào ngữ cảnh ứng dụng đích.
- Tương tác hai chiều với `ITfContext` và `ITfInsertAtSelection`: Bộ gõ không gửi phím giả lập (`keybd_event`) mà cập nhật trực tiếp chuỗi ký tự đang soạn thảo (composition).
- Nhận diện ứng dụng đang hoạt động không qua active polling.

### 3. macOS: InputMethodKit vs Accessibility API
- Tại sao việc đòi quyền Accessibility lại là rủi ro bảo mật lớn?
- Cách triển khai `IMKInputController` để xử lý buffer âm tiết chuẩn xác mà không cần hook hệ thống.

### 4. Tổng kết & Trải nghiệm
Mã nguồn dự án GoTV được công khai toàn bộ tại GitHub: https://github.com/isthaison/gotv
Rất hoan nghênh anh em kỹ sư cùng tham gia đóng góp và tối ưu thuật toán!
```
