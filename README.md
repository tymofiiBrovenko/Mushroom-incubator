# LaTeX-PAD: Custom Macropad for Math & Science (RP2040)

<img width="1123" height="675" alt="image" src="https://github.com/user-attachments/assets/1a03d40f-935c-4bb1-b577-e74c94a8ec6c" />


This is a custom 15-key macropad with a rotary encoder. I built it mainly to speed up writing math formulas in LaTeX. It runs on a Seeeduino XIAO RP2040 using custom QMK firmware.

I designed this project to make my life easier when typing complex equations. It combines a regular numpad with some really handy LaTeX macros.

## Key Features

* **15 Cherry MX-style keys** wired in a 4x4 ortholinear matrix.
* **1 Rotary Encoder (EC11)** with a push-button switch.
* **Smart Cursor Navigation:** The macros do the boring work for you. If you paste a command like `\frac{}{}`, the cursor automatically jumps back inside the first set of brackets so you can start typing right away.
* **Seeeduino XIAO RP2040:** Has more than enough memory and pins for complex QMK setups.

## Hardware & Enclosure

### Case Design
I designed the enclosure from scratch in Fusion 360 to securely hold the PCB, switches, and the microcontroller together. 
* *Optional: The case is 3D printed in [PLA/PETG] and held together with [M3 screws].*

<div align="center">
  <img width="45%" alt="CAD render 1" src="https://github.com/user-attachments/assets/d0e816e2-e972-458c-8174-80056514a3af" />
  <img width="45%" alt="CAD render 2" src="https://github.com/user-attachments/assets/eee5907c-4131-45e6-96ec-015f690bac03" />
" />
</div>

### PCB
<div align="center">
  <img width="45%" alt="PCB Front" src="https://github.com/user-attachments/assets/f0b39f75-fbd8-4a48-8440-c000d1a2b7e7" />
  <img width="45%" alt="PCB Back" src="https://github.com/user-attachments/assets/bc042096-03ba-4135-b111-f0d4b1a4fb25" />
</div>

> **Note:** The footprint library in the schematic might show SAMD21 pinouts (D0, D1, etc.). Just ignore that — the actual hardware uses the XIAO RP2040, and the QMK firmware is mapped to the correct RP2040 pins (GP26, GP6, etc.).

### Logik
<div align="center">
  <img width="485" height="556" alt="image" src="https://github.com/user-attachments/assets/0eec56d2-98dc-463a-a6ea-8a6f423a10ee" />
</div>

## Firmware Details

The macropad runs on QMK Firmware to handle matrix scanning, the encoder, and all the custom macros.

### Keymap Layers
* **Base Layer (`_NUMPAD`):** Works just like a standard numpad. It also has a dedicated layer-modifier key.
* **LaTeX Layer (`_LATEX`):** Hold the modifier key to access 14 of the most common LaTeX commands (`\frac{}{}`, `\sqrt{}`, `\sum_{}^{}`, `\int_{}^{}`, etc.).

### Rotary Encoder
* **Rotate Left/Right:** Moves the text cursor left or right. It's really useful for tweaking long equations without touching your mouse.
* **Press:** Acts as a standard Left Mouse Button click.

## Bill of Materials (BOM)

If you want to build one yourself, here's what you need:
* 1x Seeeduino XIAO RP2040
* 15x Cherry MX-compatible switches (e.g., Gateron Red)
* 1x EC11 Rotary Encoder
* 15x 1N4148 Diodes
* Custom PCB (Gerber files are in the `hardware/` folder)
* 3D Printed case parts

## How to Build & Flash

1. **Hardware:** Get the PCB fabricated using the Gerber files from the `hardware/` folder. Solder the diodes, switches, encoder, and the XIAO RP2040.
2. **Firmware:** * Set up your local QMK environment.
   * Drop the firmware folder from this repo into your `qmk_firmware/keyboards/` directory.
   * Compile and flash the board (e.g., run `qmk flash -kb latex_pad -km default`).
