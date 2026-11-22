# Tài liệu AI Sinh Học — dành cho giáo viên

Bộ tài liệu này viết **ngắn, rõ, ít thuật ngữ**, để giáo viên cấp 2 có thể tự làm theo và hướng dẫn học sinh.

| File | Dùng khi nào |
|------|----------------|
| [01 — Hướng dẫn kết nối WiFi & app](01-huong-dan-ket-noi.md) | Lắp máy lần đầu, đổi WiFi nhà/trường, mở app xem số đo |
| [02 — Bài giảng lớp (45–90 phút)](02-bai-giang.md) | Soạn giáo án / dạy học sinh về không khí, CO₂, IoT |
| [03 — Sơ đồ mạch & nối dây](03-so-do-mach.md) | Kiểm tra dây, sửa lỗi màn hình / cảm biến, dạy STEM |

## Bộ kit gồm gì?

```
┌─────────────────────────────────────┐
│  Màn hình OLED (hiện số đo)         │
│  Cảm biến nhiệt độ · độ ẩm · CO₂    │
│  Bo mạch ESP32 (máy “não”)          │
│  App điện thoại “AI Sinh Học”       │
└─────────────────────────────────────┘
```

Máy đo **nhiệt độ (°C)**, **độ ẩm (%)**, **CO₂ (ppm)** — hiện trên màn hình nhỏ và gửi lên điện thoại qua WiFi.

## Quy trình nhanh (tóm tắt 1 trang)

1. Cắm nguồn USB cho máy → nhìn màn hình.
2. Nếu hiện **CAI DAT WIFI** → điện thoại nối WiFi tên **`AI_SinhHoc`** → mở trình duyệt gõ **`192.168.4.1`** → chọn WiFi **2.4GHz** nhà/trường.
3. Màn hình hết chữ “CAI DAT WIFI” → máy đã vào mạng.
4. Mở app → Chip ID mặc định **`800`** → xem số đo.

> **Lưu ý quan trọng:** Chỉ dùng WiFi **2.4GHz**. WiFi **5GHz** máy không nối được.
