# Tested 86Box configuration
This describes 86Box config that this OS was tested on:

## Machine
- Machine type: Socket 7 (Single Voltage)
- Machine: ASUS P/I-P55TP4XE
- CPU type: Intel Pentium
- Frequency: 75 (MHz)
- Memory: 8MB

## Display
Any VESA card will probably work fine, but the one I was tested on is Cirrus Logic GD5430.
But note that I couldn't get any color video mode to work, it's monochrome only from my testing.

Previously it was also tested on Mach64VT2. This thing is new enough to happily work with 24-bit color.

## HDD and CD-ROM drive

HDD is just IDE, and CD-ROM is normal ATAPI drive.

## BIOS configuration

Not much special. Just make sure to boot from CD-ROM drive first.

Though in my case since I was restarting 86Box a lot to test the OS, I disabled all unused IDE HDDs in BIOS to make POST process a bit faster.
