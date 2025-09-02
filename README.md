# Week-3-
# TC375 Lite Kit CAN ↔ UART Demo

## Overview
This project demonstrates **Classic CAN reception** on the Infineon **AURIX™ TC375 Lite Kit** and forwarding the decoded values to a PC terminal via **UART**.  
With a **PEAK PCAN-USB** adapter and **PCAN-View** software, you can transmit CAN frames from the PC. The TC375 receives them, decodes the bit-fields, and prints the physical values (Voltage, Current, Temperature) over UART at **115200 baud**.

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
  - Connected to PC via USB-UART adapter / on-board debugger channel

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
- **ID 0x400** (Voltage = 12.00 V) → `00 2C 01 00 00 00 00 00`
- **ID 0x500** (Current = 8.7 A) → `00 00 57 00 00 00 00 00`
- **ID 0x600** (Temp = 25.3 °C) → `00 00 00 00 FD 00 00 00`

---

## UART Output
When frames are sent from **PCAN-View** (TX), the TC375 receives them via CAN RX, decodes the bit-fields, and prints the physical values to the UART terminal at **115200 baud**.  

At the same time, the TC375 periodically transmits its own frames (0x400/0x500/0x600). These can be observed in **PCAN-View RX**, and the same decoded values are also printed on the UART terminal for verification.  

This allows two-way observation:  
- **PCAN → TX → TC375 RX → UART**  
- **TC375 TX → PCAN RX + UART decode**  

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
