# ios-nabu

**Apple iPadOS 16.6.1 on the Xiaomi Pad 5 (`nabu`).**

[中文](README.zh-CN.md)

This project ports Apple's open-source XNU kernel (xnu-8796, the iOS 16
generation) to the Xiaomi Pad 5 (`nabu`, Qualcomm SM8150-AC / Snapdragon 860).
On top of it runs the **unmodified iPadOS 16.6.1 (20G81) userland of the
iPad7,3**: launchd, the system daemons, backboardd and SpringBoard.

**Current state (2026-10-21):** XNU boots on hardware, the UFS root is
read/write, launchd / SpringBoard / backboardd start, and the display services
are registered. The iPad UI is not visible yet: backboardd submits one
`SwapEnd`, then powers the display off; there is no GPU renderer. Touch
(Novatek NT36523) and the USB gadget are still in bring-up.

> **Experimental bring-up, not a usable tablet OS.** Many things are missing or
> stubbed, and Apple's security model is mostly disabled. The system is
> installed into `userdata`, so it erases Android user data.

## What works

| Area | State |
| --- | --- |
| Boot | Unlocked stock ABL → `fastboot boot` our shim → XNU; boot partitions are not flashed |
| CPU | 1 core stable; 8 cores reach userspace but occasionally reset in the loopattach race, so `cpus=1` is the default |
| Storage | Own UFS host driver; the iPadOS root is a read/write HFS+ volume in `userdata` |
| iPadOS userland | Real iPad7,3 16.6.1 (20G81) root and shared cache; launchd and system daemons run |
| SpringBoard | SpringBoard, backboardd and AKS calls start |
| Display services | `AppleCLCD`, `IOMobileFramebuffer` and `IOSurface` are registered; the 1600×2560 panel is on |
| Keys | PMIC PON / GPIO reads work and produce key events |
| AppleKeyStore | Selectors work; the keybag is present and AKS calls return |
| Logging | A 4 MiB persistent ring at the end of `userdata`, readable from TWRP |

## What does not work yet

| Area | State |
| --- | --- |
| iPad UI | backboardd submits one `SwapBegin`/`SwapEnd`, then `RequestPowerChange(0)` turns the display off; no continuous swap |
| GPU | No accelerator; only a CPU framebuffer/compositing path is being explored |
| Touch | Novatek NT36523 over SPI; protocol parser and host tests are done, the kernel probe is not |
| USB | USB0 GDSC/clocks/DWC3/femto are readable; `nabuusb=1` is stable, `nabuusb=2` resets after ~60–70 s; the host has not seen `1209:0001` |
| 8 cores | Reaches userspace, but occasional loopattach races/resets; not stable yet |
| Other | Wi-Fi, Bluetooth, audio, camera, sensors, GPS, SEP/Face ID are not implemented |

## Current bring-up focus

**Display.** The cheapest next step is `nabufb_force_on=1`: ignore
`RequestPowerChange(0)` during bring-up and log the first `SwapEnd` records
(mask/layer/surface/rect). If `NABU FB: swap` appears, the renderer is working
and only power/format/scale remain.

**USB.** The PHY is the remaining piece. `nabuusb=1` keeps the controller
readable; `nabuusb=2` runs femto init + gadget and resets. The next step is a
step-limited femto bisect with a synchronous log flush after every step.

**Touch.** The correct protocol is the NT36523 **65-byte point-data** protocol,
not the NT36532E THP matrix. The parser and host tests live in
`tools/touch-nt36523/`; GENI SPI, clocks, GPIO, SMMU/DMA and HID mapping still
need hardware validation.

## Boot flow

```text
ABL (unlocked stock Xiaomi bootloader)
  -> fastboot boot shim-boot-nabu.img          (temporary; boot is not flashed)
     -> shim: own MMU, reads the live FDT, loads the payload from the ramdisk
        -> XNU Mach-O + generated Apple Device Tree (ADT) + boot-args
           -> XNU -> UFS driver -> HFS+ root in userdata
              -> dyld + shared cache -> launchd -> daemons
                 -> backboardd + SpringBoard
```

There is no usable serial port on this board. The kernel console is drawn into
the ABL framebuffer at `0x9c000000` and also written to the persistent ring at
the end of `userdata`.

## Build

The XNU patch series lives in the external `darwin-xnu-build` tree and is built
on a macOS GitHub runner:

