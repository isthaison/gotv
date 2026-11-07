# Bản Windows

Native Windows 10/11 (x64). Gõ bằng **TSF Text Service** (stub `gtv_tsf.dll` đăng ký một lần + engine versioned trong `ver\<bản>\`, composition chuẩn Windows — đúng trong mọi ô nhập kể cả autocomplete/khó tính); app khay hệ thống lo cấu hình, gợi ý AI, cập nhật. Không còn hook bàn phím.

- Cài lần đầu: đăng xuất/đăng nhập lại rồi chọn GoTV bằng `Win+Space`. Các bản **cập nhật sau không cần** đóng app, đăng xuất hay reboot (xem dưới).
- Nếu chưa thấy bàn phím GoTV: vào **Settings → Time & Language → Language** → chọn `English` (hoặc `Tiếng Việt`) → **Options** → **Add a keyboard** → chọn **GoTV**. Không thấy nữa thì đăng xuất/đăng nhập lại một lần.
- Icon khay hiện kiểu gõ như bản Linux: đỏ `T` = Telex, đỏ `V` = VNI (**click-trái** trực tiếp vào icon khay để chuyển đổi nhanh giữa Telex và VNI; chuột-phải mở menu; click đúp mở Cài đặt). Tooltip khay cũng hiện `GoTV — Telex/VNI` nên chụp màn hình vẫn thấy mode gõ hiện tại. Không có chữ `E`/nút tắt: chuyển Anh/Việt bằng `Win+Space` (Windows quản lý indicator ngôn ngữ).
- Telex/VNI, chuẩn dấu, chính tả đổi trong Bảng điều khiển/menu tray/click khay, TSF nhận ngay khi chuyển ô nhập (đọc lại cấu hình mỗi lần kích hoạt).

## Cài đặt và cập nhật

- Mỗi release GitHub đính kèm `gotv-<version>-x64-setup.exe` (Inno Setup, per-user, không cần admin, gỡ sạch qua Add/Remove Programs, giữ nguyên cấu hình).
- Cập nhật không chạm app đang chạy, không cần đăng xuất/reboot: mỗi bản nằm trong thư mục version riêng (`ver\<bản>\`), stub TSF (`gtv_tsf.dll`) đăng ký đúng một lần nên không cài lại, không UAC. App đang mở giữ bản cũ tới khi thoát; app mở mới dùng bản mới ngay; app khay tự tiếp quản bản mới trong ~1 giây. Chỉ giữ bản hiện tại + 1 bản rollback, cũ hơn tự dọn.
- App tự kiểm tra release mới nhất mỗi ngày; menu tray **Kiểm tra cập nhật...** để kiểm tra tay. Có bản mới thì hiện balloon — **click vào balloon** để tải và chạy bộ cài silent. So sánh semver, chỉ cài đúng asset `gotv-<version>-x64-setup.exe`.
- Code signing: bản release ký self-signed cert (RSA 4096, 1 năm) — bypass SmartScreen trên大部分 Windows. Nếu cert chưa cấu hình, bản unsigned vẫn hoạt động bình thường nhưng SmartScreen có thể hiện cảnh báo lần đầu chạy.
- Chẩn đoán: `%LOCALAPPDATA%\gotv\update.log` (mọi bước check/tải/cài của updater).

## Microsoft Store

GoTV hiện dùng TSF text service được nạp bên trong các ứng dụng khác. Vì vậy không
thể chỉ bọc bản hiện tại thành MSIX: MSIX desktop không hỗ trợ công bố in-process
extensions cho tiến trình ngoài package ([hướng dẫn đóng gói desktop của
Microsoft](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-prepare)).
Hướng phù hợp là gửi Microsoft Store liên kết tải trực tiếp tới bộ cài `.exe`
độc lập theo [chính sách 10.2.9](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies#1029).

Tạo bộ cài riêng bằng `make -f Makefile.win store-setup`. Tên file có hậu tố
`-store-setup.exe`; bộ cài GitHub thông thường không bị thay đổi. Khi khai báo
trong Partner Center, cấu hình tham số cài đặt
`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-` để cài đặt không hiện wizard.
Trang chính sách quyền riêng tư công khai dùng cho listing: [GoTV Privacy Policy](https://isthaison.github.io/gotv/privacy.html).
Lần đầu cài có thể hiện UAC để đăng ký TSF; prompt này được Store cho phép.
Đăng ký TSF vẫn chạy trong chế độ silent để bàn phím GoTV hoạt động sau khi cài.
Ở bản Store, tự cập nhật ứng dụng từ GitHub và lựa chọn khởi động elevated bị
ẩn; người dùng nhận bản ứng dụng mới qua listing Store. Khởi động thường vẫn có
thể bật chủ động trong cài đặt, nhưng mặc định bị tắt khi cài silent.
GoTV không tự thay đổi danh sách ngôn ngữ/bàn phím của Windows ở lần chạy đầu;
người dùng thêm GoTV qua **Bàn phím (Win+Space)...** trong menu khay để giữ quyền
chủ động với thiết lập Windows.

Đây mới là đầu ra cài đặt, chưa đủ điều kiện gửi Store: Store yêu cầu URL tải
theo phiên bản, installer độc lập, và cả installer lẫn mọi file PE bên trong
phải được ký bằng chứng thư code-signing có chuỗi tin cậy tới CA thuộc Microsoft
Trusted Root Program. Chữ ký self-signed hiện tại không đáp ứng điều kiện này.
Mỗi phiên bản cần URL/file mới trong Partner Center; cần tài khoản Partner
Center và chứng thư phù hợp trước khi phát hành.

CI luôn build riêng payload Store (`release-pkg-store`) và installer. Để CI tạo
bản có thể nộp, cấu hình repository variable `STORE_SIGNING_ENABLED=true` cùng
Actions secrets `STORE_SIGNING_CERT_PFX_B64` (PFX base64) và
`STORE_SIGNING_CERT_PASSWORD` (mật khẩu PFX), dùng chứng thư code-signing có
chuỗi tin cậy tới CA được Microsoft chấp nhận. CI ký và xác minh từng `.exe`/
`.dll` trong payload, rồi dùng Inno Setup ký cả installer và uninstaller;
artifact và release asset Store chỉ
được xuất khi bật signing. Nếu chưa cấu hình, CI chỉ build bản unsigned để kiểm
tra đóng gói, không xuất bản như installer Store. Có thể chạy
`make -f Makefile.win store-staging` để chỉ tạo payload Store riêng.

## Dùng hằng ngày

- **Menu khay**: Bảng điều khiển, Bàn phím (Win+Space)..., Telex/VNI, chính tả, chuẩn dấu,
  tự khởi động (thường/admin), Kiểm tra cập nhật, Thoát.
- **Bảng điều khiển**: kiểu gõ, chuẩn dấu, chính tả, tự khởi động, cụm từ điển
  (bật/tắt gợi ý, phiên bản + số từ, nút **Cập nhật từ điển**),
  nút **Bàn phím...** (xem [Quản lý danh sách bàn phím](#quản-lý-danh-sách-bàn-phím-winspace)),
  nút **Dữ liệu...** — lưu ở `%LOCALAPPDATA%\gotv\` như bản Linux.
- **Gợi ý từ điển dạng balloon**: TSF báo mỗi từ vừa commit về app khay; từ sai được tra từ điển nền (`"sai" có thể bạn muốn gõ "đúng"?`, chống spam 10 giây), offline 100%. **Click balloon** để nhận (chưa gõ tiếp thì thay tại chỗ, rồi thì copy vào clipboard); lựa chọn được học vào `learned-corrections.txt`.
- **Bảng gợi ý keyword inline (broker ngoài process)**: TSF trong app nhập liệu chỉ giữ composition/candidate và bắt phím, không tạo cửa sổ (an toàn với Electron/OpenCode); process tray riêng vẽ overlay cạnh caret, không lấy focus. Gõ `:sm` gợi ý emoji ngay; từ thường gợi ý từ từ điển sau debounce ngắn. Phím: `Up/Down` di chuyển, `Tab` hoặc số `1-5` (Telex) để nhận, `Esc` bỏ (vẫn gõ tiếp), `Enter` chỉ commit nguyên chữ đang gõ (không nhận gợi ý), `Space` giữ nguyên chữ. Không lấy được tọa độ caret thật thì không hiện gì (không fallback góc màn hình). Từ trùng macro/emoji khớp tuyệt đối vẫn ưu tiên bung bằng `Tab` như cũ. Lựa chọn được học như balloon.
  Chẩn đoán vị trí hiển thị: `%LOCALAPPDATA%\gotv\suggest.log` — ghi mỗi khi rect caret nhận về không nằm trong cửa sổ đang focus (tức lỗi ở phía TSF), kèm DPI và toạ độ popup; nhật ký tự cắt ở 64 KB.
- Macro/emoji/config: chung lõi và file `data/` với bản Linux.
- Chữ Việt hiển thị đúng mọi locale: toàn bộ UI dùng Unicode API, dialog đặt chữ lúc chạy.

## Khởi động với quyền admin

App thường (Run key) không chạm được vào cửa sổ admin (Task Manager, regedit, cmd admin):
chọn menu **Khởi động với quyền admin**, đồng ý UAC một lần — app tạo tác vụ logon
`GoTV` chạy elevated, tắt mục Run thường để không mở 2 bản. Tắt/chuyển chế độ
cũng cần UAC một lần. Hai mục khởi động loại trừ nhau: chỉ một được check.

## Quản lý dữ liệu (macro/emoji)

Bảng điều khiển → **Dữ liệu...**: chuyển Macro/Emoji, chọn dòng để sửa, **Lưu** để thêm/cập nhật,
**Xóa** để gỡ, **Mở thư mục** để sửa file text tay. Khóa không chứa dấu cách hay `=`,
giá trị không rỗng. Lưu ghi đè file riêng của bạn (`macros.txt`/`emojis.txt` trong
`%LOCALAPPDATA%\gotv\`, thay thế hoàn toàn file hệ thống) — **app đang mở phải khởi
động lại mới nhận bảng mới** (TSF đọc bảng lúc app khởi động).

Macro và emoji đều kích hoạt bằng phím **Tab** (gõ từ tắt/trigger rồi nhấn Tab để bung; bấm Space hoặc dấu câu giữ nguyên, không tự động bung ngoài ý muốn).

## Quản lý danh sách bàn phím (Win+Space)

Menu khay → **Bàn phím (Win+Space)...**, hoặc Bảng điều khiển → **Bàn phím...**.
Đây là danh sách bàn phím mà `Win+Space` chuyển qua, theo đúng thứ tự Windows hiển thị:

- **Thứ tự trong danh sách chính là thứ tự `Win+Space`** — dùng **Lên**/**Xuống** để đổi.
- **Thêm...** mở danh sách bàn phím mà Windows đang có cho ngôn ngữ đã chọn: bàn phím cổ điển
  (US, Dvorak, Colemak, French AZERTY/BÉPO…) và bộ gõ TSF (GoTV, Vietnamese Telex, VNI…).
  Bàn phím đã có trong danh sách bị ẩn để khỏi thêm trùng.
- **Xóa** để gỡ. Giữ ít nhất một bàn phím — Windows không cho `Win+Space` trống rỗng.
- **Mặc định** trả về cấu hình GoTV: **Anh (US) + Tiếng Việt (GoTV)**.
- Sửa xong bấm **Đóng** và chọn **Có** để lưu.

Vài điểm Windows thật sự quyết định, đã tính sẵn trong code:

- Danh sách được đọc/ghi qua `Get/Set-WinUserLanguageList` (powershell ẩn), vì ghi thẳng
  registry bị lớp input của Windows bỏ qua.
- Lưu xong app **đọc lại** và so với danh sách đã chọn: nếu Windows từ chối một bàn phím
  (TIP chưa đăng ký, ví dụ `gtv_tsf.dll` lỗi) thì báo tên bàn phím đó thay vì âm thầm mất —
  và nói rõ phải chạy `gotv.exe --register-tsf` (admin).
- Windows tự gom bàn phím theo ngôn ngữ khi lưu, nên sau khi lưu danh sách được đọc lại
  và hiển thị lại cho khớp với thứ tự thật của `Win+Space`.
- Từ lúc bạn tự lưu, phần tự động lần đầu (`input_setup.c`) **không còn đụng vào** danh sách
  nữa — nó sẽ không ghi đè lựa chọn của bạn ở các lần khởi động sau.
- Log thao tác: `%LOCALAPPDATA%\gotv\keyboard.log`.

## Gạch đỏ dưới chữ Việt trong trình duyệt/app khác

Không phải của GoTV: TSF của app không bao giờ vẽ gạch đỏ (không set display
attribute nào), bản Windows cũng không có gạch đỏ inline (chỉ có balloon gợi ý AI).
Gạch đỏ cả câu như ảnh là **spellchecker của trình duyệt** (từ điển Anh gặp chữ Việt)
hoặc Windows (Settings → Time & language → Typing → Highlight misspelt words khi đang
ở ENG). Tắt spellcheck của ô nhập đó, hoặc thêm tiếng Việt vào trình duyệt là hết.

## Giới hạn đã biết

- Gợi ý từ điển inline chạy qua overlay của process tray (không tạo cửa sổ trong app nhập liệu nên an toàn với Electron/OpenCode); macro/emoji vẫn bung bằng `Tab`.
- TSF chưa hỗ trợ trường mật khẩu đặc biệt.

## Build và release

```sh
# MSYS2 MinGW-w64 (cửa lệnh chung, như Linux/macOS):
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-glib2 make
./gtv.sh build                 # gotv.exe + gtv_tsf.dll + test
./gtv.sh package               # gotv-X.Y.Z-x64-setup.exe (cần Inno Setup)
./gtv.sh install               # cài silent + chạy app (đóng app giữ DLL trước)
./gtv.sh uninstall             # gỡ + dọn Run/task/profile
# Từ cmd.exe: gtv build | test | package | install | uninstall
# Lệnh make gốc vẫn dùng được: mingw32-make -f Makefile.win [test|staging]
```

Cắt release: `./gtv.sh bump X.Y.Z` (ghi `VERSION` + mọi file mang version),
commit, push tag `vX.Y.Z` — CI build `gotv-X.Y.Z-x64-setup.exe` và đính kèm release để app tự cập nhật.

---
Xem thêm: [README](../README.md) · [Cấu hình](config.md) · [Bản macOS](macos.md) ·
[Phát triển](dev.md) · [Trang chủ](https://isthaison.github.io/gotv/)
