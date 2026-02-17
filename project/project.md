# 🛠️ Project Master Plan: "Hybrid-Sense" Sign Language Translator Glove (Version 4.0)

**System Architecture:** Distributed Hub (Wrist-Primary)
**Platform:** LILYGO T-Display S3 AMOLED 1.64
**Sensing Method:** Hybrid (Flex Resistive + Linear Hall Effect Magnetic)
**UI Control:** Touch Display (LVGL) + IMU Tap Detection
**Status:** Hardware Migration - AMOLED Display Integration

---

## 1. 📋 Comprehensive Bill of Materials (BOM)

### **A. Core Electronics & Processing**

* **Microcontroller:** **LILYGO T-Display-S3-AMOLED-1.64**.
    * *MCU:* ESP32-S3 Dual-core 240MHz, 16MB Flash, 8MB PSRAM.
    * *Display:* 1.64" AMOLED, 280×456 pixels, QSPI Interface (Integrated).
    * *Touch:* FT3168 Capacitive Touch Controller (Integrated on I2C bus).
    * *Battery Management:* Built-in LiPo charging circuit.
    * *Battery Voltage Monitoring:* GPIO 4 (ADC1_CH3) - Internal connection.
    * *Power Enable:* GPIO 38 is used as **power button input** (sleep/wake control via external momentary switch).
    * *Antenna:* PCB Antenna + External Antenna Connector (3D Antenna default).
    * *Dimensions:* 42.0×28.0×11.0 mm (including display and components).
    * *Available ADC Pins:* GPIO 1, 2, 3, 5, 18 (5 total ADC-capable pins exposed).
    * *Form Factor:* Compact wrist-mounted controller with integrated display.

* **Multiplexer:** **CD74HC4067 (16-Channel Analog/Digital Multiplexer)**.
    * *Spec:* High-speed CMOS, 2V-6V operation.
    * *Function:* Consolidates 16 analog inputs (5 Flex + up to 11 Hall sensors) into 1 ADC pin.
    * *Reasoning:* Required because the T-Display has only 5 exposed ADC pins, and we need more than 5 analog sensors.
    * *Actual Dimensions:* 40.6×17.9×1.5 mm (breakout board). 40.6×17.9×3.5 mm (with components).

* **IMU (Inertial Measurement Unit):** **MPU-6050 (GY-521 Breakout)**.
    * *Spec:* 3-Axis Gyroscope + 3-Axis Accelerometer.
    * *Function:* Tracks hand orientation (Palm Up/Down), rotation, and tap detection for wake/sleep control.
    * *I2C Address:* 0x68 or 0x69 (configurable via AD0 pin).
    * *Connection:* Shares I2C bus with FT3168 touch controller (GPIO 6/7).
    * *Special Feature:* Double-tap detection for "Tap to Wake" display power management.
    * *Dimensions:* 14.8×11.9×2.3 mm.

### **B. Sensors (The Hybrid Suite)**

* **Flex Sensors:** **SpectraSymbol 2.2" (x5)**.
    * *Spec:* Variable resistance (~25kΩ flat to ~100kΩ bent).
    * *Function:* Measures the analog degree of finger curvature.
    * *Dimensions:* 73.8×6.6×0.4 mm.
    * *With Pins:* 79×6.6×0.6 mm (variants 006 23 & 007 22).

* **Contact Sensors:** **SS49E Linear Hall Effect Sensors (x5)**.
    * *Spec:* **Linear analog output** proportional to magnetic field strength (not binary on/off).
    * *Output Voltage:* ~2.5V at zero field; increases/decreases with magnetic polarity.
    * *Package:* TO-92S (3-pin: VCC, GND, Vout).
    * *Sensitivity:* Can detect field strength and distance; enables "proximity detection" and "contact pressure" estimation.
    * *Calibration Feature:* Use AMOLED screen to display real-time magnetic field strength bars for precise sensor alignment.
    * *Dimensions:* 4.1×3.0×1.5 mm.

* **Triggers:** **Neodymium Magnets (x5)**.
    * *Spec:* N35 or N52 Grade Disc.
    * *Dimensions:* 5 mm (Diameter)×2 mm (Thickness).
    * *Placement:* Glued to opposing contact surfaces (e.g., thumb magnet faces index finger Hall sensor during pinch).

### **C. Audio & Power Systems**

* **Audio Output:** **Google Pixel 7 Pro Loudspeaker Unit**.
    * *Spec:* High-fidelity micro driver, rectangular form factor.
    * *Overall Dimensions:* Approx. 30×21×6.5 mm (Requires custom cavity).
    * *Active Speaker Area:* 22.4×17.3×6.1 mm. 
    * *Full Speaker Area:* 29.46×20.7×6.1 mm.

* **Audio Amplifier:** **MAX98357A I2S Class-D Amp**.
    * *Spec:* 3W Mono, I2S Interface (LRC, BCLK, DIN).
    * *Requirement:* Essential to drive the Pixel speaker; MCU GPIO cannot drive it directly.
    * *Dimensions:* 17.8×18.7×2.9 mm (Breakout board and components).

* **Battery:** **Lithium Polymer (LiPo) Cell**.
    * *Model:* 602040.
    * *Capacity:* 600mAh / 3.7V.
    * *Dimensions:* 40×20×6 mm.
    * *With Tabs:* 42×21×6 mm.
    * *Charging:* Built-in charging circuit on T-Display board (USB-C).
    * *Voltage Monitoring:* GPIO 4 (internal ADC connection) - Display battery percentage on AMOLED.

* **Power Control:** **Momentary Push Button on GPIO 38**.
    * *Type:* Momentary tactile switch (NO - Normally Open), wired to GPIO 38.
    * *Function:* Single power button (like a phone): short press to sleep, press to wake from deep sleep.
    * *Connection:* One side to **GPIO 38**, other side to **GND**. Internal pull-up used.
    * *Wake Source:* `esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, LOW)` for deep sleep wake.
    * *Software Control:* Short press toggles sleep/wake. MPU6050 double-tap can also wake.
    * *Dimensions:* 6×6×5 mm (standard tactile switch).

