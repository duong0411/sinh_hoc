# Bài giảng: Không khí lớp học & máy AI Sinh Học

> Đối tượng: **học sinh cấp 2** (lớp 6–9)  
> Người dạy: giáo viên (không cần biết code)  
> Thời lượng gợi ý: **1 tiết (45 phút)** hoặc **2 tiết (90 phút)**  
> Mục tiêu: hiểu vì sao cần đo không khí; đọc được số trên máy; làm thí nghiệm nhỏ; liên hệ sức khỏe.

---

## 1. Mục tiêu bài học (viết lên bảng)

Cuối buổi, học sinh:

1. Nói được máy đo **3 đại lượng**: nhiệt độ, độ ẩm, CO₂.
2. Đọc được số trên màn hình OLED và trên app.
3. Giải thích được: **CO₂ cao** → phòng thiếu thông gió.
4. Đề xuất được **1 hành động** cải thiện không khí lớp (mở cửa, trồng cây, giảm đông…).

**Không yêu cầu** học sinh lập trình.

---

## 2. Kiến thức nền — giải thích như nói chuyện

### 2.1. Không khí trong lớp học

Không khí quanh ta có nhiều thành phần. Bài này chỉ quan tâm 3 thứ máy đo được:

| Tên | Đơn vị | Ví dụ đời thường |
|-----|--------|------------------|
| **Nhiệt độ** | °C | Nóng / lạnh trong lớp |
| **Độ ẩm** | % | Không khí khô / ẩm (khó thở, mốc…) |
| **CO₂** (khí carbon dioxide) | ppm | Hơi thở của người thải ra |

**ppm** = “phần triệu” — cứ 1 triệu phần không khí thì có bao nhiêu phần CO₂.  
Học sinh **không cần nhớ công thức**, chỉ cần nhớ: **số càng lớn → CO₂ càng nhiều**.

### 2.2. Vì sao CO₂ quan trọng trong lớp?

- Người thở ra **CO₂**.
- Phòng kín, nhiều người → CO₂ tăng dần.
- CO₂ cao lâu dài dễ kèm cảm giác: mệt, khó tập trung, buồn ngủ.

> Nói với học sinh: “Máy không thay bác sĩ. Máy chỉ là **đồng hồ đo không khí** để lớp ta tự chăm phòng học.”

### 2.3. Mốc đọc số CO₂ (thang đơn giản cho lớp)

| Khoảng CO₂ (ppm) | Gợi ý đọc cho học sinh |
|------------------|-------------------------|
| Khoảng **400–800** | Không khí khá tốt / thông thoáng |
| Khoảng **800–1200** | Nên **mở cửa / quạt** |
| Trên **1200** | Phòng đang **bí** — cần thông gió ngay |

*(Đây là thang tham khảo giáo dục, không phải chẩn đoán y tế.)*

### 2.4. Máy “AI Sinh Học” làm gì?

```
  Cảm biến “ngửi” không khí
           │
           ▼
     Bo mạch ESP32
     (não máy — tính & gửi số)
           │
     ┌─────┴─────┐
     ▼           ▼
  Màn OLED    WiFi → App điện thoại
  (nhìn tại chỗ)   (nhìn từ xa)
```

Từ khóa dễ nhớ cho học sinh:

- **Cảm biến** = “mũi” của máy  
- **ESP32** = “não”  
- **OLED** = “mặt đồng hồ”  
- **WiFi + app** = “gửi số về điện thoại cô/thầy”

---

## 3. Giáo án 45 phút (1 tiết)

| Phút | Hoạt động | Việc giáo viên làm | Việc học sinh làm |
|------|-----------|--------------------|-------------------|
| 0–5 | Mở đầu | Hỏi: “Lớp kín cửa 2 tiếng — các em thấy thế nào?” | Trả lời ngắn |
| 5–12 | Kiến thức | Giảng mục 2 (nhiệt độ, độ ẩm, CO₂, thang ppm) | Ghi 3 dòng vào vở |
| 12–20 | Làm quen máy | Bật máy, chỉ OLED: CO2 / TEMP / HUM | Đọc to số hiện tại |
| 20–30 | Thí nghiệm 1 | Đóng cửa 5–8 phút, ghi số CO₂ đầu & cuối | Điền bảng thí nghiệm |
| 30–38 | Thảo luận | So sánh số trước/sau; liên hệ mở cửa sổ | Nhóm 2–3 em nói 1 câu kết luận |
| 38–45 | Củng cố | 3 câu hỏi nhanh (mục 6) | Trả lời miệng / phiếu |

### Nếu đã cấu hình WiFi sẵn

Cho 1–2 nhóm nhìn **app** trên điện thoại giáo viên (chiếu lên máy chiếu nếu được) — học sinh thấy số **giống OLED**.

### Nếu chưa cấu hình WiFi

Chỉ dạy với **OLED**. Kết nối WiFi làm **ngoài giờ** theo file [01-huong-dan-ket-noi.md](01-huong-dan-ket-noi.md) — tránh mất thời gian tiết học.

---

## 4. Giáo án 90 phút (2 tiết) — thêm thí nghiệm

**Tiết 1:** như bảng 45 phút ở trên.

**Tiết 2 — mở rộng STEM nhẹ:**

