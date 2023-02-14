# Notes

## To-Do

 - [x] bump to latest TinyUSB release
 - [x] figure out periodic distortion bug (occurs every ~24.5 seconds, regardless of sampling rate. happens simultaneously on multiple mics)
 - [ ] run mic with 48x decimation, then at 96 kHz sample rate
 - [ ] substitute bit-shift interleave for byte-to-byte lookup table
     + [ ] profile the two approaches
     + [ ] this could be done without CPU via DMA — see [this thread](https://forums.raspberrypi.com/viewtopic.php?t=338287#p2025806)
     + [ ] this should probably just be done via PIO registers

## Miscellaneous

### Performance Limits

Basic timing profiling shows that `usb_microphone_write` takes ~40us (as 4 channel device), and `pdm_microphone_read` requires ~300us per channel (with additional time needed for interleaving). It seems the upper limit for `pdm_microphone_read` runtime is ~590us, above which strange clicks and repetitions begin to distort the input.

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

## Known Issues

### Asynchronous USB Communication Bug

I spent quite a bit of time debugging a synchronization issue between the MCU and USB clocks. I'd like to document it for my own sanity and future self. The issue first presented as periodic distortion, occuring consistently every ~24.5 seconds. This distortion was insensitive to the sampling rate, # of channels, and seemingly to PDM sample buffer sizes.

Given the periodic nature of the bug, it seemed likely to stem from some timing mismatch. My first guess was that the PDM and USB interrupts were occuring at differing rates, one consistently falling just before or just after the other. After a great deal of trial and error (first with semaphore-based buffer access, and then with critical sections), I discovered that protecting the USB write from interruption traded the periodic distortion for a regular crackling noise (occuring ~0.1 to ~3 times a second). This seemed like progress, but I still lacked useable audio.

> An aside: I also tried protecting the PDM reads from interruption, but IIRC this crashed the hardware. My guess is that it had something to do with failing to clear overlapping interrupts in time, such that they accumulate and something overflows, but I don't really know.

Next, I suspected that PDM-PCM processing during DMA interrupts was causing slight delays in triggering subsequent interrupts (i.e. PDM sample skips which produce pops when passed through the PCM filter), so I decoupled the interrupts and DMA triggering, opting instead for two ping-ponging DMA channels (each chained to trigger the other). Each channel had an independent completion interrupts meant only for processing their PDM data and configuring their next write address (to a buffer two indices down).

Frustratingly, this restored the original issue — the pops were gone, but there was once again a bout of distortion every 25 seconds. Having spent multiple days debugging and testing theories, and now faced with original issue, I resigned myself to the reality that there is a fundamental sampling mismatch underlying the bug which can't be swept under the rug, so to speak. As long as my data is produced with the MCU's clock and polled by the USB's clock, the MCU's CPU will be forced to intermediate the two clock frequencies, which (without resampling filters) means dropping samples somewhere or another.

After resigning myself to this fact, my focus became extending the period between distortion events. My first idea was to understand and lower the USB polling rate. At 2ms, unexpected crackles were reintroduced, and at lower polling rates, the MCU altogether crashed (in fact, my laptop fully cut-off two or three times). I abandoned the idea and refocused on extending buffers to increase the distortion period.

To avoid crackling, the PDM-to-PCM processing has to occur on as many consecutive samples as possible (before a skip). However, it needn't occur on the DMA clock — instead I tried performing the PDM processing at USB poll-time, which completely decoupled the DMA-based writing from the processing and transmitting via USB. The DMA interrupts remained solely to track the current write indices of the two ping-ponging channels.

The final step was to coordinate the USB-read index around the DMA-write index. If my understanding were correct and the two clocks are slightly out-of-time, then whenever the read-index approaches the write-index we'd need to jump over it (effectively repeating or skipping an entire raw PDM sample buffer's worth of samples). The upside is that as the PDM sample buffer increases in length, the time it takes for two the indices to coincide increases, and the cracks are relegated to a single momentary pop at a much lower frequency. This is the current implementation — at the cost of 8x extra sample buffers, a pop occurs around once every four minutes (and can be less if we use more memory).

I am curious as to how real USB microphones address these issues. Are there resampling filters? Do they operate in a synchronous mode, letting the MCU clock drive the USB polling? Maybe analog (i.e. non-PDM) microphones don't sound as bad when they skip a sample and we can sweep it under the rug. I don't know, but for now our large sample buffers will have to do.