### **D. Interface & Hardware**

* **Display:** **1.64" AMOLED Display (Integrated with T-Display S3)**.
    * *Spec:* 280×456 pixels, QSPI Interface (CO5300 Controller).
    * *Function:* Provides visual feedback for system status, sensor readings, magnetic field strength visualization, battery status, and LVGL-based GUI.
    * *Interface:* QSPI using GPIO 10-17 (dedicated pins, not available for other use).
    * *Touch Controller:* FT3168 on I2C bus (GPIO 6/7, shared with MPU6050).
    * *Power Consumption:* High-power display; requires "Tap to Wake" power management strategy.
    * *Backlight Control:* GPIO 16 (LCD_EN) - Enable/disable display power.
    * *Dimensions:* Integrated into 42.0×28.0×11.0 mm board footprint.
    * *UI Framework:* **LVGL 8.3.5** (Light and Versatile Graphics Library).

* **Navigation:** **Touch Display + IMU Gestures**.
    * *Primary Input:* Direct touch interaction via FT3168 capacitive touch (integrated).
    * *Secondary Input:* MPU6050 double-tap detection for wake/sleep control.
    * *Removed:* 5-way navigation switch and analog resistor ladder (no longer needed with touch display).

### **F. Wiring & Connectors**

* **Enameled Copper Wire:** 0.3mm (30 AWG) for internal routing.
* **Braided Sleeve:** 8mm Diameter (Black) for the Wrist-to-Hand bridge.
* **JST PH Connector:** 5-pin 1.5mm for detachable Hand-to-Wrist connections.

### **G. Assembly & Fastening Hardware**

* **Heat-Set Inserts:** M2 x 3.5mm (OD) x 5mm (Length) – Brass.
* **Bolts:** M2 x 5mm Philips Flat Head (Countersunk).
* **Material:** Hardboard.


---

## 2. 🏗️ Mechanical Design Architecture

### **Zone 1: The Wrist Command Hub**

The central processing and control center. Houses all high-power components.

**Physical Specifications:**
* **Shell Construction:** Two-part assembly (Main Body + Faceplate) secured via 4x M2 corner bolts with heat-set inserts.
* **Wall Thickness:** 1.2mm nominal with 3mm internal bosses at fastener points.
* **Material:** PETG or PLA+ (Black matte finish recommended).

**External Layout:**
* **Top Face:** 
    - **1.64" AMOLED Touch Display** centered (integrated with T-Display board).
    - Touch interaction replaces physical navigation controls.
    
* **Side Panel (Accessible Location):**
    - **Momentary Push Button** for manual sleep/wake control.
    
* **USB-C Port:**
    - Bottom edge of T-Display board for charging and programming.
    - Cutout required for cable access.

**Internal Component Stacking (Bottom to Top):**
1. **Floor Level:** 600mAh LiPo Battery secured with double-sided foam tape or battery holder.
2. **Mid-Level:** Pixel 7 Pro Speaker (facing downward toward speaker grill) and MAX98357A Amplifier.
3. **Upper Level:** T-Display S3 board mounted horizontally with display facing outward.
4. **Wiring Level:** Harness routing for multiplexer connection and hand hub bridge cable.

**Cooling & Ventilation:**
* Small vent holes on the rear of the case for thermal dissipation (AMOLED generates heat).
* Speaker grill opening (approx 30×21 mm) on the bottom or side of the case.

**Power Management Strategy:**
* **Power Button:** GPIO 38 momentary switch triggers deep sleep.
* **Button Sleep:** Press GPIO 38 button to enter deep sleep.
* **Tap to Wake:** MPU6050 detects double-tap (secondary wake source).

---

### **Zone 2: The Hand Sensor Hub**

The signal aggregation point, mounted on the opisthenar (back of hand).

**Purpose:** Centralizes sensor data and multiplexing to minimize wiring complexity.

**Components:**
* **MPU-6050 IMU:** Centered position for accurate hand orientation tracking (pitch, roll, yaw) and tap detection.
* **CD74HC4067 Multiplexer:** Receives 10 input channels (5 Flex + 5 Hall sensors), outputs 1 consolidated analog signal.

**Physical Design:**
* **Form Factor:** Low-profile rectangular plate (~50×40×8 mm total).
* **Sensor Connectors:** JST connector for braided sleeve from the wrist hub.
* **Mounting:** Adhered to the back of hand using medical-grade adhesive, elastic strap, or sewn into glove backing.

**Connection Port:**
* Accepts **8mm Braided Sleeve** containing the control wires (VCC, GND, SDA, SCL, SIG, S0–S3).

---

### **Zone 3: Finger Assemblies**

Individual sensor modules for each digit.

**Flex Sensor Installation:**
* Mounted along the length of each finger (dorsal or palmar surface).
* Secured using Kapton tape and hot-melt glue for strain relief.
* Enameled wire routed back to the hand hub connector.

**Hall Sensor Installation:**
* Positioned on or near the fingernail/fingertip.
* Aligned perpendicular to expected magnetic field direction.
* Secured with epoxy or hot-melt adhesive.

**Magnet Placement:**
* Glued to the opposing contact surface (e.g., thumb magnet faces index finger Hall sensor during a pinch gesture).
* Orientation: Magnetic pole pointing toward the Hall sensor.

---

## 3. 🔌 Detailed Electronics & Connection Plan (v4.0 - T-Display S3 AMOLED)

### **Critical Hardware Notes**

The LILYGO T-Display S3 AMOLED uses **many pins for the integrated display and touch controller**, leaving limited pins available for external sensors. The following pins are **RESERVED and NOT AVAILABLE**:

* **GPIO 6-17:** Display QSPI interface, Touch I2C, and control signals.
  - GPIO 6, 7: I2C (SCL, SDA) - Shared with FT3168 and can be used for MPU6050.
  - GPIO 8: Touch Reset (TP_RST).
  - GPIO 9: Touch Interrupt (TP_INT).
  - GPIO 10-17: QSPI Display interface and control.

