# ESP32 WROOM-32D Clock Wiring Guide

## Component Connections

### MAX7219 7-Segment Display Module
```
MAX7219 Pin  →  ESP32 Pin
VCC          →  3.3V or 5V (depending on your module)
GND          →  GND
DIN (Data)   →  GPIO23 (MOSI)
CS (Load)    →  GPIO5
CLK (Clock)  →  GPIO18 (SCK)
```

### Buzzer (Active or Passive)
```
Buzzer Pin   →  ESP32 Pin
VCC/+        →  GPIO4
GND/-        →  GND
```

### LEDs
```
Component       →  ESP32 Pin
Seconds LED     →  GPIO2 (built-in LED on most boards)
AM/PM LED       →  GPIO19 → LED → 220Ω resistor → GND
```

### Button (Optional - GPIO0 BOOT button can be used)
```
External Button →  ESP32 Pin
One terminal    →  GPIO0 (or use built-in BOOT button)
Other terminal  →  GND
```

## Power Supply
- ESP32 WROOM-32D: 3.3V (regulated internally from 5V USB or Vin)
- MAX7219: Can work with 3.3V or 5V (check your module specifications)
- Total current: ~200-500mA depending on display brightness

## Important Notes

### GPIO Constraints for ESP32 WROOM-32D:
- **GPIO0**: Boot button (pulled up, goes LOW when pressed)
- **GPIO2**: Built-in LED on many boards, can be used as output
- **GPIO6-11**: Connected to flash memory - DO NOT USE
- **GPIO12**: Boot configuration pin - avoid if possible
- **GPIO15**: Boot configuration pin - avoid if possible

### Safe GPIO pins for ESP32 WROOM-32D:
- **Input/Output**: 4, 5, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
- **Input Only**: 34, 35, 36, 39 (no internal pull-up)

### SPI Pins (HSPI):
- **MOSI**: GPIO23
- **MISO**: GPIO19 (not used in this project)
- **SCK**: GPIO18
- **CS**: Any GPIO (we use GPIO5)

## Testing Steps

1. **Power on ESP32** - Check if built-in LED blinks
2. **Connect Serial Monitor** - Look for boot messages
3. **Test Display** - Should see segment test pattern on startup
4. **Test WiFi** - Look for "Clock" WiFi network
5. **Test Web Interface** - Connect to 192.168.4.1
6. **Test Hardware** - Check buttons, LEDs, buzzer, display

## Troubleshooting

### Display not working:
- Check 3.3V vs 5V requirements of your MAX7219 module
- Verify SPI connections (DIN=GPIO23, CLK=GPIO18, CS=GPIO5)
- Try lower SPI clock speed (already set to 5MHz)
- Check GND connection

### WiFi issues:
- Look for "Clock" network with password "clockpass"
- Check serial output for connection messages
- Try factory reset (hold GPIO0 while powering on)

### Button not responding:
- GPIO0 (BOOT button) should work immediately
- External button needs debouncing (already implemented)
- Check GND connection for external buttons

### Buzzer silent:
- Check if it's active or passive buzzer
- Active buzzer: Just needs DC voltage
- Passive buzzer: Needs square wave (current code works for both)
- Check GPIO4 connection
