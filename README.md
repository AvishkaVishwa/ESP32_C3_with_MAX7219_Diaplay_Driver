# ⏰ ESP32‑Powered Web Clock

> Elegant Wi‑Fi digital clock with MAX7219 6‑digit display, buzzer alarm, and full web control.

<p align="center">
  <img src="/assest/final.jpg" width="620" alt="Clock demo"/>
</p>

---

## ✨ Highlights

|                       |                                                                  |
| --------------------- | ---------------------------------------------------------------- |
| **Real‑time display** | Bright 6‑digit 7‑segment driven by MAX7219 (HH\:MM\:SS)          |
| **Responsive web UI** | Set time, alarms, countdown & Wi‑Fi from any browser             |
| **Buzzer & button**   | Loud alarm + physical dismiss (GPIO 0)                           |
| **Dual‑mode Wi‑Fi**   | AP for local control (`Clock` SSID) + STA for internet time sync |
| **SNTP auto‑sync**    | Keeps time accurate to seconds with pool.ntp.org                 |
| **Open hardware**     | KiCad project, 3‑D renders, and BOM included                     |

---

## 🖼️ Gallery

| Web UI                                    | PCB 3‑D                                 | Copper                                  |
| ----------------------------------------- | --------------------------------------- | ------------------------------------------- |
| <img src="/assest/1.png" width="260"> | <img src="/Hardware/3d3.png" width="260"> | <img src="/Hardware/B_CU.png" width="260"> |
                                                                                      
More in [**/assets**](assets) & [**/hardware**](hardware).


<video src ="https://github.com/user-attachments/assets/0d8d9107-081a-4ae7-bd47-be3cfd9f0423"></video>


So when we turned on the clock it takes 10 seconds to connect to the home wifi and connect to the NTP server update the time..

<video src ="https://github.com/user-attachments/assets/e7d40686-c1fc-4405-965b-9b872882c82c"></video>


---

## 🔌 Hardware List

| Qty      | Part                              | Notes                      |
| -------- | --------------------------------- | -------------------------- |
| 1        | **ESP32‑WROOM‑32D** module        | 38‑pin, 4 MB flash         |
| 1        | **MAX7219** 8‑digit driver        | Only digits 0‑5 used       |
| 6        | 0.56 " 7‑segment (common cathode) | HHMMSS                     |
| 1        | Piezo buzzer (3 V)                | GPIO 4                     |
| 1        | Tact switch                       | Dismiss, GPIO 0            |
| 2        | LEDs + 1 kΩ                       | Seconds (G2), AM/PM (G19)  |
| 1        | **LM2596S‑5.0** buck              | 12 V → 5 V, feeds 3 V3 LDO |
| assorted | passives, headers                 | See schematic              |

Schematic & PCB files: **`hardware/`** (KiCad 9).

---

## 🗺️ Wiring / Pin Map

```text
ESP32‑WROOM‑32D     MAX7219 / IO       Notes
─────────────────────────────────────────────────
GPIO23  ─────────── DIN      (SPI MOSI)
GPIO18  ─────────── CLK      (SPI SCK)
GPIO5   ─────────── CS       (SPI SS)
GPIO4   ─────────── Buzzer   Active‑high
GPIO0   ─────────── Button   Pulled‑up, boot mode when held
GPIO2   ─────────── Seconds‑LED
GPIO19  ─────────── AM/PM‑LED
3V3/5V  ─────────── VCC      MAX7219 tolerant
GND     ─────────── GND
```

---

## 🚀 Quick Start (Firmware)

```bash
# 1 · Clone and select target
$ git clone https://github.com/AvishkaVishwa/esp32-c3-clock.git
$ cd esp32-c3-clock/firmware
$ idf.py set-target esp32

# 2 · Install submodules & configure
$ git submodule update --init
$ idf.py menuconfig   # Wi‑Fi, timezone, pins, etc.

# 3 · Build, flash & monitor
$ idf.py build flash monitor
```

First boot ➡ creates open AP `Clock` (pwd **clockpass**). Browse to **[http://192.168.4.1](http://192.168.4.1)** to set local time & Wi‑Fi.

> **Tip:** Once connected to your home network the clock pulls NTP every hour.

---

## 🔧 Advanced Options

| Menu                                         | Default              | Description                        |
| -------------------------------------------- | -------------------- | ---------------------------------- |
| `Clock ▸ Timezone`                           | Asia/Colombo (+5:30) | Any UTC offset, 30 min granularity |
| `Clock ▸ Alarms ▸ Alarm 1`                   | 07:00                | Daily repeat                       |
| `Clock ▸ Wi‑Fi ▸ AP SSID`                    | Clock                | Rename if multiple clocks          |
| `Component ▸ HTTP Server ▸ Max URI handlers` | 15                   | Reduce to save RAM                 |

---

## 🛠️ Factory Test Routine

```bash
idf.py -DMODE=TEST flash monitor
```

Runs segment sweep, buzzer chirp, Wi‑Fi scan, and prints results in JSON—ideal before boxing.

---

## 🎉 Special Thanks to PCBWay


<div align="center">
  <img src="/assest/1.jpg" width="260">  | <img src="/assest/2.jpg" width="260"> 
</div>

<p align="center">
  <a href="https://www.pcbway.com/" target="_blank">
    <img src="https://github.com/AvishkaVishwa/12V-DC-Motor-Speed-Controller-PCB-Design-using-KiCAD/blob/0191b6e02eeb30e176867d2a93ebec854536829a/Images/pcbwaylogo.jpg" alt="PCBWay" width="200"/>
  </a>
</p>

I would like to give a huge shoutout and sincere thanks to **[PCBWay](https://www.pcbway.com/)** for sponsoring the PCB fabrication of this project!

The **build quality, silkscreen clarity, via precision, and copper finish** exceeded expectations. PCBWay’s service was fast, professional, and extremely helpful throughout the production process.

This project wouldn’t have been possible without their generous support. If you’re looking to manufacture professional-grade PCBs at an affordable price, I highly recommend checking them out.

🔗 [Visit PCBWay →](https://www.pcbway.com/)

---

## 📜 License

Released under the **MIT License** – see [`LICENSE`](LICENSE).

---

> © 2025 Avishka Vishwa   •   Made with ☕ & 🕑