**Available Pins for External Use:**
* **GPIO 1, 2, 3, 5:** ADC-capable (ADC1 channels).
* **GPIO 18:** ADC-capable (ADC2_CH7).
* **GPIO 21, 40, 41, 42, 43, 45, 46, 47, 48:** Digital I/O.
* **GPIO 0:** Boot button (can be used with caution).
* **GPIO 4:** Battery voltage monitoring (internal connection).
* **GPIO 38:** Power button input (momentary switch to GND, internal pull-up).

### **A. The Power Circuit**

**Power Button (GPIO 38):**
* **GPIO 38** → Connected to a momentary tactile switch (other side to GND).
* Short press triggers deep sleep; press again to wake via `esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, LOW)`.
* Internal pull-up resistor used (`INPUT_PULLUP`).

**Battery Management:**
* **Built-in USB-C Charging Circuit** (no external switch needed).
* **GPIO 4** → Battery voltage ADC (internal connection, read-only).
* Display battery percentage on AMOLED screen.

**Sleep/Wake Control:**
* **Momentary Button on GPIO 38** is the sole power control (like a phone button).
* **Wake Sources:**
  - Button press on GPIO 38 (ext0 wakeup).
  - MPU6050 double-tap detection (optional secondary wake).

**Power Code Example:**
```cpp
#define PIN_POWER_BTN 38

void setup() {
  pinMode(LCD_EN, OUTPUT);
  digitalWrite(LCD_EN, HIGH);    // Enable display
  
  // Configure power button
  pinMode(PIN_POWER_BTN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, LOW);
}

void enterDeepSleep() {
  gfx->Display_Brightness(0);
  gfx->displayOff();
  digitalWrite(LCD_EN, LOW);
  esp_deep_sleep_start();
}
```

---

### **B. The Audio Circuit (I2S - Updated Pin Mapping)**

Digital audio from the T-Display to the Pixel 7 Pro speaker via MAX98357A amplifier.

**I2S Pin Assignments (v4.0):**
* **GPIO 40** → **MAX98357A BCLK** (Bit Clock, I2S serial clock).
* **GPIO 41** → **MAX98357A LRCK** (Left/Right Clock, I2S frame sync).
* **GPIO 42** → **MAX98357A DIN** (Data In, I2S serial data).
* **MAX98357A VCC** → **3.3V** (or 5V from USB for higher volume).
* **MAX98357A GND** → **GND**.
* **MAX98357A OUT+/OUT-** → **Pixel 7 Pro Speaker Wires**.

**Technical Notes:**
* GPIOs 40-42 are high-speed digital pins suitable for I2S audio.
* The T-Display has sufficient flash (16MB) for storing audio files.
* Use SPIFFS or LittleFS to store `.wav` files for sign language audio playback.

**Code Example (I2S Configuration):**
```cpp
#include "driver/i2s.h"

#define I2S_BCLK 40
#define I2S_LRC  41
#define I2S_DIN  42

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}
```

---

### **C. The I2C Circuit (IMU & Touch Controller - Shared Bus)**

The T-Display S3 has a built-in I2C bus for the FT3168 touch controller. The MPU-6050 IMU shares this same bus.

**Wiring:**
* **GPIO 7** → **SDA** (Shared: FT3168 Touch + MPU-6050 IMU).
* **GPIO 6** → **SCL** (Shared: FT3168 Touch + MPU-6050 IMU).
* **3.3V** → **MPU-6050 VCC**.
* **GND** → **MPU-6050 GND**.
* **Pull-up Resistors:** Already present on T-Display board (no external resistors needed).

**I2C Addressing:**
* **FT3168 Touch Controller:** 0x38 (fixed, internal to T-Display).
* **MPU-6050 IMU:** 0x68 or 0x69 (configure via AD0 pin).
  - **IMPORTANT:** Set MPU-6050 to address **0x68** (AD0 = LOW) to avoid conflict with touch controller.

**Code Example (I2C Initialization):**
```cpp
#include <Wire.h>
#include <Adafruit_MPU6050.h>

#define IIC_SDA 7
#define IIC_SCL 6

Adafruit_MPU6050 mpu;

void setup() {
  Wire.begin(IIC_SDA, IIC_SCL);
  
  if (!mpu.begin(0x68)) {  // Address 0x68
    Serial.println("MPU6050 not found!");
    while (1) delay(10);
  }
  
  // Configure MPU6050 for tap detection
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionThreshold(1);
  mpu.setMotionDetectionDuration(20);
  mpu.setInterruptPinLatch(true);
  mpu.setInterruptPinPolarity(true);
  mpu.setMotionInterrupt(true);
}
```

**Tap-to-Wake Feature:**
* Configure MPU6050 motion detection interrupt.
* Use GPIO 9 (TP_INT) or another available pin to detect tap events.
* Double-tap triggers display wake (secondary to GPIO 38 button).

---

### **D. The Multiplexer Circuit (Sensor Aggregation)**

Because the T-Display has only **5 exposed ADC pins** (GPIO 1, 2, 3, 5, 18) and we need **10+ analog sensors** (5 Flex + 5 Hall), we use the CD74HC4067 multiplexer.

**Multiplexer (CD74HC4067) Pin Connections:**

| T-Display Pin | MUX Pin | Function | Notes |
|---------------|---------|----------|-------|
| **GPIO 21** | **S0** | Address Bit 0 (LSB) | Digital control |
| **GPIO 47** | **S1** | Address Bit 1 | Digital control |
| **GPIO 48** | **S2** | Address Bit 2 | Digital control |
| **GPIO 45** | **S3** | Address Bit 3 (MSB) | Digital control |
| **GPIO 1** | **SIG** | Analog Input (Common output) | ADC1_CH0 |
| **3.3V** | **VCC** | Power supply | |
| **GND** | **GND** | Ground reference | |
| **GND** | **EN** | Enable (active LOW) | Tie to GND for always-on |

**Multiplexer Channel Assignments:**

