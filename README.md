# Chronos-858D: Custom ESP32-S3 Hot Air Rework Station

![Chronos-858D Project Banner](IMAGES/CAD.png)
![Chronos-858D Project Banner](IMAGES/CAD1.png)
![Chronos-858D Project Banner](IMAGES/CAD2.png)
![Chronos-858D Project Banner](IMAGES/CAD3.png)
![Chronos-858D Project Banner](IMAGES/CAD4.png)
![Chronos-858D Project Banner](IMAGES/CAD5.png)

## 1. Project Overview
The **Chronos-858D** is a custom-engineered, smart benchtop hot air rework station built around the **ESP32-S3-DevKitC-1** that combines microcontroller precision with robust AC/DC power electronics. Designed from the ground up with a custom 2-layer KiCad PCB and a 3D-printed Fusion 360 enclosure, the station regulates a 220V AC 700W ceramic heating element using  PID control through a  optical isolation stage while independently driving a 24V DC brushless blower fan via PWM. For workshop safety  a physical roller-lever microswitch mounted on the handle cradle automatically detects when the hot air gun is docked, immediately cutting power to the heater coil and running a timed 1-minute cooling cycle until the nozzle drops safely below 50°C, with all real-time temperature measuring, airflow power, and with the stats on a 2.8-inch ILI9341 SPI TFT display controlled by 2 rotary encoders.

---

## 2. Final CAD Renders & Enclosure Design
The enclosure and external handle holder are fully modeled in Fusion 360  for 3D printing in heat-resistant PETG or ABS, and also have an angled front instrument panel and ventilated case that house both the custom main PCB and the 24V 3A industrial SMPS in a compact case. To add tight 3mm thread depths and 3.2mm mounting holes on the display and main circuit board, the internal standoffs are designed around M3 × 3mm knurled brass heat-set inserts paired with M3 × 4mm pan head machine screws, while the side-mounted integrates a recessed slot for a KW11-3Z roller limit switch positioned along the cool lower grip of the 858D handle so the resting weight of the wand actuates the cooldown switch smoothly without exposing the switch body to nozzle heat.

---

## 3. ⚠️ High-Voltage Safety Warning
> **DANGER — 220V–240V AC MAINS VOLTAGE:** This rework station interfaces directly with lethal 220V–240V AC mains electricity to power the 700W heating element and internal SMPS, which presents a severe risk of electrocution, burns, or fire if handled improperly. Always disconnect the main AC power cord from the wall outlet before opening the enclosure, probing the circuit board, or modifying any wiring, and strictly maintain the 2.5mm physical creepage and clearance gap on the PCB between the high-voltage AC heater section and the low-voltage DC microcontroller logic. Ensure that the incoming protective earth ground pin on the IEC320 C14 socket is securely bonded to the metal frame of the 24V SMPS and the insulated-tab BTA16-600B TRIAC heatsink, and insulate all high-voltage solder joints and terminal connections using polyolefin heat shrink tubing and high-temperature polyimide Kapton tape before powering the system.

---

## 4. Electronic Specifications & Pinout
The electrical designing  separates high-voltage AC switching from low-voltage 3.3V digital control across a custom 2-layer  PCB powered by a 24V DC 3A (72W) enclosed SMPS protected by a 2A resettable PTC polyfuse . An onboard LM2596 buck converter  steps the 24V rail down to a regulated 5.0V DC to feed the ESP32-S3-DevKitC-1 , while the 24V blower fan is controllerd through an IRLZ44N logic-level N-channel MOSFET , paired with a 10kΩ gate pull-down resistor , 220Ω gate resistor , and 1N4007 flyback clamping diode . 

On the isolated AC side  protected by a 5A 250V fast-blow glass fuse , HEATER_PWM  drives an MOC3041 zero-crossing optocoupler  through a 150Ω current-limiting resistor , which triggers a 16A 600V BTA16-600B TRIAC  via a 360Ω 1W gate resistor  and a 39Ω 2W / 0.01µF 275VAC X2 RC snubber network . Closed-loop temperature feedback from the handle's K-type thermocouple  is digitized by a MAX6675 cold-junction compensated amplifier  sharing the hardware SPI bus with the non-touch 9-pin 2.8" ILI9341 240×320 TFT display (`J3`), alongside two EC11 rotary encoders  and the holder  limit switch.

