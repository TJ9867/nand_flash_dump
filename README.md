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