| Channel | Component | Signal Type | Connection |
|---------|-----------|-------------|------------|
| **C0** | Thumb Flex Sensor | Analog | Voltage Divider (Flex + 10kΩ pull-down) |
| **C1** | Index Flex Sensor | Analog | Voltage Divider |
| **C2** | Middle Flex Sensor | Analog | Voltage Divider |
| **C3** | Ring Flex Sensor | Analog | Voltage Divider |
| **C4** | Pinky Flex Sensor | Analog | Voltage Divider |
| **C5** | Thumb Hall Sensor (SS49E) | Analog | Vout (linear output) |
| **C6** | Index Hall Sensor (SS49E) | Analog | Vout |
| **C7** | Middle Hall Sensor (SS49E) | Analog | Vout |
| **C8** | Ring Hall Sensor (SS49E) | Analog | Vout |
| **C9** | Pinky Hall Sensor (SS49E) | Analog | Vout |
| **C10-C15** | RESERVED | — | Future expansion |

**Code Example (Multiplexer Control):**
```cpp
#define MUX_S0  21
#define MUX_S1  47
#define MUX_S2  48
#define MUX_S3  45
#define MUX_SIG 1   // ADC pin

void selectMuxChannel(int channel) {
  digitalWrite(MUX_S0, (channel >> 0) & 0x01);
  digitalWrite(MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3, (channel >> 3) & 0x01);
  delayMicroseconds(100); // Settling time
}

int readMuxChannel(int channel) {
  selectMuxChannel(channel);
  return analogRead(MUX_SIG);
}

void readAllSensors() {
  for (int i = 0; i < 10; i++) {
    int value = readMuxChannel(i);
    Serial.printf("Channel %d: %d\n", i, value);
  }
}
```

---

### **E. The Bridge Connection (Wrist to Hand Hub)**

The control cable that carries all command and sensor signals between the wrist hub and hand hub.

**Required Wires (inside 8mm Braided Sleeve):**

1. **3.3V** (Power) – Red
2. **GND** (Ground) – Black
3. **SDA** (I2C Data for MPU6050) – Yellow
4. **SCL** (I2C Clock for MPU6050) – Blue
5. **SIG** (Mux Output) – Green
6. **S0** (Mux Address Bit 0) – Purple
7. **S1** (Mux Address Bit 1) – Orange
8. **S2** (Mux Address Bit 2) – Brown
9. **S3** (Mux Address Bit 3) – Gray

**Total Length:** Approximately 15–20 cm (wrist to hand).

**Connector:** JST PH 2.0mm 10-pin connector (for easy detachment).

---

### **F. Master Wiring Harness (T-Display S3 AMOLED Pin Configuration v4.0)**

| GPIO Pin | Connected Device | Function | Circuit Type | Notes |
|----------|------------------|----------|--------------|-------|
| **1** | CD74HC4067 | **Mux SIG** (Analog Input) | ADC1_CH0 | Main sensor reading pin |
| **4** | Battery Monitor | **Battery Voltage ADC** | ADC1_CH3 | Internal connection (read-only) |
| **6** | FT3168 + MPU6050 | **I2C SCL** (Clock Line) | I2C Bus | Shared touch + IMU |
| **7** | FT3168 + MPU6050 | **I2C SDA** (Data Line) | I2C Bus | Shared touch + IMU |
| **8** | FT3168 | **Touch Reset** | Digital GPIO | Internal (do not use) |
| **9** | FT3168 | **Touch Interrupt** | Digital GPIO | Internal (do not use) |
| **10-17** | AMOLED Display | **QSPI Interface** | QSPI Bus | Internal (do not use) |
| **16** | Display | **LCD Enable** | Digital GPIO | Display power control |
| **21** | CD74HC4067 | **Mux S0** (Address Bit 0) | Digital GPIO | Multiplexer control |
| **38** | Power Button | **Sleep/Wake Button** | Digital GPIO | Momentary switch to GND |
| **40** | MAX98357A | **I2S BCLK** (Bit Clock) | I2S Interface | Audio clock |
| **41** | MAX98357A | **I2S LRCK** (Frame Sync) | I2S Interface | Audio sync |
| **42** | MAX98357A | **I2S DIN** (Serial Data) | I2S Interface | Audio data |
| **45** | CD74HC4067 | **Mux S3** (Address Bit 3) | Digital GPIO | Multiplexer control |
| **47** | CD74HC4067 | **Mux S1** (Address Bit 1) | Digital GPIO | Multiplexer control |
| **48** | CD74HC4067 | **Mux S2** (Address Bit 2) | Digital GPIO | Multiplexer control |
| **3.3V** | All Devices | Power Supply | Power Rail | System power |
| **GND** | All Devices | Ground Reference | Ground Rail | System ground |

**Pin Usage Summary:**
- **Reserved (Internal):** GPIO 6-17 (Display, Touch, Control)
- **Multiplexer Control:** GPIO 21, 47, 48, 45 (4 pins)
- **Analog Input:** GPIO 1 (1 pin via multiplexer)
- **I2S Audio:** GPIO 40, 41, 42 (3 pins)
- **Power Button:** GPIO 38 (1 pin - sleep/wake)
- **Battery Monitor:** GPIO 4 (1 pin - internal)
- **Available for Future Use:** GPIO 0, 2, 3, 5, 18, 43, 46 (7 pins)

---

## 4. ⚙️ Assembly & Fabrication Strategy

### **Phase 1: Preparation & Sub-Assemblies**

#### **Step 1A: Flex Sensor Preparation**

1. Cut 5 lengths of 0.3mm enameled copper wire (~8 cm each).
2. Strip and tin the ends (0.5 cm) of each wire using a soldering iron.
3. Solder one wire to each Flex Sensor tab (use minimal heat; plastic tabs melt easily).
4. **CRITICAL:** Immediately apply 3mm heat shrink tubing to the joint and heat with a heat gun.
5. Test continuity: Multimeter should read ~25kΩ (flat) to ~100kΩ (bent).

#### **Step 1B: Hall Sensor Preparation**

