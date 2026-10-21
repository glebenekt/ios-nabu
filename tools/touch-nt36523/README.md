# NT36523 protocol prototype

This directory contains the **offline** part of the nabu touch port. It has no
XNU dependencies and does not touch hardware.

## Why not UniXNU's NativeNovatekTouch?

UniXNU's driver targets the **NT36532E** with the THP 40x60 matrix protocol.
The nabu vendor driver (`refs/nt36523/nt36xxx.c`) uses the **NT36523 point-data**
protocol:

- 65-byte point frame
- 10 fingers, 6 bytes per finger
- x = `(b1 << 4) | (b3 >> 4)`
- y = `(b2 << 4) | (b3 & 0x0f)`
- weight = `b4`
- pressure = `b5`, with high bits for the first two fingers in frame[62]/[63]
- 1600x2560 default axes

The THP matrix core is therefore the wrong reference for nabu.

## What is implemented

- Novatek page/address framing (`0xff`, `addr >> 15`, `addr >> 7`)
- SPI read/write framing (`addr & 0x7f`, `| 0x80`)
- trim-id match table for NT36523 (`... 0x23 0x65 0x03`)
- FWINFO parsing (39 bytes, 1600x2560 fallback)
- point-frame parser for 10 fingers
- host tests with a fake bus

## Run

```sh
make
./test_nt36523
```

## Still hardware-dependent

- GENI SPI engine init, clock/DFS and pin mux
- reset / active GPIOs and rail enable
- SMMU mapping and DMA/FIFO choice
- IRQ vs polling
- panel orientation and HID coordinate mapping
- whether the stock firmware is already running (no download needed) or a
  firmware image must be supplied

Those belong in the kernel probe, gated behind `nabutouch=N`, and must be
verified on the device.
