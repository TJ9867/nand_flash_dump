# Pico NAND Flash Dumper

This repo is my current best attempt at a Raspberry Pi Pico NAND Flash. It doesn't follow any formal specs perfectly (e.g. ONFI), which is probably why it doesn't quite work.
## State of this Repo
Currently, this repo is not completely functional. There is a problem that occurs while dumping that I have yet to fix, which results in partially corrupted page reads.

## Prequisites
- Pico-compatible board with enough pins (15 with at least 8 sequential pins)

## Default Pinout
Everything can be configured (the logic for reading the I/O pins relies on sequential pin numbering, so make sure IO0-7 are continguous)

| Pin No. | Pin Name |
|   -     |     -    |
| GP0-7   | I/O 0-7  |
| GP22    |   CLE    |
| GP21    |   ALE    |
| GP20    |   CE     |
| GP19    |   RE     |
| GP18    |   WE     |
| GP17    |   WP     |
| GP16    |   RY     |

## Compile
```bash
# make sure Pico C SDK installed correctly and PICO_SDK_PATH is set
mkdir build && cd build && cmake ..
make
```

## Use dump\_flash.py
The project includes a sample script to dump a chip from a serial endpoint to a file on disk. Warning: the current implementation is quite slow (~7 hours per dump).
```bash
usage: dump_flash.py [-h] [-s START_PAGE] [-n NUM_PAGES] [-p PAGE_SIZE] [-x OOB_SIZE] [-f FILENAME] [-d DEVNAME] [-b BAUDRATE]
```

## TODO: `pio` version
There's a PIO version I'm attempting to build, however that's going to be used once a working version of the repo.

## Connect to Dumper Manually
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
