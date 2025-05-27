# IoT Smart Home Panel

## 🏠 Deskripsi Proyek

Proyek ini merupakan implementasi panel kontrol IoT bertemakan *Smart Home*, dikembangkan sebagai bagian dari konten pembelajaran berbasis Internet of Things (IoT). Sistem ini terdiri dari beberapa modul yang saling terintegrasi dan dikendalikan melalui *Home Assistant* pada Raspberry Pi 4.

## 🔧 Komponen Sistem

### 1. Sistem Kontrol Utama (ESP32-S3)

* Sensor DHT22
* Relay 8 Channel (kontrol 5 lampu: utama, A1, A2, B1, B2)
* Kipas, buzzer, dan solenoid
* OLED SSD1306

### 2. Sistem Monitoring Daya (ESP32-C3)

* Sensor daya PZEM-004T v3.0
* OLED SSD1306

### 3. Sistem Pembaca RFID (ESP32-C6)

* Modul RFID PN532
* LED RGB indikator akses

### 4. Server & Integrasi

* Raspberry Pi 4 + Home Assistant OS
* Integrasi melalui MQTT (dengan dukungan Auto-Discovery)

## 📂 Struktur Repositori

```
├── /esp32s3/            # Source code ESP32-S3 (kontrol utama)
├── /esp32c3/            # Source code ESP32-C3 (monitoring daya)
├── /esp32c6/            # Source code ESP32-C6 (akses RFID)
├── /home-assistant/
│   ├── configuration.yaml       # Konfigurasi entitas
│   ├── automations.yaml         # Otomasi dasar
│   └── ui_lovelace.yaml         # Tampilan UI dashboard
├── /diagram/           # Gambar diagram sistem (placeholder)
└── README.md           # Dokumentasi proyek
```

## 📷 Dokumentasi Visual

Letakkan file gambar dalam folder `/diagram/`. Berikut beberapa gambar yang **disarankan** disertakan:

* `diagram_sistem_iot.png` – Diagram sistem IoT Smart Home keseluruhan
* `diagram_kontrol_utama.png` – Diagram sistem ESP32-S3 (kontrol utama)
* `diagram_monitor_daya.png` – Diagram sistem ESP32-C3 (monitor daya)
* `diagram_rfid_akses.png` – Diagram sistem ESP32-C6 (akses RFID)
* `diagram_panel_fisik.png` – Diagram atau foto panel secara fisik

**Contoh penggunaan gambar:**

```md
![Diagram Sistem IoT](diagram/diagram_sistem_iot.png)
```

## ✨ Fitur Unggulan

* Kendali perangkat melalui dashboard Home Assistant
* Monitoring suhu, kelembapan, dan daya secara real-time
* Sistem kontrol akses berbasis UID RFID
* Tampilan status pada OLED setiap modul
* Sinkronisasi melalui MQTT dengan struktur topik standar
* Dukungan penuh MQTT Auto-Discovery untuk Home Assistant

## ⚡ Teknologi yang Digunakan

* ESP32-S3, ESP32-C3, ESP32-C6
* Home Assistant + MQTT + YAML
* Sensor: DHT22, PZEM-004T, PN532
* UI Lovelace Dashboard

## 🎓 Lisensi

Proyek ini dibuat untuk keperluan pembelajaran. Silakan modifikasi sesuai kebutuhan.

---

> Dibuat oleh \[Nama Anda/Tim Anda] • Mei 2025
