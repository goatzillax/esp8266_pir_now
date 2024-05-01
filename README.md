# esp8266_pir_now

Rework of an older PIR sensor project with updated goals.

![Samples](/docs/PIR_LFP_NOW.jpg)

## Transmit unit

The system is intended to support multiple transmitter/sensor units in an 'offline' (non-infrastructure) mode in case of remote usage.  Think:  camping.  Don't want no bears sneaking up on me know what im saying i ain't down with that git away from thar, bear u don't know me like that.

All the transmitters/sensors transmit via ESP-NOW and should be powered by a single LiFePO4 cell wired directly into 3.3v.

Upon first power up, the transmitter will scan available channels until a message is successful and then save that channel to RTC memory.

The chip should then enter deep sleep until woken up by PIR detection.

### The Problem with D3

Just realized why deep sleep with a timeout was failing.  It wasn't actually failing, it was because D3 is being used as the PIR input.

D3 must be pulled high at boot time or boot will fail.  Under normal PIR triggering, this is true.

However, if the board wakes up via a deep sleep timeout, the PIR output will still be driving low...  causing boot to fail.

The Wemos shield allows selection of the target GPIO...  but my v1 PCB doesn't because i r dum.

### TODO

* rescan after X amount of failbertz
* add selected channel to message for debugging

## Receiver unit

The receiver unit pretty much has to be running fulltime in order to receive ESP-NOW messages from the sensors.  (i.e. no sleep of any kind, including modem/wifi sleep.)  And while it's doing that, it might as well run a webserver to present controls and events.

![Samples](/docs/smaple_page.png)

In infrastructure mode:

* fire up NTPClient

In offline mode:

* use form to set time from client

### Files

#### www/history.json

Not a real file in the filesystem, generated on the fly from the ESP's history buffer.

#### www/sensors.json

Real file residing on the filesystem, updated and maintained by the ESP.  Small hands, smell like cabbage, looks like this:

        {
           "0xcacaca" : {
              "name" : "urmom",
              "status" : 0
           }
        }

### Division of work

Currently a lot of tasks are split between the ESP and the web client via Javascript.

The ESP "owns" (writes) both files.  The web client makes a bunch of queries to the ESP through http get methods which is actually kind of cumbersome to program.

The alternative is to let the web client own the sensors.json file, make all manipulations necessary, and then simply upload the file wholesale to the ESP and tell it to refresh.  This is probably less secure but a lot less cumbersome in the ESP code because writing hooks for all that shit gets tedious and the same tasks are slightly easier in Javascript on the client.  Congrats to c++ for being slightly worse than Javascript.

Would need to add some form of synchronization to the webserver to avoid collisions if that's even possible and avoid stale data usage by the client (generation/sequence number).

### TODO

* How to deal with millis() rollover in offline mode
* Might need to deal with the 2038 rollover.  Helo 64bit epox?

## Hardware

### Lolin D1 Mini

https://www.wemos.cc/en/latest/d1/d1_mini.html

Everything currently targeted at the Lolin D1 Mini because dats wat I has on hand.

The original branded hardware...  acts kind of fishy sometimes.  The ADC resistor seems off and the reset line is touchy even with their own shields.  Weirdly, the clones seem to actually work better, but are ... dimensionally challenged sometimes.

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

I need to revisit the docs for reset, but in general in order to reset I think the ESP8266 needs to see a transition from high to low (and stay low for 100ns?) on the reset line.

The PIR sensors all just pull high when motion is detected.  So what's needed is typically a capacitor at the gate of a FET which then pulls the reset line low for RC-constant amount of time.  Sometimes called an edge detector.

Also need a diode to shunt the reverse voltage against the gate when the PIR signal goes low.

If timer functionality is desired, a diode is placed between D0 and RESET so it can also directly pull reset low.

#### Dat Body Diode

What's sort of vaguely missing is if the code runs for longer than the timeout period of the PIR signal.  If another edge comes in from the PIR...  you might get reset with your pants down.  And nobody wants to see that, not even the bear.

So you need to pull the gate of the reset FET low to prevent further resets while you're working.  And since you already need a diode shunt the reverse voltage on the gate of the reset FET...  just throw a second transistor (body diode) in its place.

#### Even More Reset Hijinx

* https://github.com/esp8266/esp8266-wiki/wiki/Boot-Process
* https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

The gates of Q1 and Q2 are both pulled low in my PCB, so if I want to reuse them, I can't use them on boot-critical pins.  I.e. these are completely no bueno:

* D4/GPIO2 (Boot selector - High)
* D3/GPIO0 (Boot selector - High) This is the PIR output, which would be high anyways on a deep sleep reset
* TX/GPIO1 (? - High)
* D8/GPIO15 (Pulled to ground, boot fails if pulled high) - Might be OK if pulled to ground?

Side-eye:

* D0/GPIO16 (High at boot) - this is used for deep sleep wakeup though
* RX/GPIO3 (High at boot)

PCB has a solder jumper to select from

* RX
* D5
* D6
* D7

Options for adding a buzzer for receiver:

1.  ~~GPIO can sink 20ma, so deadbug a 220ohm resistor in series and connect Buzzer+ to H1 3.3v and Buzzer- to D3.  Use D3 as active low output.  This isn't full volume, but probably don't actually need full volume.~~  This pretty much sucks; the buzzer does squat at that current.
2.  Connect Buzzer+ to H1 3.3v, Buzzer- to Q2 Drain (so the R3/R2/Q1 net).  Solder jumper D5/6/7 and use as active high output.  Pain in the ass, but preserves D3.  5v buzzer running at 3.3v is still pretty loud.

### PCB

![PCB](/docs/PIR_LFP_Schematic.png)

#### TODO

* Name the parts better
* Put optional parts on back, required parts on front (looking at you, R3)
* Castellated edges
* Space for 3.5v zener diode
* Space for buzzer (avoid I2C pins) (for RX)
* I2C RTC header (for RX)
* Switch to FET arrays (2x for reset line+latch, 1x for buzzer, 1x inverter?)
* Space for power capacitor
* Bigger vias
* Spicy simulation results

### Case

TX V1 case based on Lolin D1 Mini + 18650 LFP cell holder + custom PCB stack

Can't figure out how to embed the STL viewer in github markdown.

Viewer doesn't sport binary apparently, nor does it support .gz files.  Neat.  It's nearly the end of 2023, yall.

#### TODO

* total weatherproofing and sealing
* external power input
* external reset

### RCWL-0516

The RCWL-0516 is a curious animal.

https://github.com/jdesbonnet/RCWL-0516

Notable things:

* Takes 4-28v.  Apparently needs higher, like 5v, to run reliably.
* Has an internal regulator, probably linear.  3.3v output is limited to 100ma.

So to use this I'd probably bump up to a 2s LiFePO4 battery, which complicates matters.

Easiest thing to do might be to use a step down regulator with an enable to produce 3.3v.

Tie the regulator enable to the output of the RCWL-0516 and also allow the microcontroller to latch it high as long as it needs.  Might be nice to know if Vout was open drain or push-pull.

This saves power by disabling the regulator, however it also means I can't use RTC memory since power is totally shut down.  Which means I need an alternate way to rescan for the ESP-NOW channel.  Maybe a bootstrap gpio with momentary switch.

![wegulator](/docs/wegulator.png)

So the RCWL-0516 triggers Vout and turns on the regulator.  The uC has 2 seconds to boot up and latch the EN for itself.  When it's done, it can deassert EN.
