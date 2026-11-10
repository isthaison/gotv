export interface FaqItem {
  id: string;
  slug: string;
  question: string;
  shortAnswer: string;
  detailHtml: string;
  category: 'general' | 'usage' | 'troubleshooting';
  dateModified: string;
}

export const FAQ_ITEMS: FaqItem[] = [
  {
    id: 'tran-ky-nam',
    slug: 'gotv-co-phai-gotiengviet-tran-ky-nam',
    question: 'GoTV có phải GoTiengViet của Trần Kỳ Nam không?',
    shortAnswer: 'Không. GoTV là dự án mã nguồn mở độc lập của isthaison, trước đây mang tên GoTiengViet. Từ phiên bản 0.9.0, dự án đổi tên để tránh nhầm lẫn với phần mềm của Trần Kỳ Nam.',
    category: 'general',
    dateModified: '2026-11-07',
    detailHtml: `
      <p class="mb-4"><strong>Câu trả lời ngắn gọn: KHÔNG.</strong></p>
      <p class="mb-4"><strong>GoTV</strong> là một dự án phần mềm bộ gõ tiếng Việt <strong>mã nguồn mở (Open Source) hoàn toàn độc lập</strong>, được viết và duy trì bởi lập trình viên <strong>isthaison</strong> theo giấy phép MIT.</p>
      
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Lịch sử về tên gọi của dự án</h3>
      <p class="mb-4">Trong các giai đoạn phát triển ban đầu (trước phiên bản <code>v0.9.0</code>), dự án từng sử dụng tên mã tạm thời là <em>GoTiengViet</em>. Tuy nhiên:</p>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li>Thương hiệu <strong>GoTiengViet</strong> của tác giả <strong>Trần Kỳ Nam</strong> đã có lịch sử hơn 15 năm và gắn bó với đông đảo người dùng máy tính tại Việt Nam.</li>
        <li>Để thể hiện sự tôn trọng tuyệt đối đối với bản quyền thương hiệu của tác giả đi trước, đồng thời tránh gây nhầm lẫn cho cộng đồng khi tìm kiếm và cài đặt phần mềm, <strong>từ phiên bản 0.9.0 trở đi, dự án đã chính thức đổi tên thành GoTV</strong>.</li>
      </ul>

      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Những điểm khác biệt nổi bật của GoTV</h3>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Mã nguồn mở 100%:</strong> Toàn bộ mã nguồn được công khai minh bạch tại <a class="text-red-700 underline font-semibold" href="https://github.com/isthaison/gotv" target="_blank" rel="noopener">github.com/isthaison/gotv</a>, cho phép cộng đồng kiểm tra độ an toàn tuyệt đối.</li>
        <li><strong>Một lõi C duy nhất cho mọi hệ điều hành:</strong> Cùng một thuật toán biến đổi Telex/VNI chạy mượt mà trên Linux (IBus), Windows 10/11 (Text Services Framework TSF) và macOS.</li>
        <li><strong>Chính tả và gợi ý ngoại tuyến:</strong> Tích hợp kiểm tra chính tả thời gian thực và gợi ý từ điển tiếng Việt offline mà không cần bất kỳ kết nối mạng hay máy chủ trung gian nào.</li>
      </ul>
    `
  },
  {
    id: 'vtvgo',
    slug: 'gotv-co-lien-quan-vtvgo-khong',
    question: 'GoTV có liên quan gì đến ứng dụng VTVGo không?',
    shortAnswer: 'Không. GoTV là bộ gõ tiếng Việt Telex/VNI trên máy tính, hoàn toàn không liên quan đến ứng dụng truyền hình VTVGo của Đài Truyền hình Việt Nam.',
    category: 'general',
    dateModified: '2026-11-07',
    detailHtml: `
      <p class="mb-4"><strong>Câu trả lời ngắn gọn: HOÀN TOÀN KHÔNG.</strong></p>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Hai sản phẩm hoàn toàn khác biệt</h3>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>GoTV:</strong> Là phần mềm <strong>bộ gõ tiếng Việt (Vietnamese Input Method Editor)</strong> mã nguồn mở trên máy tính (hỗ trợ kiểu gõ Telex và VNI trên Windows, Linux Ubuntu/Debian và macOS). Tên gọi là viết tắt ghép từ <em>Gõ Tiếng Việt</em>.</li>
        <li><strong>VTVGo:</strong> Là ứng dụng xem <strong>truyền hình trực tuyến</strong> chính thức của Đài Truyền hình Việt Nam (VTV), dùng để xem các kênh thời sự, phim truyện và thể thao trên điện thoại, TV thông minh hoặc trình duyệt web.</li>
      </ul>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Tại sao công cụ tìm kiếm hay nhầm lẫn?</h3>
      <p class="mb-4">Do hai tên gọi có các ký tự rất tương tự nhau (<em>G-o-T-V</em> và <em>V-T-V-G-o</em>), nên thuật toán tìm kiếm tự động của Google đôi khi suy đoán người dùng gõ sai và hiển thị: <em>"Có phải bạn muốn tìm: VTVGo"</em>.</p>
      <p>Để tìm kiếm chính xác bộ gõ tiếng Việt GoTV trên Google, bạn có thể nhập các cụm từ đầy đủ như: <strong>"GoTV bộ gõ tiếng việt"</strong> hoặc <strong>"GoTV isthaison"</strong>.</p>
    `
  },
  {
    id: 'so-sanh-unikey',
    slug: 'so-sanh-gotv-voi-unikey-evkey',
    question: 'GoTV khác gì so với UniKey và EVKey?',
    shortAnswer: 'GoTV dùng một lõi C chung cho cả 3 OS, có chính tả khi gõ (gạch đỏ trên Linux, balloon gợi ý trên Windows/macOS) và gợi ý từ điển offline — những thứ Unikey/EVKey không có.',
    category: 'general',
    dateModified: '2026-11-07',
    detailHtml: `
      <p class="mb-4"><strong>UniKey</strong> và <strong>EVKey</strong> là những bộ gõ xuất sắc và quen thuộc của người dùng Việt Nam qua nhiều năm. Tuy nhiên, <strong>GoTV</strong> được phát triển với những mục tiêu kiến trúc hiện đại và phục vụ người dùng đa nền tảng:</p>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Bảng so sánh chi tiết</h3>
      <div class="overflow-x-auto my-4">
        <table class="w-full text-left border border-stone-200 text-sm">
          <thead class="bg-stone-100 border-b">
            <tr>
              <th class="p-3 border-r">Tiêu chí</th>
              <th class="p-3 border-r font-bold text-red-700">GoTV</th>
              <th class="p-3 border-r">UniKey</th>
              <th class="p-3">EVKey</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-stone-200">
            <tr><td class="p-3 font-semibold border-r">Nền tảng</td><td class="p-3 border-r">Windows, Linux, macOS (Lõi C chung)</td><td class="p-3 border-r">Chủ yếu Windows</td><td class="p-3">Windows, macOS</td></tr>
            <tr><td class="p-3 font-semibold border-r">Mã nguồn mở</td><td class="p-3 border-r text-green-700 font-bold">Có (MIT License)</td><td class="p-3 border-r">Không mở bản mới</td><td class="p-3 text-stone-500">Đóng mã nguồn</td></tr>
            <tr><td class="p-3 font-semibold border-r">Chính tả khi gõ</td><td class="p-3 border-r text-green-700 font-bold">Có (Real-time offline)</td><td class="p-3 border-r">Không có</td><td class="p-3">Không có</td></tr>
            <tr><td class="p-3 font-semibold border-r">Gợi ý từ điển</td><td class="p-3 border-r text-green-700 font-bold">Có (~7000 từ + tự học)</td><td class="p-3 border-r">Không có</td><td class="p-3">Không có</td></tr>
            <tr><td class="p-3 font-semibold border-r">Cài đặt per-user</td><td class="p-3 border-r">Không cần quyền Admin</td><td class="p-3 border-r">Portable / Admin</td><td class="p-3">Portable</td></tr>
          </tbody>
        </table>
      </div>
    `
  },
  {
    id: 'mang-internet',
    slug: 'gotv-co-can-ket-noi-mang-khong',
    question: 'Bộ gõ GoTV có cần kết nối mạng Internet không?',
    shortAnswer: 'Không. Gõ tiếng Việt, kiểm tra chính tả, macro/emoji và gợi ý từ điển đều ngoại tuyến 100%. Mạng chỉ dùng khi kiểm tra bản cập nhật từ GitHub.',
    category: 'usage',
    dateModified: '2026-11-07',
    detailHtml: `
      <p class="mb-4"><strong>Câu trả lời ngắn gọn: KHÔNG. GoTV hoạt động ngoại tuyến (Offline) 100%.</strong></p>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Các tính năng chạy hoàn toàn offline:</h3>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Gõ tiếng Việt Telex và VNI:</strong> Bộ quy tắc chuyển đổi dấu và ghép âm tiết được biên dịch trực tiếp từ ngôn ngữ C, xử lý cực nhanh trong bộ nhớ RAM mà không cần gọi API mạng nào.</li>
        <li><strong>Kiểm tra chính tả:</strong> Quy tắc âm tiết và bảng từ điển nằm sẵn trong ổ đĩa cứng của bạn.</li>
        <li><strong>Gợi ý từ điển & Tự học từ:</strong> GoTV tự động lưu các từ bạn hay gõ vào file text cục bộ (<code>learned-words.txt</code>). Dữ liệu này chỉ nằm trên máy bạn và không bao giờ được gửi lên đám mây.</li>
        <li><strong>Gõ tắt Macro & Emoji:</strong> Đọc trực tiếp từ file cấu hình trên máy tính.</li>
      </ul>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Bảo đảm quyền riêng tư tuyệt đối</h3>
      <p>Vì GoTV là <strong>phần mềm nguồn mở (Open Source)</strong>, bất kỳ lập trình viên nào cũng có thể kiểm tra toàn bộ mã nguồn của chúng tôi trên GitHub để xác thực rằng phần mềm không chứa bất kỳ dòng mã theo dõi (keylogger) hay gửi trộm dữ liệu nào.</p>
    `
  },
  {
    id: 'loi-1-ra-a',
    slug: 'loi-go-1-ra-a-tren-windows',
    question: 'Tại sao gõ phím số 1 lại ra chữ ă trên Windows và cách sửa?',
    shortAnswer: 'Đó là layout VIE của Windows đặt chữ Việt ở hàng số. GoTV đã ép dịch phím qua layout US nên gõ 1 ra 1 đúng. Kiểm tra đang dùng bàn phím GoTV và xóa bàn phím Vietnamese của Windows đi.',
    category: 'troubleshooting',
    dateModified: '2026-11-07',
    detailHtml: `
      <h3 class="text-xl font-bold mt-2 mb-3 text-stone-900 border-b border-red-700 pb-1">1. Nguyên nhân gây ra lỗi</h3>
      <p class="mb-4">Khi bạn gõ số <code>1</code> mà hiển thị thành <code>ă</code>, gõ <code>2</code> ra <code>â</code>, <code>3</code> ra <code>ê</code>... thủ phạm <strong>không phải do bộ gõ tiếng Việt bị lỗi</strong>, mà do Windows đang kích hoạt bàn phím hệ thống có tên là <strong>Vietnamese keyboard layout (VIE)</strong> của Microsoft.</p>
      
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">2. Cách khắc phục nhanh trong 10 giây</h3>
      <p class="mb-2"><strong>Cách 1: Chuyển đổi bàn phím bằng phím tắt</strong></p>
      <ol class="list-decimal pl-6 mb-4 space-y-1">
        <li>Nhấn tổ hợp phím <code>Windows + Phím cách (Space)</code>.</li>
        <li>Chọn <strong>GoTV (English - United States)</strong> hoặc <strong>ENG (US Keyboard)</strong>. Tránh chọn mục có chữ <em>VIE (Vietnamese)</em>.</li>
      </ol>
      <p class="mb-2"><strong>Cách 2: Gỡ bỏ vĩnh viễn bàn phím VIE của Windows</strong></p>
      <ol class="list-decimal pl-6 mb-4 space-y-1">
        <li>Mở <strong>Settings</strong> trên Windows (bấm phím tắt <code>Win + I</code>).</li>
        <li>Vào mục <strong>Time & Language</strong> &rarr; <strong>Language & Region</strong>.</li>
        <li>Tìm mục <strong>Tiếng Việt</strong> &rarr; chọn <strong>Language options</strong>.</li>
        <li>Tại mục <strong>Keyboards</strong>, nếu thấy có <em>Vietnamese (Telex)</em>, hãy xóa bỏ và chỉ giữ lại bàn phím chuẩn <strong>US</strong> và <strong>GoTV</strong>.</li>
      </ol>
    `
  },
  {
    id: 'macos-developer',
    slug: 'macos-bao-unidentified-developer',
    question: 'Cách xử lý thông báo "Unidentified Developer" khi cài GoTV trên macOS?',
    shortAnswer: 'Bản preview chưa ký số Apple. Chuột phải vào app trong Finder → chọn Open để chấp nhận một lần là có thể sử dụng bình thường.',
    category: 'troubleshooting',
    dateModified: '2026-11-07',
    detailHtml: `
      <h3 class="text-xl font-bold mt-2 mb-3 text-stone-900 border-b border-red-700 pb-1">1. Vì sao máy Mac hiện thông báo này?</h3>
      <p class="mb-4">Tính năng bảo mật <strong>Gatekeeper</strong> của Apple mặc định chỉ cho phép chạy ứng dụng đã được ký số trả phí. Vì GoTV là dự án phần mềm mã nguồn mở cộng đồng miễn phí, các bản preview chưa đăng ký chứng chỉ thương mại nên macOS sẽ hiện cảnh báo bảo vệ.</p>

      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">2. Hướng dẫn mở ứng dụng</h3>
      <ol class="list-decimal pl-6 mb-4 space-y-2">
        <li>Mở <strong>Finder</strong> và tìm đến ứng dụng GoTV.</li>
        <li><strong>Nhấp chuột phải</strong> (hoặc giữ phím <code>Control</code> và nhấp chuột) vào biểu tượng GoTV.</li>
        <li>Chọn <strong>Open</strong> trên menu ngữ cảnh.</li>
        <li>Bấm <strong>Open</strong> một lần nữa trong hộp thoại xác nhận. Bạn chỉ cần làm thao tác này đúng 1 lần đầu tiên.</li>
      </ol>
    `
  },
  {
    id: 'gach-do-trinh-duyet',
    slug: 'tat-gach-do-trinh-duyet-khi-go-tieng-viet',
    question: 'Tại sao cả câu bị gạch chân đỏ trong trình duyệt và cách tắt?',
    shortAnswer: 'Không phải GoTV (bộ gõ không vẽ gạch đỏ). Đó là tính năng spellcheck tiếng Anh của trình duyệt Chrome/Edge gặp chữ Việt. Tắt spellcheck hoặc thêm ngôn ngữ Tiếng Việt là hết.',
    category: 'troubleshooting',
    dateModified: '2026-11-07',
    detailHtml: `
      <h3 class="text-xl font-bold mt-2 mb-3 text-stone-900 border-b border-red-700 pb-1">1. Hiểu đúng nguyên nhân</h3>
      <p class="mb-4">Khi gõ trên web (Facebook, Gmail, Docs), dấu gạch đỏ là do trình duyệt (Chrome, Edge, Cốc Cốc) bật sẵn từ điển tiếng Anh nên coi chữ tiếng Việt là viết sai.</p>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">2. Cách tắt nhanh</h3>
      <ol class="list-decimal pl-6 mb-4 space-y-2">
        <li>Nhấp chuột phải trực tiếp vào ô văn bản đang bị gạch đỏ.</li>
        <li>Tìm đến dòng <strong>Kiểm tra chính tả (Spell check)</strong>.</li>
        <li>Bỏ chọn ô <strong>Kiểm tra chính tả các trường văn bản</strong>.</li>
      </ol>
    `
  },
  {
    id: 'mat-gotv-windows-update',
    slug: 'mat-gotv-sau-khi-cap-nhat-windows',
    question: 'Bị mất biểu tượng GoTV sau khi cập nhật Windows Update?',
    shortAnswer: 'Đăng xuất/đăng nhập lại Windows; nếu chưa thấy: Settings → Time & Language → Language → Options → Add a keyboard → GoTV.',
    category: 'troubleshooting',
    dateModified: '2026-11-07',
    detailHtml: `
      <p class="mb-4">Các bản cập nhật lớn của Windows Update có thể thiết lập lại Text Services Framework (TSF).</p>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">Các bước xử lý</h3>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Bước 1:</strong> Đăng xuất (Sign out) tài khoản Windows rồi đăng nhập lại.</li>
        <li><strong>Bước 2:</strong> Vào Settings &rarr; Time & Language &rarr; Language &rarr; Language options &rarr; Add a keyboard &rarr; Chọn <strong>GoTV</strong>.</li>
        <li><strong>Bước 3:</strong> Hoặc chạy lại file cài đặt <code>gotv-*-x64-setup.exe</code> để tự động đăng ký lại các tệp DLL TSF.</li>
      </ul>
    `
  },
  {
    id: 'vi-tri-config',
    slug: 'vi-tri-luu-file-cau-hinh-gotv',
    question: 'Vị trí lưu file cấu hình, macro và từ điển của GoTV ở đâu?',
    shortAnswer: 'Windows: %APPDATA%\\gotv\\ — Linux: ~/.config/gotv/ — macOS: ~/Library/Application Support/gotv/. Toàn bộ là file text UTF-8, sửa tay được.',
    category: 'usage',
    dateModified: '2026-11-07',
    detailHtml: `
      <h3 class="text-xl font-bold mt-2 mb-3 text-stone-900 border-b border-red-700 pb-1">1. Đường dẫn thư mục</h3>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Windows:</strong> <code>%APPDATA%\\gotv\\</code> (Bấm <code>Win + R</code>, dán <code>%appdata%\\gotv</code> và Enter).</li>
        <li><strong>Linux:</strong> <code>~/.config/gotv/</code></li>
        <li><strong>macOS:</strong> <code>~/Library/Application Support/gotv/</code> hoặc <code>~/.config/gotv/</code></li>
      </ul>
      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">2. Các tệp quan trọng</h3>
      <p class="mb-2"><code>config</code>: Tùy chọn kiểu gõ (telex/vni), chính tả.</p>
      <p class="mb-2"><code>macro.txt</code>: Danh sách từ gõ tắt cá nhân (ví dụ: <code>cty=Công ty TNHH</code>).</p>
      <p><code>learned-words.txt</code>: Dữ liệu từ vựng bộ gõ đã tự học.</p>
    `
  },
  {
    id: 'terminal-macos',
    slug: 'loi-go-tieng-viet-terminal-macos',
    question: 'Cách khắc phục lỗi gõ tiếng Việt trong Terminal trên macOS (Ghostty, iTerm2, Kitty)?',
    shortAnswer: 'GoTV khắc phục triệt để bằng chuẩn Apple IMK: khử sạch gạch chân phiền toái (NSUnderlineStyleNone), không bao giờ bị mất ký tự hay xóa buffer khi gõ lệnh trong Ghostty, iTerm2, Kitty và Terminal.app.',
    category: 'troubleshooting',
    dateModified: '2026-11-10',
    detailHtml: `
      <h3 class="text-xl font-bold mt-2 mb-3 text-stone-900 border-b border-red-700 pb-1">1. Hiện tượng lỗi thường gặp</h3>
      <p class="mb-4">Khi dùng các bộ gõ tiếng Việt thông thường trong các ứng dụng Terminal hiện đại trên macOS (Ghostty, iTerm2, Kitty, Alacritty, WezTerm):</p>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Chữ bị gạch chân gián đoạn:</strong> Khi gõ lệnh (ví dụ <code>git commit -m "..."</code>), các ký tự đang soạn thảo bị gạch chân và che mất gợi ý autocompletion của shell (zsh-autosuggestions, fish).</li>
        <li><strong>Bị xóa trắng bộ đệm khi tắt marked text:</strong> Nếu người dùng tắt tính năng marked text trong cài đặt Terminal để tránh gạch chân, các bộ gõ khác sẽ xóa trắng sạch toàn bộ từ vừa gõ trên mỗi phím bấm.</li>
      </ul>

      <h3 class="text-xl font-bold mt-6 mb-3 text-stone-900 border-b border-red-700 pb-1">2. Giải pháp triệt để của GoTV</h3>
      <p class="mb-4">Từ phiên bản 0.9.9, GoTV áp dụng <strong>100% chuẩn Apple InputMethodKit (IMK)</strong> xử lý tận gốc:</p>
      <ul class="list-disc pl-6 mb-4 space-y-2">
        <li><strong>Không gạch chân (NSUnderlineStyleNone):</strong> Bộ gõ gửi chuỗi văn bản với thuộc tính không gạch chân, giữ giao diện dòng lệnh sạch đẹp tuyệt đối.</li>
        <li><strong>Khử xung đột Marked Range:</strong> Thuật toán kiểm tra an toàn vị trí con trỏ (<code>reconcileMarkedText:</code>), ngay cả khi tắt marked text thì bộ đệm phím vẫn được giữ nguyên vẹn 100%.</li>
        <li><strong>Không cần cấp quyền Accessibility (TCC):</strong> Tuân thủ Sandbox của macOS, không inject phím giả lập nguy hiểm vào process Terminal.</li>
      </ul>
    `
  }
];