1. Cut 5 lengths of 0.3mm enameled wire (~8 cm each).
2. Strip the ends and tin.
3. Solder to the Hall Sensor output pin (Vout pin, middle pin on TO-92 package).
4. Apply heat shrink immediately.
5. Also prepare the **Vcc** and **GND** wires for the Hall sensors (routed back to the hand hub).

#### **Step 1C: Magnet Attachment**

1. Glue the 5×2 mm neodymium magnets to the designated contact surfaces (fingertips or opposite fingers).
2. Use epoxy or super glue; allow 24 hours to cure.
3. Verify magnetic field orientation using a compass or Hall sensor.

#### **Step 1D: Build the Bridge Cable**

1. Cut a 8mm braided sleeve to ~18 cm length.
2. Prepare 9 enameled wires (0.3mm), each ~20 cm long, with color coding (see section 3.G).
3. Bundle the wires and thread them through the braided sleeve.
4. Strip the enameled coating from both ends of each wire (use a lighter or fine sandpaper).
5. Tin all 18 wire ends (9 ends at each terminus).
6. Label each wire end with tape for identification during assembly.

---

### **Phase 2: Wrist Hub Assembly**

#### **Step 2A: Insert Heat-Set Inserts**

1. Preheat a soldering iron to 230°C (not too hot; brass inserts can be damaged).
2. Carefully press the M2×3.5 brass inserts into the 4 corner posts inside the 3D-printed wrist hub.
3. Hold for 3–5 seconds until the insert is flush with the internal surface.
4. Allow to cool before handling.

#### **Step 2B: Install Internal Components**

1. **Battery:** Secure the 600mAh LiPo cell to the floor of the case using double-sided foam tape or battery holder. Ensure the polarity markings are visible and JST connector is accessible.
2. **Speaker:** Glue the Pixel 7 Pro speaker to the mid-level mounting cavity, facing downward toward the speaker grill opening.
3. **Amplifier:** Mount the MAX98357A breakout board adjacent to the speaker, securing with hot-melt glue or epoxy.

#### **Step 2C: Mount the T-Display S3 Board**

1. Position the T-Display S3 AMOLED board horizontally with the display facing outward through the case opening.
2. Ensure the USB-C port on the bottom edge is aligned with the charging cutout.
3. Secure the board using mounting bosses or hot-melt glue (avoid blocking the antenna area).
4. **CRITICAL:** The board must be accessible for programming and the display must be visible.

#### **Step 2D: Wire the Power System**

1. Connect the LiPo battery JST connector to the T-Display's battery connector (if available) or solder directly to BAT+/BAT- pads.
2. **No external power switch needed** - the board has built-in charging circuitry.
3. Add a momentary push button:
   - Connect one side to **GPIO 38**.
   - Connect the other side to **GND**.
   - This button will trigger sleep/wake functions (like a phone power button).

#### **Step 2E: Wire the Audio Amplifier**

1. Connect MAX98357A to T-Display:
   - **BCLK** → **GPIO 40**
   - **LRCK** → **GPIO 41**
   - **DIN** → **GPIO 42**
   - **VCC** → **3.3V** (or 5V from USB header if available)
   - **GND** → **GND**
2. Connect amplifier output to Pixel 7 Pro speaker wires.
3. Route wires neatly to avoid interference with display flex cable.

#### **Step 2F: Connect Bridge Cable to T-Display**

1. Route the 9-wire bridge cable from the hand hub to the wrist hub.
2. Connect bridge wires to T-Display pins:
   - **3.3V** (Red) → **3.3V** header pin
   - **GND** (Black) → **GND** header pin
   - **SDA** (Yellow) → **GPIO 7** (shared I2C bus)
   - **SCL** (Blue) → **GPIO 6** (shared I2C bus)
   - **SIG** (Green) → **GPIO 1** (ADC input)
   - **S0** (Purple) → **GPIO 21**
   - **S1** (Orange) → **GPIO 47**
   - **S2** (Brown) → **GPIO 48**
   - **S3** (Gray) → **GPIO 45**

3. Use pin headers or direct soldering depending on T-Display board design.
4. Secure wires with kapton tape or cable management clips.

#### **Step 2G: Initial Power-On Test**

1. **CRITICAL:** Before connecting battery, upload test firmware that enables the display.
2. Connect USB-C cable for programming.
3. Upload minimal test code:
   ```cpp
   #include "pin_config.h"
   #define PIN_POWER_BTN 38
   void setup() {
     pinMode(LCD_EN, OUTPUT);
     digitalWrite(LCD_EN, HIGH);  // Enable display
     pinMode(PIN_POWER_BTN, INPUT_PULLUP); // Power button
     Serial.begin(115200);
     Serial.println("T-Display Ready!");
   }
   void loop() { delay(1000); }
   ```
4. After successful upload, connect battery and verify display powers on.
5. Double-check all solder joints and voltage levels with multimeter.

---

### **Phase 3: Hand Hub Assembly**

#### **Step 3A: Mount the Multiplexer**

1. Solder the CD74HC4067 breakout board to the hand hub PCB or use pin headers for modular assembly.
2. Wire the address lines (S0–S3) to the bridge cable wires (Purple, Orange, Brown, Gray).
3. Wire the common signal output (SIG) to the bridge cable (Green wire).
4. Connect VCC to 3.3V (Red wire) and GND to ground (Black wire).
5. Tie the EN (Enable) pin to GND for always-on operation.

#### **Step 3B: Mount the MPU-6050 IMU**

1. Position the MPU-6050 IMU module centrally on the hand hub for accurate motion detection.
2. Connect to the shared I2C bus from the bridge cable:
   - **SDA** → Bridge Yellow wire (connects to T-Display GPIO 7)
   - **SCL** → Bridge Blue wire (connects to T-Display GPIO 6)
   - **VCC** → Bridge Red wire (3.3V)
   - **GND** → Bridge Black wire
3. **Configure MPU-6050 Address:** Ensure AD0 pin is connected to GND for address 0x68 (avoids conflict with FT3168 touch at 0x38).
4. Secure the MPU-6050 with hot-melt glue or mounting screws.

