# Security Hardening

VKey là bộ gõ chạy với quyền truy cập bàn phím — bảo mật được đặt ưu tiên cao trong thiết kế.

## Threat Model

IME cục bộ trên Windows cá nhân. Kẻ tấn công chính: phần mềm độc hại cùng user session (không cần admin). MITM mạng là ưu tiên thấp.

---

## 1. Update System

| Biện pháp | Mô tả |
|-----------|-------|
| **SHA-256 hash verification** | Mỗi bản cập nhật đi kèm file `.sha256`. Sau khi tải, VKey tính hash thực tế bằng Windows CNG (bcrypt) và so khớp trước khi giải nén |
| **URL domain whitelist** | Chỉ chấp nhận tải từ `https://github.com/`, `https://objects.githubusercontent.com/`, `https://codeload.github.com/`. Từ chối HTTP và domain lạ |
| **PowerShell command escaping** | Escape ký tự `'` trong đường dẫn trước khi truyền vào `Expand-Archive`, chống command injection |
| **ZIP path validation** | Tham số `--install-update` chỉ chấp nhận file trong `%TEMP%`. Dùng `GetFullPathNameW()` để resolve path traversal (`..\..\evil.zip`) |

## 2. IPC & Shared Memory

| Biện pháp | Mô tả |
|-----------|-------|
| **Restricted DACL** | Shared memory, config event, mutex đều dùng SDDL `D:PAI(A;;GA;;;SY)(A;;GA;;;CO)` — chỉ SYSTEM và Creator/Owner được truy cập. Chặn pre-create attack từ process khác |
| **Session-scoped names** | Tất cả named objects dùng prefix `Local\` — không expose ra session khác |
| **Seqlock protocol** | Reader/writer dùng epoch + `std::atomic_thread_fence(acquire)` đảm bảo đọc ghi không bị race condition, tương thích cả x86 và ARM |
| **Magic number validation** | SharedState kiểm tra magic `0x4E4B4559` ('NKEY'), version và size trước khi sử dụng |
| **Volatile pointers** | Memory-mapped view dùng `volatile` ngăn compiler cache giá trị cũ |

## 3. Config Loading

| Biện pháp | Mô tả |
|-----------|-------|
| **File size limit** | Config > 1 MB → bỏ qua, dùng default. Chống OOM từ config.toml khổng lồ |
| **Macro bounds** | Key tối đa 32 ký tự, value tối đa 512 ký tự. Entry vượt giới hạn bị skip |
| **Per-app limit** | Tối đa 256 per-app entries. Vượt quá → truncate |
| **Enum range check** | Validate CodeTable (0–4), InputMethod (0–2) trước khi cast. Giá trị lạ → fallback default |

## 4. DLL & TSF Security

| Biện pháp | Mô tả |
|-----------|-------|
| **HKCU CLSID cleanup** | Xóa `HKCU\Software\Classes\CLSID\{GUID}` mỗi lần khởi động + đăng ký DLL. Chặn DLL hijack qua HKCU override — kẻ tấn công không thể chèn DLL vào Word, Chrome qua registry |
| **Safe DLL loading** | `LoadLibraryW()` dùng full path, không dựa vào search path |
| **DisableThreadLibraryCalls** | Tắt thông báo DLL_THREAD_ATTACH/DETACH không cần thiết |

## 5. Memory Safety

| Biện pháp | Mô tả |
|-----------|-------|
| **SecureZeroMemory** | Xóa `inputHistory_` và `rawMacroBuffer_` khi chuyển focus app. Chống đọc lịch sử phím từ memory dump |
| **Smart pointers** | `unique_ptr` cho handle management, `CComPtr` cho COM — tự động cleanup, không leak |
| **No unsafe functions** | Không dùng `wcscpy`, `strcpy`, `sprintf`, `gets`. Chỉ dùng `std::wstring`, `StringCchPrintfW`, `swprintf_s` |
| **Zero-initialization** | SharedState `InitDefaults()` khởi tạo tường minh tất cả fields |

## 6. Process Security

| Biện pháp | Mô tả |
|-----------|-------|
| **Single instance mutex** | Mutex với restricted DACL chống chạy trùng instance |
| **UAC elevation** | Đăng ký TSF yêu cầu admin với consent dialog. Không tự nâng quyền ngầm |
| **Subprocess isolation** | Settings/Macro dialog chạy process riêng biệt |

## 7. Compiler & Linker Hardening

| Flag | Mục đích |
|------|----------|
| `/W4 /WX` | Warning level cao nhất + coi warning là error |
| `/permissive-` | C++ standards-conforming nghiêm ngặt |
| `/GS` | Stack buffer overrun detection (mặc định MSVC) |
| `/DYNAMICBASE` | ASLR — random hóa địa chỉ load |
| `/NXCOMPAT` | DEP — ngăn thực thi code trên stack/heap |
| `/HIGHENTROPYVA` | High-entropy ASLR cho 64-bit |

## 8. Hook Engine

| Biện pháp | Mô tả |
|-----------|-------|
| **Magic marker** | SendInput event gắn `VKEY_EXTRA_INFO = 0x4E4B` — ngăn hook xử lý lại phím do chính VKey tạo ra |
| **Focus isolation** | Chuyển app → xóa buffer, commit/discard composition. Không leak keystroke qua ranh giới ứng dụng |
| **Bounded buffers** | Input buffer có giới hạn, macro expansion bounded bởi config limits |

---

## Testing

33 unit test chuyên cho bảo mật trong `tests/UpdateSecurityTest.cpp`:
- PowerShell escaping (6 tests)
- URL whitelist validation (14 tests)
- SHA-256 hash parsing (7 tests)
- Edge cases: empty string, invalid hex, oversized hash, case normalization

---

*Chi tiết kỹ thuật đầy đủ: xem `docs/SECURITY_FIXES-distillate.md`*
