# Platform and peripherals

The Pocket SCION is built around an RP2040 with external flash, an I2S audio
DAC, a biofeedback pulse input, five buttons, DIN MIDI output, USB, and nine
serial RGB pixels behind five concentric translucent PCB-art rings.

This map was recovered from the original v1.0.1 UF2 through static analysis and
then checked against the replacement firmware on hardware.

## Pin map

| GPIO | Direction / peripheral | Function | Confidence |
|---:|---|---|---|
| 0 | SIO input, both-edge IRQ | Biofeedback oscillator pulse input | Confirmed |
| 1 | PIO1 SM0 output | Nine WS2812-compatible RGB pixels | Confirmed |
| 12 | PIO0 output | I2S serial audio data | Confirmed |
| 13 | PIO0 output | I2S bit clock | Confirmed |
| 14 | PIO0 output | I2S left/right clock | Confirmed |
| 16 | UART0 TX | 31,250-baud Type-A TRS MIDI | Confirmed |
| 17 | Active-low input | Volume + | Hardware confirmed |
| 18 | Active-low input | Volume − | Hardware confirmed |
| 19 | Active-low input | Instrument / Shift | Hardware confirmed |
| 20 | Active-low input | Sensitivity − | Hardware confirmed |
| 21 | Active-low input | Sensitivity + | Hardware confirmed |
| 27 | Active-low input | Auxiliary signal, purpose unresolved | Pin confirmed |
| 29 | Active-high input | Auxiliary signal, purpose unresolved | Pin confirmed |

GPIO27 and GPIO29 are initialized safely but are not used by this firmware.

## Audio

The replacement runs at 48 kHz signed 16-bit stereo. PIO0 generates Philips
I2S on GPIO12–14. Three chained 256-frame DMA buffers keep rendering and output
decoupled. The RP2040 system clock is set to 153.6 MHz so the 3.072 MHz bit
clock and 48 kHz word clock divide exactly.

## Sensor

GPIO0 receives a digital pulse train from the plant/biofeedback front end.
Both rising and falling edges are timestamped. Intervals at or below 2,500 µs
are rejected, and ten accepted intervals make one statistics window.

The firmware calculates:

- minimum and maximum interval;
- mean and sample variance;
- standard deviation;
- range and a recovered range-fault threshold;
- normalized proximity and expression;
- the recovered sensitivity-trigger inequality.

Normal synthesis uses these statistics musically. Raw mode instead DMA-samples
the GPIO status at 48 kHz and sends the actual pulse train to the DAC, making
pressure changes audible as pitch changes.

## RGB display

The LEDs form one 800 kbit/s GRB chain on PIO1:

| Chain index | Physical ring |
|---:|---:|
| 0 | 4, outer side A |
| 1 | 3, side A |
| 2 | 2, side A |
| 3 | 1, side A |
| 4 | 0, centre |
| 5 | 1, side B |
| 6 | 2, side B |
| 7 | 3, side B |
| 8 | 4, outer side B |

In live synthesis, polyphony selects the illuminated radius. Brightness follows
the real PRA32-U amp envelope and a restrained amount of its live LFO. Actual
ratchet firings add short white accents.

## Unused application peripherals

No application use was found for SPI, I2C, UART1, ADC, PWM, or RTC. Generic SDK
code may still contain support for these peripherals.
