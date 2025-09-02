# TC375 Lite Kit CAN ↔ UART Demo

## Overview
This project demonstrates **Classic CAN reception** on the Infineon **AURIX™ TC375 Lite Kit** and forwarding the decoded values to a PC terminal via **UART**.  
With a **PEAK PCAN-USB** adapter and **PCAN-View** software, you can transmit CAN frames from the PC. The TC375 receives them, decodes the bit-fields, and prints the physical values (Voltage, Current, Temperature) over UART at **115200 baud**.iLLD Libraries which belongs to Infinion Tecnhnology company  are used .In addition, HTerm  appis used to see the messages.

---

## Hardware
- **MCU:** AURIX™ TC375 (Lite Kit)
- **Transceiver:** TLE9251V
  - STB pin (P20.6) → LOW (normal mode)
- **CAN0 Pins:**
  - TXD → P20.8
  - RXD → P20.7
- **UART (ASCLIN):**
  - Baudrate: **115200-8-N-1**
  - Connected to PC via USB-UART adapter 

---

## CAN Configuration
- **Bitrate:** 500 kbit/s  
- **Sample Point:** ~80 %  
- **SJW:** 3  
- **Frame type:** Classic CAN, Standard ID, DLC = 8  
- **Filter:** Accept-all → FIFO0  

---

## Message Definition
| ID    | Signal     | startBit | length | Scale       | Unit |
|-------|------------|----------|--------|-------------|------|
| 0x400 | Voltage    | 6        | 12     | raw / 100   | V    |
| 0x500 | Current    | 16       | 8      | raw / 10    | A    |
| 0x600 | Temperature| 32       | 12     | raw / 10    | °C   |

Encoding uses **Intel/LSB0** bit numbering.

---

## Example Values
PCAN-View TX frames (DLC=8, hex):
| ID    | Physical Value | Raw  | DLC=8 Payload (hex)                  |
|-------|----------------|------|--------------------------------------|
| 0x400 | 12.56 V        | 1256 | `00 34 01 00 00 00 00 00`            |
| 0x500 | 8.8 A          | 88   | `00 00 58 00 00 00 00 00`            |
| 0x600 | 32.3 °C        | 323  | `00 00 00 00 43 01 00 00`
## WARNING Logic (in UART output)

| ID    | Physical Value | Raw   | DLC=8 Payload (hex)                  |
|-------|----------------|-------|--------------------------------------|
| 0x400 | 15.00 V        | 1500  | `00 77 01 00 00 00 00 00`            |
| 0x500 | 10.0 A         | 100   | `00 00 64 00 00 00 00 00`            |
| 0x600 | 144.4 °C*      | 1444  | `00 00 00 00 A4 05 00 00`  
---

## UART Output
When frames are sent from **PCAN-View** (TX), the TC375 receives them via Uart RX, decodes the bit-fields, and prints the physical values to the UART terminal at **115200 baud**.  

This allows one way observation:  
- **PCAN → TX → TC375 RX → UART**  

The values printed over UART correspond to the decoded signals using the formulas defined in code:  
- **Voltage [V] = raw / 100** (12-bit @ startBit 6)  
- **Current [A] = raw / 10** (8-bit @ startBit 16)  
- **Temperature [°C] = raw / 10** (12-bit @ startBit 32)  
## Usage
1. Connect **PCAN-USB** to CAN_H / CAN_L of the Lite Kit board.  
2. Open **PCAN-View**, set **500 kbit/s**, Standard ID, DLC=8.  
3. Transmit frames with IDs 0x400 / 0x500 / 0x600 and correct payload.  
4. Open a terminal (HTerm, PuTTY, etc.) at **115200-8-N-1**.  
5. Observe decoded values printed over UART in real-time.
### Normal values
<img width="965" height="160" alt="Peakcan_Gönderdiğm" src="https://github.com/user-attachments/assets/d1586c65-222a-4fc7-b406-b14efc785af7" />

---

<img width="1906" height="335" alt="UARTFOTOSU" src="https://github.com/user-attachments/assets/657c9089-d63d-4659-80e6-e4c0a1a9f4d6" />

---

### High / Warning value

<img width="967" height="160" alt="Warning_Uart peakcan" src="https://github.com/user-attachments/assets/455e4938-64ae-46d0-86ba-e5ae3b3c1c86" />

---

<img width="653" height="61" alt="Warning_Uart" src="https://github.com/user-attachments/assets/6e2e4197-ebd6-49e7-8e5e-8469b77ec591" />