```sh
gh workflow run xnu.yml -R YOU/ios-nabu
gh run download RUN_ID -R YOU/ios-nabu -n xnu-kernel -D xnu/out
```

Build the iPadOS root on Linux with `apfs-fuse` and `hfsprogs`:

```sh
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ipados16-root.img 16G
sudo fsck.hfsplus -fy ipados16-root.img
sudo mount -t hfsplus -o loop,force ipados16-root.img /mnt/ipadroot
sudo python3 tools/nabu-root-tweaks.py /mnt/ipadroot
sudo umount /mnt/ipadroot
img2simg ipados16-root.img ipados16-root.simg
```

`nabu-root-tweaks.py` skips Setup, seeds the activation state, removes the
baseband daemon, disables the watchdogs that kill slow apps, and generates the
DataMigration/activation data from the root's actual `SystemVersion.plist`.

Pack and boot:

```sh
python3 tools/mkadt.py shim/out/nabu.adt --highmem
python3 tools/mkpayload.py xnu/out/kernel.development.nabu shim/out/nabu.adt shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 ufsrw=1 rootrw=1 launchd_unsecure_cache=1 nabuusb=1 ufslogsecs=1 nabufb_force_on=1 nabufb_noconsole=1 wdt=-1 wdt_test=no_user_checkin ignition_level=1'
shim/build.sh ../shim/out/payload.bin
fastboot -S 512M flash userdata ipados16-root.simg   # erases data
fastboot boot shim/out/shim-boot.img
```

Read the kernel log:

```sh
fastboot boot recovery/recovery.img        # TWRP, temporary
tools/nabu-log-pull.sh logs/boot.txt
tools/nabu-log.py logs/boot.txt | less
```

## Boot arguments

| Boot-arg | Effect |
| --- | --- |
| `rd=ufsroot0 ufsrw=1 rootrw=1` | Root on `userdata`, writable |
| `cpus=1` | Safe default; 8-core is experimental |
| `nabuusb=1` | Read-only USB state, no femto/gadget |
| `nabuusb=2` | Run femto init + gadget (currently resets) |
| `nabufb_force_on=1` | Ignore `RequestPowerChange(0)` |
| `nabufb_noconsole=1` | Stop drawing kernel text over the GUI |
| `ufslogsecs=1` | Flush the persistent ring every second |
| `wdt=-1 wdt_test=no_user_checkin` | Disable the Apple watchdog |

## Hardware

- SoC: SM8150-AC / Snapdragon 860, 6 GB RAM, UFS 3.1.
- Panel: 1600×2560; ABL framebuffer at `0x9c000000`.
- Touch: Novatek NT36523 over SPI; IRQ GPIO39, reset GPIO12.
- USB: DWC3 `0x0a600000`, QSCRATCH `0x0a6f8800`, femto PHY `0x088e2000`.
- PMIC: PM8150 / PM8150B / PM8150L.
- userdata: `/dev/block/sda31`, first LBA `0x288040`, last LBA `0x769e7fa`,
  4 KiB blocks; log ring starts at `0x769e3fa`.

## Repository layout

| Path | Contents |
| --- | --- |
| `shim/` | Bare-metal boot shim (C + asm), packed as an Android boot image |
| `tools/mkadt.py` | Generates the Apple Device Tree |
| `tools/mkpayload.py` | Packs kernel + ADT + boot-args |
| `tools/mkiosroot.sh` | Builds the HFS+ iPadOS root from IPSW images |
| `tools/nabu-root-tweaks.py` | Root bring-up changes |
| `tools/nabu-log-pull.sh`, `nabu-log.py` | Read and parse the persistent log |
| `tools/nabu-crash.py` | Symbolicate crash/panic reports |
| `tools/touch-nt36523/` | NT36523 protocol parser and host tests |
| `tools/qemu-nabu.py` | QEMU boot test |
| `NOTES.md` | Detailed engineering log: boots, crashes and hardware facts |

Not in git: partition backups, live device trees, recovery images, Apple
firmware/root images, and downloaded/generated build trees. They contain device
serials, calibration data, or Apple copyrighted files.

## Credits

Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs),
[BearSSL](https://bearssl.org),
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build),
[ipsw](https://github.com/blacktop/ipsw), Qualcomm/Xiaomi's published Linux
kernel sources, TWRP, and the ios-sweet / UniXNU / ios-nabu bring-up work.
iOS/iPadOS itself is Apple's and is not distributed here.