| Phút | Hoạt động |
|------|-----------|
| 0–10 | Ôn nhanh 3 đại lượng + thang CO₂ |
| 10–25 | **Thí nghiệm 2:** 8–10 học sinh đứng gần máy, thở bình thường 3 phút → quan sát CO₂ |
| 25–40 | **Thí nghiệm 3:** Mở hết cửa/sổ 5 phút → CO₂ giảm thế nào? |
| 40–55 | Vẽ sơ đồ “đường đi của số đo” (cảm biến → não → màn hình / app) — xem [03-so-do-mach.md](03-so-do-mach.md) phần đơn giản |
| 55–70 | Nhóm đề xuất **“Nội quy không khí lớp”** (3 quy tắc) |
| 70–85 | Trình bày 1 phút / nhóm |
| 85–90 | Giáo viên chốt + giao bài về nhà |

---

## 5. Phiếu thí nghiệm (photo / in phát học sinh)

```
PHIẾU THÍ NGHIỆM — KHÔNG KHÍ LỚP HỌC
Họ tên: ..........................  Lớp: ........  Ngày: ........

1) Số đo LÚC BẮT ĐẦU
   Nhiệt độ: ........ °C
   Độ ẩm:    ........ %
   CO₂:      ........ ppm

2) Việc lớp làm (khoanh 1 lựa chọn):
   □ Đóng kín cửa  □ Đông người gần máy  □ Mở cửa thông gió

3) Số đo SAU THÍ NGHIỆM (sau ...... phút)
   Nhiệt độ: ........ °C
   Độ ẩm:    ........ %
   CO₂:      ........ ppm

4) CO₂ đã:  □ Tăng   □ Giảm   □ Gần như không đổi

5) Em giải thích vì sao (1–2 câu):
   ................................................................
   ................................................................

6) Việc lớp nên làm tuần này:
   ................................................................
```

---

## 6. Câu hỏi củng cố (3–5 phút)

1. Máy đo những gì? → **Nhiệt độ, độ ẩm, CO₂**  
2. Đơn vị CO₂ trên máy là gì? → **ppm**  
3. CO₂ tăng khi nào? → **Nhiều người / phòng kín / thiếu thông gió**  
4. Thấy CO₂ cao, lớp nên làm gì trước? → **Mở cửa / quạt / giảm đông cục bộ**  
5. OLED và app khác nhau chỗ nào? → **OLED xem tại máy; app xem qua điện thoại (cùng một số đo)**

---

## 7. An toàn & kỷ luật khi dùng máy

- Không tự tháo dây nếu chưa có giáo viên hướng dẫn (xem sơ đồ mạch).
- Không đổ nước lên bo mạch / cảm biến.
- Cắm nguồn USB ổn định; không giật dây khi đang đo.
- Không thổi trực tiếp mạnh vào cảm biến quá gần (số sẽ nhảy ảo).
- App chỉ xem số — **không cần** học sinh cấu hình WiFi trừ khi giáo viên giao nhiệm vụ STEM riêng.

---

## 8. Gợi ý liên môn

| Môn | Gợi ý gắn bài |
|-----|----------------|
| **Sinh học** | Hô hấp tế bào / trao đổi khí ở người |
| **Hóa học** | Công thức CO₂; khí trong không khí |
| **Vật lý** | Cảm biến, tín hiệu, đo lường |
| **Tin học** | IoT = “Internet của mọi thứ” (máy kết nối mạng) |
| **GDCD / kỹ năng** | Chăm sóc môi trường lớp học chung |

---

## 9. Script giáo viên (đọc gần như nguyên văn nếu muốn)

> “Hôm nay lớp mình có một chiếc máy nhỏ. Nó không phải điện thoại, cũng không phải máy tính. Nó là **máy đo không khí**.  
> Mỗi lần các em thở ra, trong hơi thở có khí **CO₂**. Phòng đóng kín lâu, CO₂ tăng — giống như phòng bí.  
> Máy sẽ hiện ba số: nóng lạnh, ẩm khô, và CO₂.  
> Việc của chúng ta không phải học code, mà là **đọc số – hiểu số – hành động**: mở cửa, thông gió, giữ lớp dễ thở hơn.”

---

## 10. Bài về nhà (tùy chọn)

Viết 5–7 dòng:

1. Hôm nay lớp đo được CO₂ khoảng bao nhiêu?  
2. Em cảm thấy lớp bí hay thoáng?  
3. Em đề xuất một việc nhỏ để tuần sau không khí tốt hơn.

---

## Phụ lục — Thuật ngữ “dịch” sang tiếng dễ

| Thuật ngữ kỹ thuật | Nói với học sinh |
|--------------------|------------------|
| Cảm biến | Mũi đo của máy |
| OLED | Màn hình nhỏ trên máy |
| ESP32 | Não máy |
| WiFi 2.4GHz | Loại sóng WiFi máy hiểu được |
| MQTT / broker | “Bưu điện” trên mạng chuyển số đo về app |
| Chip ID `800` | Số hiệu máy (như số báo danh của thiết bị) |
| ppm | Đơn vị đếm CO₂ |
| Portal / CAI DAT WIFI | Máy đang nhờ điện thoại cho mật khẩu WiFi |
