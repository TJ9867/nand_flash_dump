# Pico NAND Flash Dumper

This repo is my current best attempt at a Raspberry Pi Pico NAND Flash. It doesn't follow any formal specs perfectly (e.g. ONFI), which is probably why it doesn't quite work.

## Prequisites
- Pico-compatible board with enough pins (15 with at least 8 sequential pins)

## Default Pinout
Everything can be configured (the logic for reading the I/O pins relies on sequential pin numbering, so make sure IO0-7 are continguous)

I/O pins 0-7 = GP0-7
CLE = GP22 
ALE = GP21

CE = GP20
RE = GP19
WE = GP18
WP = GP17
RY = GP16

## Compile
```bash
# make sure Pico C SDK installed correctly and PICO_SDK_PATH is set
mkdir build && cd build && cmake ..
make
```

## Connect to Dumper
1. Plugin the Pico / Flash the Firmware (hold button while plugging in, copy the `.uf2` produced by build onto the PICO drive that appears)
2. Open a serial terminal program like screen (Linux): `screen /dev/ttyACM0 115200` or PuTTY (windows)
3. Typing commands let you interact with the dumper (eventually the idea is that it will also allow for writing, too)
```
0 = READ ID bytes - this will show you the standard ID bytes that can be used to decode the flash type, manufacturer, page size, etc.
1 = READ PAGE - this reads a single page size. Right now the tool needs to be modified to adjust the page size, but at some point the ID reading will be done automatically
2 = RESET PAGE NO - this read the page number internally, so reading can begin from the beginning of the flash.
3 = SET PAGE NO - this sets the page number to a specific value  so reading can begin at a specific offset
4 = GET DRIVE STRENGTH - an debugging command added to check whether drive strength was properly being set
5 = DO NOTHING
```
