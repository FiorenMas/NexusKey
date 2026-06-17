# VKey - Bộ gõ tiếng Việt hiện đại cho Windows

[![Build](https://github.com/phatMT97/VKey/actions/workflows/build.yml/badge.svg)](https://github.com/phatMT97/VKey/actions/workflows/build.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPLv3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Engine: Dual License](https://img.shields.io/badge/Engine-Dual%20License-orange.svg)](LICENSE-COMMERCIAL)
[![Release](https://img.shields.io/github/v/release/phatMT97/VKey)](https://github.com/phatMT97/VKey/releases)
[![Code signing](https://img.shields.io/badge/Code_signing-SignPath_Foundation-success)](https://signpath.org/)

<p align="center">
  <img src="docs/images/vkey-compact.png" alt="VKey Compact View" width="250">
  <img src="docs/images/vkey-expanded.png" alt="VKey Expanded View" width="550">
</p>
<p align="center"><em>Giao diện VKey: Chế độ thu gọn (trái) và Cài đặt mở rộng (phải)</em></p>

<p align="center">
  <img src="docs/images/vkey-full-UI.png" alt="VKey Full UI" width="800">
</p>
<p align="center"><em>Giao diện đầy đủ VKey</em></p>

<p align="center">
  <img src="docs/images/nexus-classic.png" alt="VKey Classic UI" width="700">
</p>
<p align="center"><em>Giao diện Classic (Win32 native) — nhẹ, tương thích cao</em></p>

---

**[Giới thiệu](#giới-thiệu)** | **[Hướng dẫn sử dụng](docs/GUIDE.md)** | **[Tính năng](#tính-năng)** | **[Cài đặt](#cài-đặt)** | **[Xác minh](#xác-minh-bản-tải-verify-release)** | **[Build](#build-từ-mã-nguồn)** | **[Kiến trúc](#kiến-trúc)** | **[English](#english-version)** | **[Credits](#credits)**

---

> [!NOTE]
> **📖 Hướng dẫn sử dụng:** Để xem hướng dẫn chi tiết các tính năng, phím tắt và câu hỏi thường gặp (FAQ), vui lòng đọc [Hướng dẫn sử dụng VKey (GUIDE.md)](docs/GUIDE.md).

## Giới thiệu

**VKey** là bộ gõ tiếng Việt mã nguồn mở cho Windows, được viết lại từ hoàn toàn [NextKey](https://github.com/phatMT97/VKey/tree/feat/UI-Next) (một fork của [OpenKey](https://github.com/tuyenvm/OpenKey) của Mai Vũ Tuyên).

Engine mới, kiến trúc mới, C++20, hiệu năng cao, giao diện Glassmorphism.

> Engine VKey được tối ưu dựa trên nghiên cứu ngữ âm học tiếng Việt, có tham khảo mã nguồn [Unikey](https://www.unikey.org/source.html) của bác **Phạm Kim Long** cho các quy tắc âm vị học (VCPair, consonant restrictions). Xin chân thành cảm ơn!

<a name="privacy-policy"></a>
### Chính sách bảo mật
* **Không ghi phím:** VKey không thu thập, lưu trữ, hay gửi phím bạn gõ đi đâu.
* **Không thu thập dữ liệu:** Không có dữ liệu cá nhân nào được gửi lên server.
* **Hoạt động offline:** Phần mềm hoạt động hoàn toàn cục bộ trên máy bạn.
* **Mã nguồn mở:** Bạn có thể tự kiểm chứng bằng cách đọc mã nguồn.

> **Lưu ý:** Dự án này được phát triển chủ yếu dựa trên nhu cầu và trải nghiệm cá nhân, vì vậy có thể vẫn tồn tại một số lỗi chưa được phát hiện hoặc khắc phục triệt để. Rất mong nhận được sự thông cảm và đóng góp ý kiến thông qua [Issue](https://github.com/phatMT97/VKey/issues) để bộ gõ ngày càng hoàn thiện hơn.

---

## 🚀 Key Features

* **Smart Switch per app**
  Tự động nhớ chế độ Việt/Anh theo từng ứng dụng

* **App Exclusion (Hard / Soft)**
  Linh hoạt kiểm soát bật/tắt tiếng Việt theo app (phù hợp game, tool đặc thù)

* **Advanced Macro & Typing**
  Gõ tắt mở rộng (lên đến 20.000 ký tự, hỗ trợ xuống hàng), hoạt động cả trong English mode, hỗ trợ phụ âm nhanh

* **Telex + VNI Combined**
  Hỗ trợ gõ song song cả Telex và VNI mà không cần chuyển đổi

* **Spell Check + Free Typing**
  Kiểm tra chính tả tiếng Việt + cho phép override khi cần gõ tự do

* **Powerful Convert Tool**
  Bôi đen → chuyển mã nhanh, hỗ trợ nhiều kiểu chuyển đổi (HOA, thường, bỏ dấu…)

* **Per-App Configuration**
  Tùy chỉnh bảng mã & kiểu gõ riêng cho từng ứng dụng

* **Context-Aware Input**
  Tự tắt khi dùng CJK, phát hiện tiếng Anh để tránh lỗi gõ

* **Modern UI**
  Glassmorphism, auto Light/Dark, tùy biến icon, hỗ trợ song ngữ

* **High Performance Engine**
  Độ trễ thấp, hỗ trợ song song Hook Engine & TSF (Context-Aware) để bắt ngữ cảnh tốt nhất, auto update, [tối ưu bảo mật](docs/SECURITY.md)

* **Game-Friendly Telex**
  Chơi game với Telex bình thường — WASD di chuyển mượt, không cần chuyển Simple Telex hay tắt tiếng Việt

* **Float icon in FullScreen**
  Hỗ trợ hiển thị icon status V/E nổi trên màn hình - thích hợp các app FullScreen

---

## Cài đặt

1. Tải phiên bản mới nhất tại **[Releases](https://github.com/phatMT97/VKey/releases)**.
2. Giải nén và chạy `VKey.exe`.
3. *(Khuyến nghị)* Tắt các bộ gõ khác (Unikey, EVKey) để tránh xung đột.

> **Ký số (Code signing):** Dịch vụ ký số (Authenticode) trên Windows cho VKey được cung cấp miễn phí bởi [SignPath.io](https://signpath.io/), chứng chỉ ký số bởi [SignPath Foundation](https://signpath.org/).

---

## Xác minh bản tải (Verify Release)

Mỗi bản phát hành đều được ký bằng [Sigstore](https://sigstore.dev) và đính kèm [build attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds) từ GitHub Actions — chứng minh file được build từ mã nguồn trong repo này.

```bash
# Xác minh bằng GitHub CLI
gh attestation verify VKey.zip --repo PhatMT97/VKey

# Xác minh bằng cosign
cosign verify-blob VKey.zip \
  --bundle VKey.zip.sigstore.json \
  --certificate-oidc-issuer=https://token.actions.githubusercontent.com \
  --certificate-identity-regexp="https://github.com/phatMT97/VKey/"
```

> **Lưu ý:** Đây không phải code signing truyền thống (Authenticode). Windows SmartScreen vẫn có thể cảnh báo khi chạy lần đầu — đây là hành vi bình thường với phần mềm mã nguồn mở chưa có chứng chỉ ký số.

---

## Build từ mã nguồn

### Yêu cầu

- **Windows 10/11**
- **Visual Studio 2022** (workload "Desktop development with C++" + ATL)
- **CMake 3.20+**

Các thư viện phụ thuộc (Sciter SDK, Google Test, toml++) đã có sẵn trong `extern/`.

### Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VKeyApp
```

### Chạy test

```powershell
cmake --build build --config Release --target VKeyTests
ctest --test-dir build --build-config Release --output-on-failure
```

---

## Kiến trúc

VKey gồm ba lớp:

| Lớp | Target | Mô tả |
|-----|--------|-------|
| **VKeyEngine** | Static lib | Engine gõ tiếng Việt thuần C++20 (Telex, VNI), kiểm tra chính tả, chuyển bảng mã. Không phụ thuộc platform. |
| **VKeyCore** | Static lib | Lớp platform — shared memory IPC, quản lý config, smart switch. |
| **VKeyApp** | Win32 EXE | Ứng dụng GUI với Sciter.JS, hook engine, tray icon, tự cập nhật. |
| **VKeyTSF** | DLL | Tích hợp Text Services Framework — đăng ký như Windows input method. |

### Tối ưu bộ nhớ

VKey được thiết kế với kiến trúc gọn nhẹ, không phụ thuộc runtime nặng, nên rất tiết kiệm tài nguyên — lúc đang chạy chỉ chiếm khoảng **1.6 ~ 2 MB RAM**. Con số này là *working set* mà Task Manager hiển thị: nó tự **lên xuống** theo mức sử dụng. Khi máy rảnh một lúc, hệ điều hành tự trim xuống còn khoảng **0.3 MB** (mức ~1.6–2 MB lúc đang dùng là hoàn toàn bình thường):

<p align="center">
  <img src="docs/images/ram-optimize.png" alt="VKey RAM Usage - 0.3 MB" width="700">
</p>
<p align="center"><em>VKey ở mức nghỉ ~0.3 MB sau khi idle — kiến trúc nhẹ, OS tự tối ưu</em></p>

> ℹ️ Thấy Task Manager hiện ~1.7 MB chứ không phải 0.3 MB? Đó là *working set* lúc đang dùng — bình thường và khỏe mạnh, không phải rò rỉ bộ nhớ. Chi tiết: [GUIDE — FAQ về RAM](docs/GUIDE.md#faq).

---

## English Version

<details>
<summary>Click to expand English version</summary>

### About

> [!NOTE]
> **📖 User Guide:** For detailed usage instructions, hotkeys, and FAQ, please refer to the [VKey User Guide (docs/GUIDE.md)](docs/GUIDE.md).

**VKey** is an open-source Vietnamese Input Method Editor (IME) for Windows, completely rewritten from [NextKey](https://github.com/phatMT97/VKey/tree/master) (based on [OpenKey](https://github.com/tuyenvm/OpenKey) by Mai Vu Tuyen).

New engine, new architecture, C++20, high performance, Glassmorphism UI. Engine phonology rules reference [Unikey](https://www.unikey.org/source.html) by Pham Kim Long.

### Privacy Policy
* **No Keylogging:** VKey does not collect, store, or transmit your keystrokes.
* **No Data Collection:** No personal data is sent to any server.
* **Offline First:** The software operates entirely locally on your machine.
* **Open Source:** You can verify this behavior by reviewing our source code.

### Features

**Input Methods & Code Tables**
- **Input methods:** Telex, VNI, Simple Telex, Combined (Telex + VNI)
- **Code tables:** Unicode, TCVN3, VNI Windows, Unicode Compound, Vietnamese Locale
- **Tone placement:** Modern (oà, uý) and classic (òa, úy) options

**Spell Check & English Detection**
- **Spell checker** — Vietnamese word validation, reduces accidental tone placement on English words
- **Free typing mode** — Bypass English detection, allows free tone placement (e.g., `yes` → `ýe`)
- **Quick-disable shortcuts** — Solo Ctrl temporarily disables spell check; Double-Alt temporarily disables Vietnamese for current word

**Smart Switching**
- **Smart Switch** — Automatically remembers V/E mode per application
- **Exclude Apps (Hard)** — Force English and block toggle completely for specified apps
- **Exclude Apps (Soft)** — Always reset to English when opening/switching to an app, but still allows toggling to Vietnamese via hotkey — ideal for fullscreen games
- **Auto-disable on CJK** — Disables Vietnamese input when keyboard layout is CJK (Chinese, Japanese, Korean)
- **Per-app configuration** — Code table and input method overrides per application
- **Import/Export** — Import and export excluded apps lists from file

**Macros & Quick Typing**
- **Macros** — Text expansion shortcuts (e.g., `addr` → full address), supports up to 20,000 characters and newlines
- **Macros in English mode** — Allows macro expansion while in English mode
- **Quick consonants** — cc→ch, gg→gi, nn→ng, plus quick start/end consonant shortcuts
- **Auto-capitalize** — Automatically capitalizes first letter after sentence-ending punctuation, including macro output and TSF context-aware support

**Convert Tool**
- **Quick convert via selection** — Select text and press hotkey to convert instantly
- **Sequential conversion** — Automatically cycles through conversion options (UPPERCASE, lowercase, Title Case, Remove accents...) then returns to original. Only active when "Auto paste + select" is enabled
- **Multi-encoding support** — Convert between Unicode, TCVN3, VNI Windows

**User Interface**
- **Glassmorphism UI** — Transparent, backdrop blur, rounded corners in Windows 11 style
- **Auto Theme Sync** — Automatically switches Light/Dark in real-time with Windows, no restart needed
- **Icon Color** — Customizable V/E icon colors on the system tray
- **Bilingual** — UI supports both Vietnamese and English

**System**
- **Parallel Engine** — Parallel Hook Engine and TSF (Context-Aware) support for superior tracking and document awareness
- **TSF Engine** — Text Services Framework integration for modern applications, with per-app TSF selection
- **Auto-update** — Built-in update checker and installer
- **[Security hardened](docs/SECURITY.md)** — Code optimized to minimize security vulnerabilities
- **High performance** — Optimized system processing

### Installation

1. Download the latest version from **[Releases](https://github.com/phatMT97/VKey/releases)**.
2. Extract and run `VKey.exe`.
3. *(Recommended)* Disable other IMEs (Unikey, EVKey) to avoid conflicts.

> **Code signing:** Free Authenticode code signing on Windows for VKey is provided by [SignPath.io](https://signpath.io/), with the certificate issued by [SignPath Foundation](https://signpath.org/).

### Building

**Prerequisites:** Windows 10/11, Visual Studio 2022 (Desktop C++ + ATL), CMake 3.20+

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VKeyApp
```

### Architecture

| Layer | Target | Description |
|-------|--------|-------------|
| **VKeyEngine** | Static lib | Pure C++20 Vietnamese input engines (Telex, VNI), spell checker, code table converter. Zero platform dependencies. |
| **VKeyCore** | Static lib | Platform layer — shared memory IPC, configuration management, smart switch. |
| **VKeyApp** | Win32 EXE | GUI application with Sciter.JS UI, hook engine, tray icon, auto-update. |
| **VKeyTSF** | DLL | Text Services Framework integration — registers as a Windows input method. |

#### Memory Optimization

VKey is built with a lean architecture and no heavy runtime dependencies, keeping resource usage minimal — only **~1.6–2 MB RAM** while active. That figure is the *working set* shown in Task Manager: it naturally **rises and falls** with usage. After a period of inactivity the OS trims it down to as low as **0.3 MB** (seeing ~1.6–2 MB while you're typing is perfectly normal):

<p align="center">
  <img src="docs/images/ram-optimize.png" alt="VKey RAM Usage - 0.3 MB" width="700">
</p>
<p align="center"><em>VKey at its idle floor (~0.3 MB) — lightweight architecture, trimmed automatically by the OS</em></p>

> ℹ️ Task Manager shows ~1.7 MB instead of 0.3 MB? That's the active *working set* — normal and healthy, not a memory leak. Details: [GUIDE — RAM FAQ](docs/GUIDE.md#faq).

</details>

---

## Sponsors

<table>
  <tr>
    <td align="center" width="80">
      <a href="https://signpath.org/"><img src="https://signpath.org/assets/favicon-50x50.png" alt="SignPath" width="40"></a>
    </td>
    <td>
      Free code signing on Windows provided by <a href="https://signpath.io/">SignPath.io</a>, certificate by <a href="https://signpath.org/">SignPath Foundation</a>
    </td>
  </tr>
</table>

## Credits

- Kế thừa từ [NextKey](https://github.com/phatMT97/VKey/tree/master), lấy cảm hứng từ [OpenKey](https://github.com/tuyenvm/OpenKey) của Mai Vũ Tuyên
- Quy tắc âm vị học tham khảo từ [Unikey](https://www.unikey.org/source.html) của Phạm Kim Long
- Giao diện bởi [Sciter.JS](https://sciter.com/)
- Đọc config bởi [toml++](https://github.com/marzer/tomlplusplus)
- Testing bởi [Google Test](https://github.com/google/googletest)
- Tham khảo rule -ing cho spell check của [Gonhanh.org](https://github.com/khaphanspace/gonhanh.org?tab=readme-ov-file#-t%C3%A0i-li%E1%BB%87u-k%E1%BB%B9-thu%E1%BA%ADt)
- Tham khảo config TSF từ [VietType](https://github.com/dinhngtu/VietType)
- Tham khảo cách xử lý clipboard input từ [SigmaLib](https://github.com/phamhoangnhat/SigmaLib) của Phạm Hoàng Nhật
- Tham khảo quy tắt tiếng việt từ [dotnetkey](https://code.google.com/archive/p/dotnetkey/downloads)
- Dịch vụ ký số (Authenticode) trên Windows cung cấp miễn phí bởi [SignPath.io](https://signpath.io/), chứng chỉ ký số bởi [SignPath Foundation](https://signpath.org/)

### Top Testers
Cảm ơn các thành viên cộng đồng đã test và góp ý:
- [Zenfas](https://github.com/Zenfas)
- [huntersun](https://github.com/huntersun)
- [haihv8x](https://github.com/haihv8x)
- [os-hoanghv](https://github.com/os-hoanghv)
- [Shzr0](https://github.com/Shzr0)
- [nghiabros](https://github.com/nghiabros)

## License

Dự án sử dụng **dual license**:

| Thành phần | License |
|------------|---------|
| **Engine** (`src/core/engine/*`) | [AGPL-3.0](LICENSE) **hoặc** [Commercial](LICENSE-COMMERCIAL) |
| **Tất cả phần còn lại** | [AGPL-3.0](LICENSE) |

- **Sử dụng open-source (AGPL-3.0):** Bạn có thể sử dụng, sửa đổi, phân phối lại toàn bộ mã nguồn với điều kiện giữ nguyên license AGPL-3.0 cho derivative works — bao gồm cả khi cung cấp dưới dạng dịch vụ qua mạng (SaaS).
- **Sử dụng thương mại/closed-source:** Nếu muốn dùng engine VKey trong sản phẩm proprietary hoặc né nghĩa vụ AGPL, vui lòng liên hệ tác giả để lấy commercial license. Xem [LICENSE-COMMERCIAL](LICENSE-COMMERCIAL).

## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=phatMT97/VKey&type=date&legend=top-left)](https://www.star-history.com/?repos=phatMT97%2FVKey&type=date&legend=top-left)
