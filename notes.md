# Notes

## To-Do

 - [x] bump to latest TinyUSB release
 - [ ] figure out periodic distortion bug (occurs every ~24.5 seconds, regardless of sampling rate. happens simultaneously on multiple mics)
 - [ ] substitute bit-shift interleave for byte-to-byte lookup table
     + [ ] profile the two approaches
     + [ ] this could be done without CPU via DMA — see [this thread](https://forums.raspberrypi.com/viewtopic.php?t=338287#p2025806)

## Usage

### Manual Building

To build the UF2 binaries for the Pico microphone, just run `build.sh` from the repo's root directory. Connect a Pico (by USB) while holding the "BOOTSEL" button and drag the relevant UF2 (e.g. `build/examples/usb_microphone/usb_microphone.uf2`) to the Pico drive to flash the program.

### Debugging

There's a bunch of setup in `.vscode` and `pico-microphone.code-workspace`. That setup more-or-less follows these Digi-Key tutorials:
 - [Raspberry Pi Pico and RP2040 — Part 1: Blink and VS Code](https://www.digikey.com/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-1-blink-and-vs-code/7102fb8bca95452e9df6150f39ae8422)
 - [Raspberry Pi Pico and RP2040 — Part 2: Debugging with VS Code](https://www.digikey.com/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-2-debugging-with-vs-code/470abc7efb07432b82c95f6f67f184c0)

One unexpected necessary modification was modifying one of the "interface" configuration files for OpenOCD. The suggested `interface/picoprobe.cfg` was replaced with `interface/bunny.cfg`:

```
adapter driver cmsis-dap
adapter speed 5000
```

To debug, follow these steps:

 1. Connect the hardware. USB to PicoProbe (Pico with PicoProbe firmware loaded); PicoProbe to Pico microphone: POWER, SWDIO/SWDCLK, & UART TX/RX.
 2. Open VSCode and the `pico-microphone` workspace.
 3. Add breakpoints as desired.
 4. Build the target (`[USB Microphone]`).
 5. Run the debugger (press `F5`).
 6. Once running, plug in the Pico microphone to use as USB device.

_Note: To build and flash normally, first disconnect the PicoProbe's USB connection._
