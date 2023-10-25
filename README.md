# esp8266_pir_now

Rework of an older PIR sensor project with updated goals.

## Transmit unit

The system is intended to support multiple transmitter/sensor units in an 'offline' (non-infrastructure) mode in case of remote usage.  Think:  camping.  Don't want no bears sneaking up on me know what mi saying i ain't down with that git away from thar, bear.

Upon first power-up, the transmitter will scan available channels until a message is successful and then save that channel to RTC memory.

The chip should then enter deep sleep until woken up by PIR detection.

TODO:  debug retry/timer mechanism.  probably more espressif errata.

## Receiver unit

The receiver unit pretty much has to be running fulltime in order to receive ESP-NOW messages from the sensors.  (i.e. no sleep of any kind, including modem/wifi sleep.)

In infrastructure mode:

* fire up NTPClient to log time
* record events into a history buffer
* serve history on a webserver

In offline mode:

* halp?

TODO:  needs a beeper.  D4/GPIO2 is the LED which would be prefect butt it can't drive enough current.  so I need to invert the signal to the FET to drive a beeper...

## Hardware

### Lolin D1 Mini

Everything currently targeted at the Lolin D1 Mini because dats wat I has on hand.

The original branded hardware...  acts kind of fishy sometimes.  The ADC resistor seems off and the reset line is touchy even with their own shields.

### PIR Sensors

#### Lolin PIR Shield

https://www.wemos.cc/en/latest/d1_mini_shield/pir.html

Directly wires the PIR sensor output to D3 and...  that's about it.  No deep sleep/reset support because reason.

TODO:  wat's the timeout?

#### Other PIR Sensors

There are a bunch of DIY modules based on AM312 sensors.  For low power single-cell LFP usage, you'll want to pull the LDO regulator on the PCB and directly wire VIN.

Not tested yet.

TODO:  wat's the timeout?

### Reset Circuitry

I need to revisit the docs for reset, but in general on order to reset I think the ESP8266 needs to see a transition from high to low (and stay low for 100ns?) on the reset line.

The PIR sensors all just pull high when motion is detected.  So what's needed is typically a capacitor at the gate of a FET which then pulls the reset line low for RC-constant amount of time.  Sometimes called an edge detector.

Also need a diode to shunt the reverse voltage against the gate when the PIR signal goes low.

If timer functionality is desired, a diode is placed between D0 and RESET so it can also directly pull reset low.

#### Dat Body Diode

What's sort of vaguely missing is if the code runs for longer than the timeout period of the PIR signal.  If another edge comes in from the PIR...  you might get reset with your pants down.

So you need to pull the gate of the reset FET low to prevent further resets while you're working.  And since you already need a diode shunt the reverse voltage on the gate of the reset FET...  just throw a second transistor in there.

#### Even More Reset Hijinx

* https://github.com/esp8266/esp8266-wiki/wiki/Boot-Process
* https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

### PCB

![PCB](/docs/PIR_LFP_Schematic.png){: width="25%"}

#### TODO

* Name the parts better
* Put optional parts on back, required parts on front (looking at you, R3)
* Castellated edges
* Space for 3.5v zener diode
* Space for buzzer
* Switch to FET arrays (2x for RESET line
* Space for power capacitor

### Case

V1 case based on Lolin D1 Mini + 18650 LFP cell holder + custom PCB stack

Can't figure out how to embed the STL viewer in github markdown.

![STL](/hardware/PIR_LFP_case_v1-top.stl)

(/hardware/PIR_LFP_case_v1-top.stl)


