# sinh_hoc — AI Sinh Học

Giám sát **nhiệt độ · độ ẩm · CO₂** trên OLED 1.3" (ESP32) và app Flutter qua MQTT AloT.

## Phần cứng

| Thiết bị | ESP32 |
|----------|-------|
| OLED 1.3" SH1106 SCL/SDA | GPIO 22 / 21 |
| ENS160+AHT21 SCL/SDA | GPIO 26 / 25 |
| DHT11 DATA | GPIO 27 |
| Nguồn | 3V3 / GND chung |

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

- Push tag `v*` (vd: `v1.0.0`) **hoặc** chạy tay **Actions → Build & Release → Run workflow**
- Build **APK** (Android) + **IPA** (iOS, unsigned — sideload bằng Sideloadly/AltStore)
- Tự tạo **GitHub Release** kèm 2 file

```bash
git tag v1.0.0
git push origin v1.0.0
```

> IPA unsigned cần cài bằng Sideloadly (Windows) với Apple ID của bạn. Muốn signed IPA store thì thêm secrets signing Apple sau.