#### **Step 3C: Connect Sensor Inputs to Multiplexer**

For each of the 5 fingers:

1. **Flex Sensor Wiring:**
   - Create voltage dividers: Connect one end of flex sensor to 3.3V, other end to corresponding Mux channel AND a 10kΩ pull-down resistor to GND.
   - Channels: C0=Thumb, C1=Index, C2=Middle, C3=Ring, C4=Pinky

2. **Hall Sensor Wiring (SS49E Linear):**
   - Connect **VCC** pin to 3.3V rail
   - Connect **GND** pin to ground rail
   - Connect **Vout** (middle pin) to corresponding Mux channel
   - Channels: C5=Thumb, C6=Index, C7=Middle, C8=Ring, C9=Pinky

3. **Calibration Tip:** Use the AMOLED display to show real-time Hall sensor readings as bar graphs. This helps with magnet alignment and distance calibration.

#### **Step 3D: Magnet Proximity Testing**

1. Before final assembly, test each Hall sensor with its corresponding magnet.
2. The SS49E outputs ~2.5V at zero magnetic field.
3. As magnet approaches, voltage should increase or decrease (depending on polarity).
4. Use the T-Display to visualize magnetic field strength in real-time.
5. Adjust magnet positions if needed for optimal detection range.

#### **Step 3E: Finalize Hand Hub**

1. Apply hot-melt glue to strain-relief the enameled wires entering the hub.
2. Seal the JST connector port with a rubber gasket or cap to prevent dust ingress.
3. Test all sensor inputs using a multimeter before final closure.
4. **Testing Checklist:**
   - Flex sensors: ~25kΩ flat, ~100kΩ bent
   - Hall sensors: ~2.5V at zero field, voltage change with magnet proximity
   - I2C communication: MPU6050 responds at address 0x68
   - Multiplexer: All 10 channels read valid ADC values

---

### **Phase 4: Finger & Glove Assembly**

#### **Step 4A: Flex Sensor Installation**

1. Cut kapton tape strips to secure each flex sensor along the dorsal (back) surface of each finger.
2. Align the sensor with the finger's length and tape it in place using 2–3 strips.
3. Route the enameled wire along the finger, secured with additional kapton tape or heat shrink.
4. Ensure the wire is not pinched or kinked where it exits the finger.

#### **Step 4B: Hall Sensor Installation**

1. Mount the Hall sensor on the fingertip or nail, oriented perpendicular to the expected magnet approach.
2. Secure with epoxy or hot-melt glue.
3. Route the output wire (and power wires if not shared) back to the hand hub.

#### **Step 4C: Magnet Mounting Verification**

1. Test the magnetic field using a Hall sensor and a multimeter.
2. Adjust magnet orientation if the field is too weak or misaligned.

#### **Step 4D: Bundle Wires**

1. Gather all 5 flex sensor wires and 5 Hall sensor wires.
2. Group them into the 8mm braided sleeve along with the bridge cable.
3. Secure the bundle at the hand hub connector with a cable tie or heat shrink.

---

### **Phase 5: Firmware & Calibration**

#### **Step 5A: Load Bootloader & Libraries**

1. Install PlatformIO or Arduino IDE with ESP32-S3 board support.
2. Install required libraries:
   - **Wire.h** (I2C, pre-installed)
   - **Adafruit_MPU6050.h** (IMU motion and tap detection)
   - **lvgl.h** (LVGL 8.3.5 - GUI framework)
   - **Arduino_GFX_Library.h** (Display driver for AMOLED)
   - **Arduino_DriveBus_Library.h** (I2C touch controller)
   - **ESP32 I2S Audio** (for audio playback)
   - **LittleFS** or **SPIFFS** (file system for audio storage)

3. Reference the LVGL example at `/examples/Lvgl/Lvgl.cpp` for display initialization.

#### **Step 5B: Multiplexer Control Code (Updated for T-Display)**

```cpp
// T-Display S3 AMOLED Pin Definitions
#define MUX_S0  21  // Address Bit 0
#define MUX_S1  47  // Address Bit 1
#define MUX_S2  48  // Address Bit 2
#define MUX_S3  45  // Address Bit 3
#define MUX_SIG 1   // ADC1_CH0

void setup() {
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);  // CRITICAL: Enable system power
  
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
}

void selectMuxChannel(int channel) {
  digitalWrite(MUX_S0, (channel >> 0) & 0x01);
  digitalWrite(MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3, (channel >> 3) & 0x01);
  delayMicroseconds(100); // Settling time
}

int readMuxChannel(int channel) {
  selectMuxChannel(channel);
  return analogRead(MUX_SIG);
}
```

#### **Step 5C: LVGL Display Initialization**

Use the provided LVGL example as a template:

```cpp
#include "lvgl.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"

// Initialize display (see examples/Lvgl/Lvgl.cpp for complete setup)
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RST, 
    0 /* rotation */, false /* IPS */, LCD_WIDTH, LCD_HEIGHT);

// LVGL display buffer and driver setup
// Create UI screens for:
// - Sensor visualization (flex + hall readings)
// - Magnetic field strength bars
// - Battery percentage
// - Gesture recognition status
```

#### **Step 5D: Sensor Calibration with AMOLED Visualization**

1. Create an LVGL screen showing real-time sensor data:
   - 5 flex sensor bars (vertical, showing bend degree)
   - 5 Hall sensor bars (horizontal, showing magnetic field strength)
   - Battery voltage indicator
   - MPU6050 orientation display

2. **Hall Sensor Calibration:**
   - Baseline reading: ~2.5V (no magnet, ADC ~2048)
   - Contact threshold: Define based on testing (e.g., >3.0V or <2.0V depending on magnet polarity)
   - Display magnetic field strength in real-time for precise alignment

3. **Flex Sensor Calibration:**
   - Flat finger: ~25kΩ resistance (ADC value depends on voltage divider)
   - Fully bent: ~100kΩ resistance
   - Store min/max values in NVS (Non-Volatile Storage)

