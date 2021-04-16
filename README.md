# GeigerNano
**GC-1602-NANO** firmware from scratch

>NOTE: You have to install LiquidCrystalIO and dependencies from Arduino Library Manager to compile sketch.

## Porpoise
After purchasing affordable **CAJOE 1.3 GC-1602-NANO** Geiger counter off Aliexpress, I found the firmware for it appears to not be available anywhere. Update: [File drop](https://drive.google.com/drive/u/0/folders/0B9itH-BnWE5sY2JGRkM4MWhSYkE) contains bare minimum sketch for serial CPM logger.
Due to the Geiger counter having Arduino Nano compatible MCU on board, it could work as serial logger over the USB port, but this feature is missing from the original firmware.
Therefore I decided to re-implement the firmware from scratch to implement additional changes.

## Problem definition
**GC-1602-NANO CAJOE 1.3** geiger counter kit has **Arduino Nano 5V** compatible board connected to **1602A 16x2 LCD** display with **PCF8574 I2C** backpack. It's available pre-soldered (Due to SMT components; other versions have unassembled kits), with Chinese glass **J305** gamma/slow beta Geiger-tube. The original firmware appears to calculate counts per 10 second and extrapolate it to 60 seconds, and has a 10 second booting animation while it collects first set of samples, with no serial output or dead time compensation.

## Saving and updating original firmware
To store original factory firmware, you may want to do (Replace COM6 with whatever port is assigned in Device Manager) assuming normal install path and running from wherever you want to keep the firmware:

    "c:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe" -C "c:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf" -pm328p -carduino -PCOM6 -b57600 -Uflash:r:CAJOE-13-GC-1602-NANO-original.hex:i

Back up the original firmware so you don't accidentally overwrite it. If you want to return back to the original firmware, use "-Uflash:w:" instead:

    "c:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe" -C "c:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf" -pm328p -carduino -PCOM6 -b57600 -Uflash:w:CAJOE-13-GC-1602-NANO-original.hex:i

To program the new sketch on **GC-1602-NANO** from Arduino IDE, select "Board: Arduino Nano", "Processor: ATmega328P (Old Bootloader)" for the Arduino clone board and correct serial port.

## Compatibility
This sketch is compatible with many other possible setups including **CAJOE 1.1**, **Geekreit** and **RHElectronics** ones likely requiring only small changes.
* *LiquidCrystalIO* library is used to support wide variety of LCD displays, although you will have to change configuration to match their connection, and likely display formatting for the layout.
* Currently it's set up for standard I2C SDA=A4, SCL=A5 on Nano at address 0x27. Geiger counter output (labeled VIN on some CAJOE boards) signal connects to Interrupt 0 at pin D2 on Nano. 
* Counts to microSievert/hour conversion factor should be set according to sample isotope and Geiger tube. Default value 151.0 is based on compatibility with **GC-1602-NANO** stock firmware.

## Features
Sketch measures interrupts per second, calculating sliding sum of counts over one minute.

Given the base unit is Counts Per Minutes, I felt one minute collection time was most accurate, although it means the results will take a minute to settle to new value.

The sliding window CPM is written to the USB serial port at *9600bps* every second, and can be plotted with Arduino Serial Plotter, or read in for further analysis.

As a way of compromise for survey meter needs, if the one-second CPM value exceeds current reading by more than 1000 (This would normally require 17 more/less counts per one second) the reading will reset to new CPM value. I'm still looking for best way to trigger this.

Pseudo standard deviation is calculated for the one second samples over one minute. Standard deviation for the counts per second is multiplied by square root of sample number (60), eliminating the divisor in standard deviation, to arrive at a pseudo-stdev for range of CPM variation which reacts within single minute.

Dead-time which is the minimum time between counts is tracked as well, shown as lowest microseconds between counts after the microSieverts/hour. For **GC-1602-NANO** this appears to be 250 microseconds due to pulse-width of the count signal. The interrupt code itself seems to run 4-8 microseconds, so the increased bookkeeping doesn't seem to be an issue.

## Notes
>The board has 500V+ voltage supply for the Geiger tube, while the current is low, it will definitely shock you so don't go touching the board itself while it's powered. Short-circuits on the board could also inadvertently send that voltage to connected equipment, so it's a good idea to encase it for use.

The jumper *P100* on **CAJOE** boards connects the count signal to amplifier for the headphone jack which can also be used to connect it to computers via sound card. [GeigerLog](https://sourceforge.net/projects/geigerlog/files/) is a great tool for this, and includes great documentation on CAJOE boards.
On the 1.3 board there appears to be no easy way to disable the count ticker sound as *J1* is SMT component. This will slowly drive you insane at house, "The Tell-Tale Heart" style, so one might want to de-solder the piezzo-speaker on the board.

Illustration of possible wiring placement of the piezzo speaker on the **CAJOE 1.3 GC-1602-Nano** board. Note you must also solder D8 pin on the top of the Arduino clone board, as only originally connected & corner pins are pre-soldered! Some suggest you should use resistor with the piezo, but it's worked fine for me without and that would make it quieter. The board layout is somewhat sub-optimal for this, because as can be seen from the **CAJOE-1.1** board schematic in the file drop, *J1* is connecting the piezzo to *+5V* while the tick sound is generated by second *555 timer IC2* driving transistor *Q4* to cut off the ground side. This arrangement does allow just powering the piezzo on or off and letting the circuit on the board worry about actually driving it however, but the savings in processor cycles are minimal because the Nano has timer outputs.

I'm considering ~~driving *Q4 base* via *R24* with another Nano pin instead of the 555, which would allow creating different frequencies and warning sounds with **Arduino Nano** timer. This will take finer soldering, be less reversible and render the whole 555 circuit obsolete.~~ shorting the ground pin of the piezzo directly to ground to allow driving it from the Nano in a reversible manner if the *J1* mod is also done. Really, the Chinese missed a major opportunity to cut down on component count by having the Arduino drive the piezzo, leds and headphones directly!
![J1 removed and linked to Pin 9](https://github.com/Donwulff/resources/raw/main/20210410_200253.jpg)

**The author takes no responsibility for results of use of the device or the sketch herein, intentional or accidents, nor for fitness for any particular purpose, but then you knew that already.**
