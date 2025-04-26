1x G-EDM EVOIII Pulseboard

1x G-EDM EVOII or EVOIII Motionboard

1x ESP32 WROOM32 AZdelivery 38Pins CP2102

1x ILI9341 SPI SD TOUCH

4x TMC2209 v2 with big heatsink

1x rocker switch (ON/OFF)

JST wires of different types.

1x 10A 75mV Shunt

1x 12V Cooling fan (No high speed fans. Simple 120mmx20mm silent fan is ok. High Speed fans may induce noise etc.)

1x MEAN WELL APV-35-12 36W 12V 3A PSU

1x 12v 10A PSU

1x Lianshi 6A 0-80V variable voltage PSU

ONE of those (TTL version! NOT RS-485 and also not the RF version!):

    1x DPH8909 Programmable Power Supply TTL 0-96V 0-9.6A

    1x DPM8605 Programmable Power Supply TTL 0-60V 0-5A



# Warning

EDM creates a lot of EMI noise. The motionboard with the sensitive ESP and SPI communication needs to be isolated from the 
pulsing engine. And it also should not be placed next to the sparks. A strong discharge can mess things up pretty easy.

The pulse electronics and all PSUs need to be placed inside a metal box and the motionboard needs to be outside of this box.
Best case would be to put the motionboard into a second metalbox. There are some limits about the distance between motionboard and pulseboard. The wires should be kept as short as possible and the DPH/DPM interface wire can create problems if it is too long.

# Notes

The ON/OFF switch is required! Limit switches are optional. The ON/OFF switch acts as eStop in the process too. If it is not connected or turned to OFF the process will not start and all motion is blocked.


# ESP32 note

Go for the AZdelivery boards. So far everyone who bought a cheap knockoff had issues of some kind. Inverted colors on the display was one of them. It is the heart of everything. Don't cheap out on that.

# ILI9341

There are hundreds of different version available. Not only different sizes but also different communication interface, with or without touch and with or without SD card and also different touch controllers.

The display needs to use SPI communication, XPT touch controller and provide a SD card reader.
2.8" size is nice but 2.4" works too.

It is also recommended to use the AZdelivery display.


# DPM8605 vs DPH8909

The DPH8909 will allow a higher voltage. From what I have seen it is not really needed. Cutting at 80V sounds and looks really
impressive but the surface finish is ugly. 70V is a nice working voltage. So far all showcase cuts where done with the DPM8605
at 60V and only one with the DPH8909.

The DPM8605 is still not deprecated and a valid option. It costs only half of what the DPH costs and is easily available. The DPH8909 in
the TTL version ships from china and can take two weeks to deliver.

DPM8605 has a max inpt voltage of around 70v. I recommend 65-70v to power it. DPH8909 can take the full max voltage of the 0-80V PSU without problems.


# About the 12V 10A PSU:

I use this exact model from Kingwen:
https://www.amazon.de/dp/B08QCNYLY8

A different one was tested but it induced noise into the motionboard and messed a little with the feedback signal.
If the cFd feedback does not produce a stable 0 reading without load the 12v PSU may be the first thing to check.
The Kingwen never made any troubles.


# About the variable voltage PSU

They don't all produce the exact same max output voltage. Some are very close to 80v and some can go up to 84v.
It is ok to use 84v if the DPH8909 is used but with the DPM8605 it needs to be set to 70v max.

Lianshi 0-80v variable voltage PSU (6A 480W):

https://www.amazon.de/dp/B0BQ33XG33

# Connecting the shunt

It does not break anythin if the shunt sensing wires are connected wrong on the pulseboard. It would just not generate a feedback reading.
If no cFd reading is visible try swapping the shunt sensing wires on the terminal. High side goes to the left input. Or it should. Don't remember currently.
