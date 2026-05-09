# 🏎️ Porsche Core: Advanced ESP32-C3 Car Controller



[cite_start]An asynchronous, hardware-integrated control system for a Porsche model car, powered by a Seeed Studio XIAO ESP32-C3[cite: 1340]. This project serves as a smart-home bridge, a PC power manager, and a highly advanced cinematic lighting engine.

## ✨ Key Features

### 🖥️ PC & KVM Management
* [cite_start]**PC Ignition & Power Control:** Bypasses software networking to trigger the PC motherboard's physical power pins for Wake, Soft-Shutdown, and Force-Shutdown[cite: 1469, 1470].
* [cite_start]**Background RTOS Network Scanner:** Constantly pings port 135 to detect if the Desktop or Laptop is online, updating the UI telemetry in real-time[cite: 1568, 1569].
* **Seamless USB KVM Handoff:** Fires a precise electrical pulse to swap peripherals. [cite_start]If the Desktop shuts down, the system detects the power loss and automatically drops you onto the Laptop[cite: 1457].

### 🎨 The Mathematical Animation Engine
* **Cinematic Car Themes:** Features an 80-theme smart deck (including *F1 Race Start, Cyber Boot, Alien Abduction, etc.*). [cite_start]A Fisher-Yates shuffle algorithm mathematically guarantees you never see the same theme twice until all 80 have played[cite: 2028, 2033].
* **Highway Run (Physics Simulator):** A virtual physics engine that simulates driving. [cite_start]Calculates speed, gears, and dynamic RPM audio, while randomly generating traffic jams and rainstorms[cite: 2355, 2367, 2380].
* [cite_start]**Jukebox & Mario Mode:** An 8-bit synthesizer with 13 retro tracks and a special Mario Mode that ends in a randomized 50/50 "Victory" or "Death" sequence[cite: 1988, 2348, 2350].
* [cite_start]**Ambient Flow:** Exceptionally smooth, mathematically generated sine-wave breathing animations with Perlin noise, REM sleep flickers, and a circadian rhythm engine[cite: 1948, 1965, 1978, 2409].

### 📱 Premium Web Dashboard
* [cite_start]**Dynamic Day/Night UI:** A frosted-glass interface with live WebSockets to prevent UI lag during complex animations[cite: 1475, 1484].
* [cite_start]**Smart Telemetry Rings:** 3-tier visualizers tracking the Auto-Sleep Shutdown Timer, the 8-Hour Master Idle Limit, and the NTP-synced Hourly Audio Chime[cite: 1435, 1436].
* [cite_start]**Secure Login & Captive Portal:** Protected by a login screen with a 1-year cookie[cite: 1521, 1712]. [cite_start]If network settings fail, holding the physical car hood for 30 seconds broadcasts an emergency `PORSCHE-SETUP` Wi-Fi network for recovery[cite: 1670, 1698].

---

## 🔌 Hardware Connections (Pinout)
The system is built around the Seeed Studio XIAO ESP32-C3.

| ESP32-C3 Pin | GPIO | Connected Component | Function Type |
| :--- | :--- | :--- | :--- |
| **D0** | `GPIO 2` | Front Bonnet (Hood) Switch | [cite_start]Input (Pullup) [cite: 1362, 1702] |
| **D1** | `GPIO 3` | PC Motherboard Relay | [cite_start]Output (Ignition) [cite: 1362, 1704] |
| **D2** | `GPIO 4` | USB KVM Switch Relay | [cite_start]Output (Peripheral Swap) [cite: 1362, 1704] |
| **D3** | `GPIO 5` | Left Headlight | [cite_start]Output (PWM) [cite: 1362, 1702] |
| **D4** | `GPIO 6` | Right Headlight | [cite_start]Output (PWM) [cite: 1362, 1703] |
| **D5** | `GPIO 7` | Left Brake Light | [cite_start]Output (PWM) [cite: 1362, 1703] |
| **D6** | `GPIO 21` | Underglow LEDs | [cite_start]Output (PWM) [cite: 1362, 1703] |
| **D7** | `GPIO 20` | Piezo Buzzer | [cite_start]Output (Tone/Audio) [cite: 1362, 1703] |
| **D8** | `GPIO 8` | Right Door Switch | [cite_start]Input (Pullup) [cite: 1362, 1702] |
| **D9** | `GPIO 9` | Left Door Switch | [cite_start]Input (Pullup) [cite: 1362, 1363, 1702] |
| **D10** | `GPIO 10` | Right Brake Light | [cite_start]Output (PWM) [cite: 1363, 1703] |

---

## 🕹️ Physical Controls (Tactile Inputs)

The car's physical doors and hood act as interactive multi-tap triggers.

### [cite_start]🚘 Front Bonnet (Hood) [cite: 1449]
* **Single Tap:** Toggles the USB KVM Switch.
* **Double Tap:** Master System Wake / Sleep.
* **Hold 3 Seconds:** Toggles Child Lock (Ignores all other physical inputs).
* **Hold 30 Seconds:** Triggers Captive Portal Wi-Fi Setup.

### [cite_start]🚪 Left Door (Passenger) [cite: 1450]
* **Single Tap:** Wakes the Desktop PC (Ignition).
* **Double Tap:** Reboots the Desktop PC.
* **Hold 3 Seconds:** Hard-Shutdowns the Desktop PC.

### [cite_start]🚪 Right Door (Driver) [cite: 1450, 1451]
* **Single Tap:** Toggles Ambient Flow mode.
* **Double Tap:** Triggers Highway Run physics simulator.
* **3 Taps:** Triggers Jukebox mode.
* **4 Taps:** Triggers Cinematic Car Themes.
* **5+ Taps:** Triggers Mario Mode.
* **Hold 3 Seconds:** Deep System Standby (Forces all off).

---

## ⚙️ Software Installation
1. Flash the `thenewporch.ino` code to your XIAO ESP32-C3 using the Arduino IDE.
2. Upon first boot, the car will fail to find a network and trigger the Captive Portal.
3. [cite_start]Connect your phone to the `PORSCHE-SETUP` Wi-Fi network[cite: 1697].
4. [cite_start]Enter your home Wi-Fi credentials, assign your PC IPs, and set your UI password[cite: 1550].
5. The car will reboot, connect to your router, and the dashboard will be live at the assigned IP!
