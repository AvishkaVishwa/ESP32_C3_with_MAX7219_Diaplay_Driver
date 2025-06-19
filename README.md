
---

## 📱 ESP32-C3 Digital Clock with Web Interface and MAX7219 Display Driver

This project is a **web-controlled digital clock** using an **ESP32-C3**, a **MAX7219**-driven 6-digit 7-segment display, and a buzzer alarm. It includes:

✅ Real-time clock display
✅ Web interface to set time and alarms
✅ Dismiss alarm function (via web and hardware button)
✅ Clean, responsive, and minimal web design
✅ Lightweight implementation designed for ESP32-C3

---

### ✨ Features

* **Real-time Clock Display**: Displays HH\:MM\:SS on a 6-digit 7-segment display via MAX7219.
* **Web Interface**:

  * Set current time
  * Set alarm time
  * Dismiss alarm
  * View current time
* **Buzzer Output**: Activates when alarm triggers.
* **Dismiss Button**: Physical button to stop the alarm.
* **Wi-Fi Access Point**: Connect to `ESP32-C3-Clock` to access the web interface.

---

### 🛠️ Hardware Requirements

| Component                 | Description                          |
| ------------------------- | ------------------------------------ |
| **ESP32-C3 Super Mini**   | Wi-Fi microcontroller                |
| **MAX7219 Module**        | Drives the 6-digit 7-segment display |
| **6x 7-segment displays** | Common cathode, single digit         |
| **Buzzer**                | Alarm output (GPIO6)                 |
| **Dismiss Button**        | Momentary push-button (GPIO7)        |
| **Breadboard & Wires**    | Prototyping connections              |

---


## 🖼️ Sneak Peek

| Web UI                                                | PCB Render                                         |
| ----------------------------------------------------- | -------------------------------------------------- |
| <img src="/assest/1.png" width="320">                 | <img src="E:\Clock\clcok\Hardware\3d.png" width="320"> |
                                                      
## 📐 PCB Design Gallery

| View              | Snapshot                                         | Notes                                                                                 |
| ----------------- | -------------------------------------------------- | ------------------------------------------------------------------------------------- |
| **Top copper**    | <img src="/Hardware/zone.png" width="320">    | High‑speed SPI and control lines kept short; ground pour stitched with plenty of vias |
| **Bottom copper** | <img src="/Hardware/B_CU.png" width="320"> | Almost‑solid GND plane with 5 V return path and a few low‑speed signals               |
| **3‑D render**    | <img src="/Hardware/3d3.png" width="320"> | Compact 90 × 30 mm, ESP32‑C3 left, MAX7219 centre, LM2596 buck right                  |

---


## 📌 Pin Map (default firmware)

| ESP32‑C3 Pin | Purpose        | MAX7219 | Notes                |
| ------------ | -------------- | ------- | -------------------- |
| **GPIO2**    | SPI MOSI       | DIN     |                      |
| **GPIO4**    | SPI CLK        | CLK     |                      |
| **GPIO5**    | SPI CS         | CS      | Can be any free GPIO |
| **GPIO6**    | Buzzer         | –       | Active‑high          |
| **GPIO7**    | Dismiss button | –       | Pulled‑up internally |

> ℹ️ All pins are configurable in **`idf.py menuconfig ▸ Clock ▸ GPIO Map`**.

---

## 🗺️ System Block Diagram

```
      ┌────────────┐  SPI  ┌───────────────┐    ┌──────────────┐
      │  ESP32‑C3  ├──────►│   MAX7219     ├────►  6 × 7‑SEG   │
      │            │       └───────────────┘    └──────────────┘
      │  Wi‑Fi AP  │
      │  HTTP srv  │─────┐          ┌─────────┐
      │  OTA srv   │     └─REST/SSE─► Browser │
      │            │                 └─────────┘
      ├────────────┤
      │  Buzzer 6  │◄────────────────────────────── Alarm ISR
      │  Button 7  │─────────────────┐
      └────────────┘                 └ Debounced GPIO
```

---

## 🚀 Quick Start

```bash
# 1 · Clone & init IDF project
$ git clone https://github.com/AvishkaVishwa/esp32-c3-clock.git
$ cd esp32-c3-clock/firmware
$ idf.py set-target esp32c3

# 2 · Configure Wi‑Fi country code / timezone / pins
$ idf.py menuconfig

# 3 · Build, flash & monitor
$ idf.py build flash monitor
```

After first boot the device creates an **open AP** named `ESP32‑C3‑Clock`. Connect, browse to `http://192.168.4.1`, and set the current time & your alarms.

---

## 🛠️ Advanced Configuration

| Setting                | `menuconfig` Path         | Default          |
| ---------------------- | ------------------------- | ---------------- |
| Timezone               | `Clock ▸ Time`            | `Asia/Colombo`   |
| AP SSID                | `Clock ▸ Wi‑Fi`           | `ESP32‑C3‑Clock` |
| Alarm 1                | `Clock ▸ Alarms`          | `07:00`          |
| HTTP max header length | `Component ▸ HTTP Server` | 1024 bytes       |

---

> Want to help? Check out [open issues](https://github.com/AvishkaVishwa/esp32-c3-clock/issues) and start hacking!

---

## 🤝 Contributing

1. Fork & create your branch: `git checkout -b feat/cool‑feature`
2. Commit with **Conventional Commits**.
3. Push & open a PR – GitHub Actions will run lint & build checks.

Even typo fixes are appreciated ✨

---


