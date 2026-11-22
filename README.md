# sinh_hoc — AI Sinh Học

Giám sát **nhiệt độ · độ ẩm · CO₂** trên OLED 1.3" (ESP32) và app Flutter qua MQTT AloT.

## Phần cứng

| Thiết bị | ESP32 |
|----------|-------|
| OLED 1.3" SH1106 SCL/SDA | GPIO 22 / 21 |
| ENS160+AHT21 SCL/SDA | GPIO 26 / 25 |
| DHT11 DATA | GPIO 27 |
| Nguồn | 3V3 / GND chung |

## Tài liệu cho giáo viên / khách hàng

File Word trong thư mục [`docs/`](docs/) (mở bằng Microsoft Word / Google Docs):

- [`00-Muc-luc.docx`](docs/00-Muc-luc.docx) — mục lục
- [`01-Huong-dan-ket-noi.docx`](docs/01-Huong-dan-ket-noi.docx) — kết nối WiFi & app
- [`02-Bai-giang.docx`](docs/02-Bai-giang.docx) — bài giảng lớp cấp 2
- [`03-So-do-mach.docx`](docs/03-So-do-mach.docx) — sơ đồ mạch & nối dây

## Firmware

`firmware/AI_SinhHoc_ESP32/` — chipId MQTT: **`800`**

AP cấu hình WiFi: `AI_SinhHoc` → http://192.168.4.1

## App mobile

```bash
cd mobile
flutter pub get
flutter run
```

## CI/CD — APK & IPA

Workflow: `.github/workflows/release.yml`

- Push tag `v*` (vd: `v1.0.2`) **hoặc** chạy tay **Actions → Build & Release → Run workflow**
- Build **APK** (Android) + **IPA** (iOS, unsigned — Sideloadly/AltStore)
- Release **chỉ tạo khi cả APK và IPA đều build OK** (tránh thiếu APK như v1.0.1)

```bash
git tag v1.0.2
git push origin v1.0.2
```

APK build tay:

```bash
cd mobile
flutter build apk --release --no-tree-shake-icons
# → build/app/outputs/flutter-apk/app-release.apk
```

> IPA unsigned cài bằng Sideloadly (Windows) với Apple ID. Muốn IPA store thì thêm secrets signing Apple.
