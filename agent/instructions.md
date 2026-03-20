# Antigravity: AI Morse Code (CW) Trainer Reference
### Hardware: ESP32 Audio Kit v2.2 (A1S / A404)

This document serves as the technical "Source of Truth" for the Antigravity project. It defines the hardware mapping, signal flow, and strict architectural standards for the AI-enhanced Morse Code Trainer.

---

## 1. Board Specifications & Pin Mapping

The **ESP32-A1S** module uses an integrated **AC101** (or ES8388) Audio Codec. All C-code drivers must adhere to these GPIO assignments.

### Core Infrastructure & Audio Bus
| Component       | Interface / GPIO                     | Description                                   |
| :-------------- | :----------------------------------- | :-------------------------------------------- |
| **Codec I2C**   | SDA: `IO33`, SCL: `IO32`             | Control bus for volume and routing.           |
| **Audio I2S**   | BCLK: `IO27`, LRCK: `IO25`, DOUT: `IO26`, DIN: `IO35` | 16-bit PCM stream.             |
| **PA Enable**   | `IO21`                               | **HIGH** to enable the onboard Speaker Amp (Active High). |
| **HP Detect**   | `IO39`                               | **Headphones Detect** (Input only).           |
| **SD Detect**   | `IO34`                               | **SD Card Detect** (Input only).              |
| **PSRAM**       | Integrated (4MB)                     | Mandatory for DSP buffers and AI weights.     |

### User Interface (Tactile Buttons & LEDs)
| Button/LED | GPIO      | Antigravity Logic Function          | Notes                         |
| :--------- | :-------- | :---------------------------------- | :---------------------------- |
| **KEY 1**  | `IO36`    | **Toggle Session**: Start/Stop.     | SENSOR VP (Input only)        |
| **KEY 2**  | `IO13`    | **WPM+**: Increase Morse speed.     | MTCK / S1 on                  |
| **KEY 3**  | `IO19`    | **WPM-**: Decrease Morse speed.     | Shared with LED5 (red)        |
| **KEY 4**  | `IO23`    | **AI Mode**: Adaptive toggle.       |                               |
| **KEY 5**  | `IO18`    | **Pitch**: Cycle tone (400-900Hz).  |                               |
| **KEY 6**  | `IO5`     | **Manual Key**: External Input.     |                               |
| **LED 4**  | `IO14`    | Status Indicator (Red)              | MTMS                          |
| **LED 5**  | `IO19`    | Status Indicator (Red)              | Shared with KEY3              |

### Boot Strapping & SD Card
- **GPIO 0**: Must be **HIGH** at boot.
- **GPIO 2**: Must be **HIGH** at boot (SD_MMC DATA0 / SD_SPI MISO).
- **GPIO 12**: Must be **LOW** at boot (MTDI).
- **SD_MMC**: CLK(14), CMD(15), D0(2), D1(4), D2(12), D3(13).
- **SD_SPI**: SCK(14), MOSI(15), MISO(2), CS(13).

---

## 2. Sound Flow & Signal Path

### CW Side-tone Generation
- **DDS Synthesis**: Generated via I2S DMA for ultra-low latency (<20ms).
- **Envelope Shaping**: Implement a **5ms Raised Cosine** attack/decay curve. 
    - *ELI5: We soften the start/end of each beep so it doesn't "click" like a broken switch.*

### AI/DSP Decoder
- **Input Source**: `AUX IN` or Onboard Mic.
- **Goertzel Filter**: Targeted frequency detection with adaptive noise floor tracking.
- **Farnsworth Timing**: AI logic calculates WPM by measuring the ratio between Dits, Dahs, and space intervals.

---

## 3. Principal Developer Guidelines (Architectural Rules)

### 🏛 Modular Design & Readability
- **Encapsulation**: One peripheral = One module (e.g., `cw_gen.c`, `ac101_drv.c`). No "God-objects".
- **ELI5 Policy**: Comment complex math (FFT, AI) as "for beginners" with clear examples.
- **Event-Driven**: Use **FreeRTOS Event Groups** and **Queues**. Strictly avoid `vTaskDelay()` in signal-critical loops.

### 🔋 Power & Performance Optimization
- **Peripheral Gating**: Disable `PA_EN` (`IO21`) and Codec power when audio is idle.
- **Dynamic Clocking**: Run MCU at **80MHz** for Morse logic; boost to **240MHz** only for heavy AI/FFT or Wi-Fi bursts.
- **MCU Sleep**: Enable `esp_pm_configure` for automatic Light Sleep during RX-wait states.

### 🧹 Software Hygiene (Code Pruning)
- **Zero Dead Code**: Audit and **remove** any unused variables, legacy functions, or boilerplate from manufacturer examples.
- **Refactoring**: If an FSM (State Machine) exceeds 3 levels of nesting, refactor it into a structured lookup table.
- **Type Safety**: Always use `PRIu32` for logging `uint32_t` to maintain ESP-IDF compatibility.

## 5. Workflow & Build Commands

Use these commands for local development (configured in `.vscode/tasks.json`):

- **Build**: `source ~/esp/v5.4.1/esp-idf/export.sh && idf.py build`
- **Flash**: `source ~/esp/v5.4.1/esp-idf/export.sh && idf.py flash`
- **Monitor**: `source ~/esp/v5.4.1/esp-idf/export.sh && idf.py monitor`
- **Full Cycle**: `idf.py flash monitor` (once exported)

---

## 4. Known Constraints & Warnings
- **Codec Sensitivity**: AC101 requires precise I2C sequencing. Verify all commands with `ESP_ERROR_CHECK`.
- **EMI/Noise**: 2.4GHz Wi-Fi "chirps" can leak into the audio path. Disable Wi-Fi during sensitive decoding.
- **Input Voltage**: Low battery (<3.5V) may cause audio distortion. Monitor battery via PMU/ADC.