![KiCad Full Schematic Diagram](IMAGES/SCHEMATICS.png)
![KiCad 2-Layer PCB Layout](IMAGES/TRACKS.png)
![KiCad 2-Layer PCB Layout](IMAGES/PCB1.png)
![KiCad 2-Layer PCB Layout](IMAGES/PCB2.png)
![KiCad 3D PCB Render](IMAGES/PCB3d.png)

## 5. Enclosure & Case Assembly
pressing the M3 × 3mm brass heat-set inserts flush into the 3D-printed mounting bosses inside the front bezel and main chassis floor, then pre-tune the LM2596 buck converter  with a multimeter to output exactly 5.0V DC before seating the ESP32-S3 into its female headers. Next, mount the 2.8-inch ILI9341 TFT display and the populated main PCB onto the internal standoffs using M3 × 4mm pan head screws, fasten the two EC11 rotary encoders and the GX16-8 aviation panel socket to the front face, and bolt the TO-220 aluminum heatsink onto the BTA16-600B TRIAC  with a thin layer of thermal paste. Secure the 24V 3A SMPS to the base of the enclosure double-sided heavy-duty foam tape, snap the IEC320 C14 AC mains inlet switch into the rear panel cutout, and fasten the KW11-3Z roller limit switch onto the external handle cradle using M2 screws and nuts. Finally, wire the 16 AWG silicone power cables from the IEC inlet and SMPS to the screw terminal blocks  and the 24 AWG signal wires from the cradle switch , insulating all exposed AC and DC joints with heat shrink tubing and Kapton tape before closing the case and verifying electrical isolation with a continuity test.


---

## 6. Firmware Architecture & Cooldown Logic
The ESP32-S3 firmware runs a non-blocking C++ control loop that continuously polls the MAX6675 sensor over hardware SPI every 250ms, feeds the nozzle temperature into a PID control algorithm to update the zero-crossing heater duty cycle on `GPIO 4` (`HEATER_PWM`), and refreshes the 2.8-inch ILI9341 TFT display while tracking rotary encoder interrupts for instant target temperature and fan speed adjustments. When the user places the hot air gun into the cradle, the weight of the handle depresses the KW11-3Z roller lever and pulls `GPIO 6` (`REED_SW`) to `GND` (`LOW`), which immediately forces the heater output to `0%`, switches the UI to Cooldown Mode, and locks the 24V blower fan (`GPIO 5`) at 80%–100% speed for a full 1-minute cooldown timer (and until the thermocouple reading drops below 50°C) before shutting the fan off completely and entering low-power

## 7. Bill of Materials (BOM)
The complete build, including custom PCB fabrication, 3D-printed enclosure parts, electronic modules, fasteners, and SMD rework consumables, totals approximately **$100 USD**:

