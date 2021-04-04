# GeigerNano
GC-1602-NANO firmware from scratch

NOTE: You have to install LiquidCrystalIO and dependencies from Arduino Library Manager to compile sketch.

After purchasing affordable CAJOE 1.3 GC-1602-NANO Geiger counter off Aliexpress, I found the firmware for it appears to not be available anywhere.
Due to the Geicer counter having Arduino Nano compatible MCU on board, it could work as serial logger over the USB port, but this feature is missing from the original firmware.
Therefore I decided to re-implement the firmware from scratch to implement additional changes.

GC-1602-NANO CAJOE 1.3 geiger counter kit has Arduino Nano 5V compatible board connected to 1602A 16x2 LCD display with PCF8574 I2C backpack. It's available both as a kit and pre-soldered, with Chinese glass J305 gamma/slow beta Geiger-tube. The original firmware appears to calculate counts per 10 second and extrapolate it to 60 seconds, and has a 10 second booting animation while it collects first set of samples.

To store original factory firmware, you may want to do (Replace COM6 with whatever port is assigned in Device Manager):
"c:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe" -C "c:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf" -pm328p -carduino -PCOM6 -b57600 -Uflash:r:CAJOE-13-GC-1602-NANO-original.hex:i
Back up the original firmware so you don't accidentally overwrite it. If you want to return back to the original firmware, use "-Uflash:w:" instead:
"c:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe" -C "c:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf" -pm328p -carduino -PCOM6 -b57600 -Uflash:w:CAJOE-13-GC-1602-NANO-original.hex:i

To program the new sketch on GC-1602-NANO from Arduino IDE, select "Board: Arduino Nano", "Processor: ATmega328P (Old Bootloader)" and correct serial port.

This sketch is compatible with many other possible setups including CAJOE 1.1, Geekreit and RHElectronics ones likely requiring only small changes.

* LiquidCrystalIO library is used to support wide variety of LCD displays, although you will have to change configuration to match their connection, and likely display formatting for the layout.
* Currently it's set up for standard I2C SDA=A4, SCL=A5 on Nano at address 0x27. Geiger counter output (VIN on some CAJOE boards) signal connects to Interrupt 0 at pin D2 on Nano. 
* Counts to microSievert/hour conversion factor should be set according to sample isotope and Geiger tube. Default value 151.0 is based on compatibility with GC-1602-NANO stock firmware.

Sketch measures interrupts per second, calculating sliding sum of counts over one minute.
Given the base unit is Counts Per Minutes, I felt one minute collection time was most accurate, although it means the results will take a minute to settle to new value.
The sliding window CPM is written to the USB serial port at 9600bps every second, and can be plotted with Arduino Serial Plotter, or read in for further analysis.
As a way of compromise for survey meter needs, if the one-second CPM value exceeds current reading by more than 1000 (This would normally require 17 more/less counts per one second) the reading will reset to new CPM value.
Pseudo standard deviation is calculated for the one second samples over one minute. Standard deviation for the counts per second is multiplied by square root of sample number (60), eliminating the divisor in standard deviation, to arrive at a pseudo-stdev for range of CPM variation reacting within single minute.
Dead-time which is the minimum time between ticks is tracked as well, shown as lowest microseconds between ticks after the microSieverts/hour. For GC-1602-NANO this appears to be 250 microseconds due to pulse-width of the count signal. The interrupt code itself seems to run 4-8 microseconds, so the increased bookkepping doesn't seem to be an issue.