#### **Step 5E: Power Management with Tap-to-Wake**

```cpp
#include <Adafruit_MPU6050.h>

#define PIN_POWER_BTN 38

Adafruit_MPU6050 mpu;

void setupTapDetection() {
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionThreshold(2);
  mpu.setMotionDetectionDuration(20);
  mpu.setInterruptPinLatch(true);
  mpu.setMotionInterrupt(true);
}

void enterSleep() {
  gfx->Display_Brightness(0);
  gfx->displayOff();
  digitalWrite(LCD_EN, LOW);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, LOW);  // Power button wake
  esp_deep_sleep_start();
}
```

#### **Step 5F: Audio Integration**

1. Store `.wav` files in LittleFS (16MB flash provides ample space):
   ```cpp
   #include <LittleFS.h>
   #include "driver/i2s.h"
   
   void playAudio(const char* filename) {
     File audioFile = LittleFS.open(filename, "r");
     // Stream audio data to I2S
     // Use I2S_NUM_0 with GPIO 40, 41, 42
   }
   ```

2. Map gestures to audio files based on sign language recognition.

---

## 5. 🎯 Software Architecture (Firmware Overview v4.0)

### **Main Loop Structure**

```
Setup:
  1. Enable display (LCD_EN HIGH)
  2. Configure GPIO 38 as power button input
  3. Initialize LVGL and AMOLED display
  3. Initialize I2C bus (FT3168 touch + MPU6050 IMU)
  4. Configure multiplexer pins
  5. Initialize I2S audio system
  6. Mount LittleFS filesystem
  7. Load calibration data from NVS
  8. Configure tap-to-wake on MPU6050

Loop:
  1. Check for touch input (LVGL handles this automatically)
  2. Read all 10 Mux channels (C0–C9) for flex & Hall sensors
  3. Query MPU6050 for orientation (pitch, roll, yaw) and tap events
  4. Process hybrid gesture logic:
     - Hall sensor reading indicates magnetic proximity (linear, not binary)
     - Flex sensor reading indicates finger bend degree
     - Combine with IMU orientation for context (palm up/down)
  5. Update LVGL display with:
     - Current sensor readings (visualization bars)
     - Recognized gesture name
     - Battery percentage
     - System status
  6. If gesture matches dictionary → Play audio from LittleFS
  7. Handle power management:
     - Auto-sleep after configurable timeout
     - Wake on GPIO 38 button press or double-tap
  8. Repeat with ~50-100ms cycle time (balance responsiveness vs. power)
```

### **Gesture Recognition Hierarchy**

1. **Linear Hall Sensor Advantage:** The SS49E provides continuous magnetic field strength readings, not just on/off states. This enables:
   - **Proximity Detection:** Detect when fingers are approaching before contact
   - **Pressure Estimation:** Stronger magnetic field = closer proximity (pseudo-pressure sensing)
   - **Partial Contact:** Detect "light touch" vs "firm pinch" based on analog voltage
   - **Calibration Visualization:** Use AMOLED display to show real-time magnetic field strength bars

2. **Contact Priority:** Hall sensor readings (magnetic field strength) take precedence over flex sensors for contact gestures.

3. **Orientation Context:** MPU6050 IMU data disambiguates similar hand shapes:
   - Palm up vs palm down (pitch)
   - Hand rotation (roll)
   - Hand movement (yaw)
   - Tap detection for wake/sleep

4. **Audio Mapping:** Each unique gesture maps to a pre-recorded audio file stored in LittleFS (phoneme, word, phrase).

### **Display UI Features (LVGL-Based)**

Design a multi-screen LVGL interface:

**Screen 1: Sensor Dashboard**
- Real-time flex sensor bars (5 vertical bars)
- Real-time Hall sensor magnetic field strength (5 horizontal bars with color gradient)
- Battery percentage icon
- Current gesture recognized (large text display)

**Screen 2: Calibration Mode**
- Individual sensor calibration controls
- Min/Max value adjustment sliders
- "Save to NVS" button
- Magnetic field visualization (helps align magnets)

**Screen 3: Settings**
- Sleep timeout adjustment
- Audio volume control
- Bluetooth pairing controls
- System information

**Touch Interactions:**
- Swipe left/right to change screens
- Tap to select
- Long-press for context menus

---

## 6. 📐 3D Printing Specifications (Updated for T-Display)

### **Required Prints**

1. **Wrist Hub Main Body** (T-Display S3 mount, battery cavity, speaker cavity, heat-set insert posts).
2. **Wrist Hub Faceplate** (AMOLED display opening 28×42mm, USB-C port cutout, sleep button access).
3. **Hand Hub Base Plate** (MPU-6050 and Multiplexer mounts, JST connector port).
4. **Finger Sleeves** (optional; for aesthetics and sensor protection).

### **Design Considerations for T-Display**

* **Display Opening:** Must accommodate 42.0×28.0×11.0mm T-Display board with flush or slightly recessed mount.
* **Heat Management:** AMOLED displays generate heat; ensure adequate ventilation around the board.
* **Antenna Clearance:** PCB antenna area (rear of board) must not be covered by metal or obstructed.
* **USB-C Access:** Bottom edge cutout for charging and programming cable.
* **Button Access:** Small hole for momentary sleep/wake button (GPIO 0).

### **Print Settings**

* **Material:** PETG or PLA+ (Black matte recommended for professional appearance).
* **Layer Height:** 0.2 mm standard, 0.1 mm for fine details (USB-C cutout, button hole).
* **Infill:** 20% (sufficient strength, minimal weight for wrist comfort).
* **Support:** Required for overhangs (use tree supports for easy removal).
* **Wall Thickness:** Minimum 1.2 mm (critical for structural integrity and durability).

---

## 7. 🧪 Testing & Troubleshooting (v4.0 - T-Display Specific)

### **Pre-Flight Checklist**