| # | Item Description | Schematic Ref / Application | Qty | Unit Price (USD) | Total (USD) |
| :-: | :--- | :--- | :-: | :-: | :-: |
| 1 | **Custom 3D-Printed Enclosure + Cradle** | 1 | $20.12 | **$20.12** |
| 2 | **Custom 2-Layer PCB Manufacturing** | PCB | 1 lot | $11.83 | **$11.83** |
| 3 | **ESP32-S3-DevKitC-1-N8R8** | 1 | $12.43 | **$12.43** |
| 4 | **2.8" SPI TFT LCD (ILI9341, Non-Touch)** | 1 | $8.05 | **$8.05** |
| 5 | **MAX6675 Thermocouple Module** | 1 | $4.14 | **$4.14** |
| 6 | **LM2596 DC-DC Buck Step-Down Module** | 1 | $0.71 | **$0.71** |
| 7 | **Quick 858D Replacement Handle** |  1 | $6.63 | **$6.63** |
| 8 | **24V 3A (72W) Metal Enclosed SMPS** |1 | $7.10 | **$7.10** |
| 9 | **KW11-3Z Roller Lever Micro Switch** |  1 | $0.41 | **$0.41** |
| 10 | **IEC320 C14 AC Mains Socket + Rocker Switch** | 1 | $0.71 | **$0.71** |
| 11 | **Double-Sided Heavy Duty Foam Mounting Tape** |  1 roll | $1.20 | **$1.20** |
| 12 | **Polyimide Heat Insulation Tape (Kapton, 10mm×33m)** |  1 roll | $1.49 | **$1.49** |
| 13 | **Finned Aluminum Heatsink Kit (TO-220)** | 1 kit | $1.10 | **$1.10** |
| 14 | **Desoldering Vacuum Pump (Solder Sucker)** |  1 | $1.07 | **$1.07** |
| 15 | **Heat Shrink Tubing Assortment (100 pcs)** | 1 pack | $0.95 | **$0.95** |
| 16 | **Thermal Heatsink Compound Paste (Syringe)** |  1 syringe | $0.41 | **$0.41** |
| 17 | **858D Hot Air Gun Nozzles (3-Pack: 6, 8, 12mm)** |  1 set | $4.13 | **$4.13** |
| 18 | **Mechanic UV-559 Solder Flux Paste Syringe (10cc)** |  1 | $2.36 | **$2.36** |
| 19 | **63/37 Rosin Core Solder Wire (0.8mm, 50g)** | 1 spool | $2.96 | **$2.96** |
| 20 | **Anti-Static ESD Precision Tweezers (6 pcs)** | 1 set | $2.91 | **$2.91** |
| 21 | **Silicone Flexible Hookup Cable (16 & 24 AWG)** | 2 rolls | $1.78 | **$1.78** |
| 22 | **BTA16-600B TRIAC (Insulated Tab)** | 1 | $0.41 | **$0.41** |
| 23 | **MOC3041 Zero-Cross Optocoupler** |  1 | $0.30 | **$0.30** |
| 24 | **6-Pin DIP IC Socket** |  1 | $0.06 | **$0.06** |
| 25 | **IRLZ44N Logic-Level N-MOSFET** |  1 | $0.77 | **$0.77** |
| 26 | **1N4007 Diode** | 1 | $0.02 | **$0.02** |
| 27 | **39Ω 2W Metal Oxide Resistor** |  1 | $0.06 | **$0.06** |
| 28 | **360Ω 1W Axial Resistor** |  1 | $0.04 | **$0.04** |
| 29 | **150Ω, 220Ω, 10kΩ Resistors** |  3 | $0.02 | **$0.07** |
| 30 | **0.01µF (10nF) 275VAC X2 Film Cap** | 1 | $0.18 | **$0.18** |
| 31 | **5A 250V AC 5×20mm Fuse + 2x Clips** | 1 set | $0.18 | **$0.18** |
| 32 | **2A Resettable PTC Polyfuse** |  1 | $0.14 | **$0.14** |
| 33 | **EC11 Rotary Encoders + Knobs** | 2 sets | $0.59 | **$1.18** |
| 34 | **GX16 8-Pin Aviation Plug/Socket Set** |  1 set | $1.42 | **$1.42** |
| 35 | **2-Pin 5.08mm Screw Terminals (KF301)** |  7 pcs | $0.07 | **$0.50** |
| 36 | **1×40 Male & Female Berg Header Strips** |  2 strips | $0.24 | **$0.47** |
| 37 | **M3 × 3mm Brass Heat-Set Inserts** | 10 pcs | $0.04 | **$0.36** |
| 38 | **M3 × 4mm Pan Head Phillips Screws** |  10 pcs | $0.02 | **$0.18** |
| 39 | **M3 Stainless Steel Nuts & Washers Pack** |  1 pack | $0.24 | **$0.24** |
| 40 | **M2 × 8mm / 10mm Screws + M2 Nuts Pack** |  1 pack | $0.19 | **$0.19** |
| | **TOTAL ESTIMATED BUILD & TOOLING COST** | | | | **~$99.26 USD** |