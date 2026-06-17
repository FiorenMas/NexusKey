# VKey v4.1.0

[![Signed by SignPath](https://img.shields.io/badge/Signed_by-SignPath-blue)](https://signpath.io)

**✨ Tính năng mới & Cải tiến:**
- Hỗ trợ ký số (Digital Signature): Kể từ phiên bản này, tất cả các file nhị phân của VKey đều được ký số chính thức (qua chương trình SignPath Foundation). VKey tự hào là bộ gõ tiếng Việt mã nguồn mở đầu tiên trên Windows có ký số chính thức, giúp loại bỏ hoàn toàn các cảnh báo của Windows SmartScreen khi cài đặt. Xin chân thành cảm ơn [SignPath Foundation](https://signpath.org/) và [SignPath.io](https://signpath.io/) đã tài trợ ký số miễn phí cho dự án, cùng cộng đồng đã hỗ trợ trong thời gian qua.
- Hỗ trợ tự động fallback đăng ký TSF per-user (`HKEY_CURRENT_USER`) khi không chạy bằng quyền Administrator, giúp người dùng không có quyền admin vẫn có thể cài đặt và kích hoạt chế độ TSF bình thường.
- Cho phép cấu hình phương thức gửi phím "Thay thế trực tiếp (EM_REPLACESEL)" cho từng ứng dụng, giúp gõ tiếng Việt mượt mà hơn trong các ô nhập liệu tương thích mà không cần thông qua clipboard hay giả lập phím vật lý.
- Cải tiến chế độ tự động viết hoa phím tắt (Auto Capitalization): tự động chuyển đổi cụm từ mở rộng thành dạng viết hoa từng chữ (Title Case) khi gõ phím tắt viết hoa chữ cái đầu (Ví dụ: `lhq` -> `liên hiệp quốc` thì gõ `Lhq` -> `Liên Hiệp Quốc`, gõ `LHQ` -> `LIÊN HIỆP QUỐC`)
- Tăng thời gian delay kiểm tra cập nhật lúc khởi động từ 3 giây lên 30 giây để đảm bảo kết nối mạng và Windows ổn định
- Tự động sao lưu cấu hình gõ (`config.toml` sang `config.toml.bak`) trước khi cập nhật, tự động fallback sang `%APPDATA%\VKey` nếu không có quyền ghi
- Hiển thị thông báo vị trí lưu file cấu hình dự phòng cho người dùng trước khi đóng ứng dụng để cập nhật

**🛠 Sửa lỗi:**
- Sửa lỗi kẹt phím nóng (hotkey) không thể hoạt động sau khi khóa màn hình (Win+L)
- Sửa lỗi không gõ được tiếng Việt cho từ đầu sau khi nhấn hotkey
- Tối ưu tính năng reinstall hook, tránh tranh chấp gây lag app không mong muốn
- Sửa lỗi nuốt chữ (không gõ được) và tối ưu hóa triệt để hiện tượng lag khi gõ trong ô tìm kiếm (Search/Find dialog) của Notepad trên Windows 11
- Sửa lỗi tự động cập nhật thất bại và không tự chạy lại sau khi cập nhật khi ứng dụng khởi chạy cùng hệ thống (do giới hạn Job Object ngăn cản breakaway)
- Bảo vệ và tránh ghi đè file cấu hình `config.toml` của người dùng khi cập nhật phiên bản mới
- Tăng thời gian chờ của bộ cài đặt từ 30 giây lên 120 giây để người dùng kịp đọc và xác nhận thông báo sao lưu cấu hình
- Sửa lỗi phím Backspace xóa sai ký tự khi đang chọn text hoặc hiện gợi ý autocomplete (Issue #195)
- Sửa lỗi khóa file khi ghi log debug gây cản trở việc xóa file của các ứng dụng khác (Issue #196)
- Đồng bộ hóa chính xác trạng thái icon khay hệ thống (tray icon) và tính năng Smart-Switch khi gõ trong các ứng dụng TSF (như Chrome, Edge)
- Khắc phục lỗi mất định dạng viết hoa (ví dụ `VKey` bị đổi thành `Vkey`) khi kích hoạt tính năng tự động khôi phục (auto-restore) từ khóa không hợp lệ
- Sửa lỗi chuyển mã nhanh (Quick Convert): tự động fallback lấy nội dung từ clipboard nếu không có text nào được bôi đen (select) khi chạy chuyển mã nhanh, đồng thời tối ưu hóa thời gian trễ (delay). Giúp hỗ trợ dùng chung với powertoys
- Sửa lỗi đường dẫn cấu hình chứa tiếng Việt: khắc phục hoàn toàn lỗi không lưu được cấu hình (`config.toml`) khi đường dẫn thư mục chứa ứng dụng hoặc thư mục người dùng chứa ký tự Unicode tiếng Việt có dấu

---

**🔒 Xác minh bản tải (Verify this release):**
- File nhị phân được ký số (Authenticode) — chuột phải `VKey.exe` → **Properties → Digital Signatures** để xem chứng chỉ.
- Kèm [build attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations) từ GitHub Actions:
  ```bash
  gh attestation verify VKey.zip --repo PhatMT97/VKey
  ```

**Sponsors**

Free code signing on Windows provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/). Thank you, SignPath! 🙏