- [ ] **GPIO 38 power button** triggers deep sleep and wakes correctly.
- [ ] All solder joints inspected under magnification.
- [ ] Continuity verified on power rails (3.3V, GND, BAT).
- [ ] USB-C charging tested with multimeter (no shorts, correct polarity).
- [ ] AMOLED display shows test pattern (verify QSPI communication).
- [ ] FT3168 touch controller responds to touch input.
- [ ] MPU-6050 responds at I2C address 0x68 (not conflicting with FT3168 at 0x38).
- [ ] All 5 flex sensors read within expected ADC range (flat vs bent).
- [ ] All 5 SS49E Hall sensors show ~2.5V baseline, voltage changes with magnet proximity.
- [ ] Multiplexer correctly switches between all 10 channels.
- [ ] Audio amplifier plays test tone via GPIO 40-42 I2S pins.
- [ ] Battery voltage readable on GPIO 4 (internal ADC).
- [ ] Sleep/wake button on GPIO 38 triggers deep sleep correctly.
- [ ] MPU6050 tap detection functional.

### **Common Issues & Solutions**

| Issue | Cause | Solution |
|-------|-------|----------|
| **Display blank** | LCD_EN not set HIGH | Add `pinMode(LCD_EN, OUTPUT); digitalWrite(LCD_EN, HIGH);` in setup() |
| **Touch not working** | I2C address conflict or wiring | Verify FT3168 at 0x38, MPU6050 at 0x68; check GPIO 6/7 connections |
| **MPU6050 not detected** | I2C address conflict | Set MPU6050 AD0 pin LOW for address 0x68 |
| **Flex sensors unreliable** | Poor solder joint or incorrect divider | Re-solder; verify 10kΩ pull-down resistor |
| **Hall sensors always at 2.5V** | No magnet or wrong polarity | Test with magnet; try flipping magnet orientation |
| **Hall sensor drift** | Temperature or EMI | Add 100nF capacitor near sensor; calibrate regularly |
| **Multiplexer reads wrong** | Insufficient settling time | Increase `delayMicroseconds()` to 150-200µs |
| **Audio distorted** | I2S timing or clipping | Check GPIO 40-42 connections; reduce volume in code |
| **Battery drains fast** | Display always on | Implement tap-to-wake; reduce screen brightness; use light sleep |
| **System won't wake** | Wake source not configured | Enable ext0 wakeup on GPIO 38 or MPU6050 wake-on-motion |

### **Calibration Procedure**

1. **Hall Sensor Magnetic Field Visualization:**
   - Run calibration mode on LVGL display
   - Show real-time bar graph of magnetic field strength for each Hall sensor
   - Position magnets and adjust until contact readings are consistent
   - SS49E advantage: Can see gradual field strength changes, not just on/off

2. **Flex Sensor Range Calibration:**
   - Flatten each finger: record minimum ADC value
   - Fully bend each finger: record maximum ADC value
   - Store values in NVS for runtime normalization

3. **Touch Screen Calibration:**
   - If needed, run FT3168 calibration routine
   - Test all four corners and center of display

---

## 8. 📈 Future Expansions

With C10–C15 channels reserved on the Multiplexer, the following additions are possible:

* **Additional Hall Sensors** (C10-C14): Up to 11 total Hall sensors for more complex gestures.
* **Pressure Sensors** (C10): Detect grasp force for force-sensitive gestures.
* **Temperature Sensor** (C11): Hand thermal feedback or health monitoring.
* **Proximity Sensors** (C12-C13): Detect hand approaching objects.
* **Additional Analog Inputs** (C14-C15): Future sensor expansion or auxiliary controls.

**Bluetooth Connectivity:**
* Implement BLE for wireless communication with smartphone app
* Real-time gesture streaming to external device
* Remote configuration and calibration

**Machine Learning Integration:**
* On-device TensorFlow Lite for gesture recognition
* Train custom sign language models
* Improve accuracy over time with user-specific calibration

---

## 9. 📝 Revision History

| Version | Date | Key Changes |
|---------|------|-------------|
| **v1.0** | TBD | Initial concept (flex sensors only). |
| **v2.0** | TBD | Added Hall sensors, IMU, OLED, dual-hub architecture. |
| **v3.0** | 2026-02-11 | Implemented analog resistor ladder for navigation; optimized I2S audio pins; finalized pin mapping; detailed assembly strategy. |
| **v4.0** | 2026-02-16 | **Major Hardware Migration:** Migrated from Seeed XIAO ESP32-S3 to **LILYGO T-Display S3 AMOLED 1.64**. Removed 5-way navigation switch and resistor ladder (replaced with touch display). Updated to **SS49E linear Hall sensors** for analog magnetic field measurement. Implemented **LVGL 8.3.5** GUI framework. Added magnetic field visualization on AMOLED display. Implemented **Tap-to-Wake** power management with MPU6050. Changed power control from latching switch to momentary button with software sleep control. Updated pin mappings for T-Display GPIO constraints. Added battery monitoring via internal GPIO 4 connection. Enhanced calibration procedures with real-time visual feedback. |

---

**Project Status:** Hardware migration complete; ready for firmware development and testing.

**Next Steps:** 
1. Adapt LVGL example code for sensor visualization UI
2. Implement multiplexer sensor reading with new pin assignments
3. Develop magnetic field strength visualization for Hall sensor calibration
4. Create power management system with tap-to-wake functionality
5. 3D print updated enclosure for T-Display form factor
6. Test and calibrate linear Hall sensors with magnet positioning
7. Implement I2S audio playback with gesture recognition

**Key Advantages of v4.0:**
* **High-resolution AMOLED display** provides superior visual feedback and user interface
* **Integrated touch controller** eliminates need for external navigation hardware
* **Linear Hall sensors (SS49E)** enable proximity detection and "pressure" sensing
* **LVGL framework** allows professional GUI with minimal code complexity
* **Tap-to-Wake feature** maximizes battery life with convenient wake-up
* **Real-time calibration display** simplifies sensor alignment and debugging
* **Larger flash (16MB)** supports more audio files and future ML models

---

*End of Project Master Plan v4.0*
