# GeigerNano
GC-1602-NANO firmware from scratch

After purchasing affordable CAJOE 1.3 GC-1602-NANO Geiger counter off Aliexpress, I found the firmware for it appears to not be available anywhere.
Due to having Arduino Nano compatible MCU on board, it could work as serial logger over the USB port, but this feature is missing from the original firmware.
I therefore decided to re-implement the firmware from scratch to implement additional changes.

GC-1602-NANO CAJOE 1.3 geiger counter kit has Arduino Nano 5V compatible board connected to 1602A 16x2 LCD display with PCF8574 I2C backpack.

The sketch is compatible with many other possible setups including CAJOE 1.1, Geekreit and RHElectronics ones requiring only small changes.

LiquidCrystalIO library is used to support wide variety of LCD displays, although you will have to change configuration to match their connection, and likely display formatting for the layout.
Currently it's set up for standard I2C SDA=A4, SCL=A5 on Nano at address 0x27. Geiger counter output (VIN on some CAJOE boards) signal connects to Interrupt 0 at pin D2 on Nano. 
Counts to microSievert/hour conversion factor should be set according to sample isotope and Geiger tube. Default value 151.0 is based on compatibility with GC-1602-NANO stock firmware.

Sketch measures interrupts per second, calculating sliding value of counts over one minute.
Given the base unit is Counts Per Minutes, I felt this was most accurate, although it means the results will take a minute to settle to new value.
The sliding window CPM is written to the USB serial port every second, and can be plotted with Arduino Serial Plotter, or read for further analysis.
As a way of compromise for survey meter needs, if the one-second CPM value exceeds current reading by more than 1000 (This would normally require 17 more/less clicks for one second) the reading will reset to new CPM value.
Pseudo standard deviation is calculated for the one second samples over one minute. The unit is CPM, but the measurements are per 1 second, so the value is not well defined.
Dead-time which is the minimum time between ticks is tracked as well, shown as lowest microseconds between ticks after the microSieverts/hour.
