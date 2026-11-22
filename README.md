# PiSD2IEC

sd2iec for a bare-metal **Raspberry Pi Zero 2 W** (AArch64, plain C), with the
**Pi1541 HAT option B** (CBMretro vertical HAT: 7406, OLED, buzzer).

Tested with SIDPlay64 on an Ultimate64 and on a MiSTer (C64 core, External IEC
through a SNAC2IEC adapter **with** its level shifter fitted).

Other Raspberry Pi models have not been tested.

## Motivation

I originally started this project because I wanted to play SID files on the Commodore 64 core of my MiSTer FPGA.

The project was inspired by Stephen White's excellent [Pi1541](https://github.com/pi1541/Pi1541), but I wanted to take things a step further by combining SD2IEC functionality with cycle-exact 1541 disk drive emulation in a single solution.

What started as a simple way to play SID music gradually evolved into PiSD2IEC.

## Layout

```
src/hal/     hardware layer: startup, MMU, timer, GPIO, UART, mailbox, HAT, IEC lines, I2C, OLED
src/lib/     printf, string functions, lock-free queue
src/         main.c (core 0), ui_core.c (core 2), cores.h
src/sd2iec/  sd2iec (unchanged) + rpi/: the Pi layer
```

## Division of work over the cores

| Core | Job | File |
|------|-----|------|
| 0 | system: startup, console (with sd2iec's debug output), reboot (`r`) | `src/main.c` |
| 1 | sd2iec: IEC bus, DOS, FAT and SD card, without interrupts | `src/sd2iec/` |
| 2 | ui: LEDs, buttons, buzzer, the OLED display (SSD1306, I2C0 on pins 27/28) and the SD card browser | `src/ui_core.c` |
| 3 | sd2iec tick (100 Hz) and ATN watch: what interrupts do on AVR/LPC | `src/sd2iec/rpi/system.c` |

- All cores run in EL2, where the firmware starts them. Core 0 starts through
  `src/hal/start.S`, cores 1-3 through the firmware's spin table
  (`src/hal/smp.c`, `src/hal/smp_entry.S`). Each core has its own 16 KB stack,
  MMU and caches on (same tables) and the same exception vectors.
- After a reset the cache can still hold lines from the previous run, which the
  CPU hits even with caches "off". So core 0 first invalidates L2 and L1 and
  then clears `.bss` again (`src/hal/boot0.S`); cores 1-3 invalidate their own
  L1 as their very first step (`src/hal/smp_entry.S`).
- Only core 0 writes to the console. The other cores send messages through
  lock-free queues with one writer and one reader (`src/lib/ring.h`). The IEC
  core never waits: if a queue is full the message is dropped and core 0
  reports it.
- Core 0 sets up all GPIO pins before the other cores start.

## sd2iec

`src/sd2iec/` contains unchanged files from sd2iec (GPL v2, see
`src/sd2iec/COPYING`). `src/sd2iec/rpi/` is the architecture layer for the Pi,
next to sd2iec's own `avr/` and `lpc17xx/`: SD card through the EMMC controller,
timer, LED, IEC pins, "EEPROM" in RAM and the configuration (`autoconf.h`).

sd2iec runs on core 1 with the IEC bus, without interrupts. What interrupts do
on AVR/LPC is done by core 3 (`rpi/system.c`): the 100 Hz tick and the ATN
interrupt (DATA low as soon as ATN goes low). sd2iec's debug output goes to the
console on core 0 through a queue (`rpi/uart.c`), and its display hooks go to
the OLED (`rpi/display.c`).

**JiffyDOS** works: `src/sd2iec/lpc17xx/llfl-jiffydos.c` is sd2iec's own
JiffyDOS code for the LPC1768, unchanged. It is written against a small
timing layer (`lpc17xx/llfl-common.h`: wait for a line and note the time, set
or read a line at a time offset), which `rpi/llfl-common.c` implements on the
Pi with the ARM counter and busy loops on core 1. With a JiffyDOS kernal in the
C64, LOAD and SAVE use the fast protocol automatically. The other fast loaders
(Final Cartridge III, Action Replay, Epyx FastLoad etc.) use the same layer
but are not enabled yet; DolphinDOS needs a parallel cable.

## Building

Needs the Arm GNU toolchain for `aarch64-none-elf` (e.g. 15.2) on the `PATH`.

```bash
make
```

`make sdcard` puts `config.txt` and `kernel8.img` in `build/sdcard/`.

## SD card (FAT32)

Put on the SD card:

1. `config.txt` and `kernel8.img` from `build/sdcard/`
2. `bootcode.bin`, `start.elf` and `fixup.dat` from the `boot/` folder of
   https://github.com/raspberrypi/firmware

## Fast development with imgrecv (without swapping SD cards)

Put the serial bootloader imgrecv on the SD card once as `kernel8.img`. Build it
in `imgrecv/raspi`:

```bash
make rpi3 ARM64CHAIN=aarch64-none-elf- LDFLAGS=-nostdlib
```

`LDFLAGS=-nostdlib` is needed because `ld` doesn't know the `-nostartfiles`
option from its Makefile. Copy the resulting `kernel8.img` to the SD card along
with `boot/config.txt` and the firmware files.

Then, with the USB serial adapter connected:

```bash
make upload
```

This builds PiSD2IEC and starts raspbootcom. Power on or reset the Pi; as soon as
imgrecv announces itself, raspbootcom sends the image and stays open as the
console. Quit with **Ctrl-C**. After a reset of the Pi, raspbootcom sends the
image again automatically.

A different port or path: `make upload PORT=/dev/ttyUSB1
RASPBOOTCOM=/path/to/raspbootcom`. Use a raspbootcom build that writes in
blocking mode; older builds abort with EAGAIN on larger images.

## Serial console

USB serial adapter, **3.3 V only**, 115200 baud, 8N1:

| Pi pin | Function   | Adapter |
|--------|------------|---------|
| 6      | GND        | GND     |
| 8      | GPIO14 TX  | RX      |
| 10     | GPIO15 RX  | TX      |

If the HAT covers the header, connect the wires to the pins on the underside of
the Pi.

## Usage

After startup sd2iec runs as device 8. On the C64 everything sd2iec supports
works: `LOAD"$",8`, `LOAD"NAME",8`, `OPEN 15,8,15,"CD:DIR"` and so on. With a
JiffyDOS kernal in the C64, loading is several times faster; other fast
loaders are not enabled yet.

sd2iec lists directories in the order of the entries on the FAT. For
alphabetical listings, sort the card with `fatsort -c` (card unmounted).

**OLED:** 16 x 8 characters.

```
 PiSD2IEC 0.4  #8    version and device number

/C64Music            current directory ("/" = root)

Load:                last file ("Save:" when writing)
Commando

00, OK,00,00         error channel
```

With the C64 character ROM in the root of the SD card (`CHARGEN`, 4 KB, as
VICE names it, or `CHARGEN.BIN`/`CHARGEN.ROM`; not included) the OLED uses
the C64 font, and PETSCII graphics in names show as on the C64. Without it a
built-in 5x7 font is used. DOS commands and timing details are on the
console.

This comes from sd2iec's own display hooks (`CONFIG_REMOTE_DISPLAY`), which
`src/sd2iec/rpi/display.c` passes on to the OLED.

**Console** (115200 baud), next to sd2iec's own debug output:

| Key | Function |
|---|---|
| `s` | bus state (ATN, CLK, DATA, SRQ, RESET) and what the Pi itself pulls low |
| `t` | bus trace: the last 300 changes on the IEC bus, with timing |
| `i` | system information and the cores |
| `b` | beep |
| `r` | cold reboot |
| `d` | 1541 mode: state of the emulated drive (registers, VIAs, head, RAM, late cycles) |
| `w` | 1541 mode: snapshot of the drive the first time it reaches an address |
| `l` | 1541 mode: log the registers at up to 4 addresses (e.g. a loader's receive loop) |
| `p` | 1541 mode: print that log |
| `x` | bus trace also during the 1541 emulation (off by default) |

**Buttons: browse the SD card.** Up, Down or Select opens the browser on the
OLED: the current directory, sorted, directories first. Files show under
their full long name (sd2iec itself uses the 8.3 name, e.g. `GHOSTN~1.D64`,
when a name is longer than 16 characters); a name that does not fit scrolls
to the left and back on the selected line.

| Button | In the browser |
|---|---|
| Up / Down | move the cursor (held down: repeats) |
| Select | directory or disk image (D71/D81/DNP/M2I): enter it, as `CD:NAME` would; **D64: run it in the 1541 emulation** (below); file: make it the file that `LOAD"*",8,1` loads |
| Exit | one level up or out of a disk image (`CD:_`); at the root: back to the status screen |
| Add | on a file: use it as the ROM of the 1541 emulation (below) |

After 30 seconds without a button the status screen returns. The browser
and the C64 share the same current directory: a `CD` from the C64 shows up in
the browser, and a directory entered with the buttons is what `LOAD"$",8`
lists. The work is done by sd2iec on core 1 while the bus is idle, through
sd2iec's hook for external displays (`src/sd2iec/rpi/browser.c`). The reset
button on the HAT is the IEC reset line, not a Pi button; reboot the Pi with
`r` on the console.

**IEC reset** (C64 reset, or the HAT's reset button): in sd2iec mode all files
the C64 had open are closed and the error channel shows the power-on message
(73); the current directory stays. In 1541 mode the emulated drive is held in
reset while the line is low and then starts over, like a real 1541; the disk
stays in.

If the ACT LED blinks fast, an exception occurred; the register dump is on the
console.

## 1541 emulation

Selecting a D64 in the browser turns the Pi into a real 1541: a cycle-exact
6502 with the two 6522 VIAs runs the original 1541 DOS ROM on core 1, in
place of sd2iec, until Exit. Everything the C64 sends to the drive runs on
the emulated drive exactly as on the real one, including drive code that
fast loaders upload. With a JiffyDOS drive ROM it is a JiffyDOS 1541.

| Part | File |
|---|---|
| NMOS 6502: one bus access per clock cycle including dummy accesses, IRQ timing, all undocumented opcodes | `src/emu/cpu6502_impl.h` |
| 6522 VIA: timers (datasheet timing), ports, CA/CB lines, interrupts | `src/emu/via6522.h` |
| The 1541: memory map, VIA1 on the IEC bus with the hardware ATN acknowledge, VIA2 with the head (bit rate per zone, SYNC, BYTE READY to the SO pin, stepper by half tracks) | `src/emu/drive1541.c` |
| D64 to GCR bit streams with the 1541 track layout | `src/emu/disk1541.c` |
| On the Pi: loading, real time against the ARM counter, GPIO | `src/sd2iec/rpi/emu1541.c` |

Needs the 1541 DOS ROM (16 KB, not included) in the root of the SD card as
`DOS1541` (or `DOS1541.BIN`, `1541.ROM`, `1541.BIN`). **Another drive ROM**
(1541-II, JiffyDOS, ...): put it on the card, select it in the browser and
press Add. A 16 KB ROM is used as it is; an 8 KB ROM is the upper chip only
($E000-$FFFF, as JiffyDOS often comes) and gets the lower half from
`DOS1541`. The choice is kept in `PISD2IEC.CFG` in the root and shown on the
1541 screen; Add on `DOS1541` goes back to the standard ROM. The OLED shows the
ROM, the disk, the head position, motor and LED. Console `d` shows `late`:
cycles that started after their moment (should stay 0).

**Writing** (SAVE, highscores, formatting): the emulated head writes GCR bits
onto the track in memory, exactly as the DOS (or any drive code) produces
them. The tracks that were written to are decoded back into sectors (SYNC,
header, data block, both checksums) and only the sectors that changed are
written to the D64 file on the SD card, 256 bytes each: after the motor has
been off for 2 seconds, before a disk change and at Exit. While that happens
the OLED shows "Saving to SD...". The D64 file itself is changed, so keep a
copy of images you want to keep untouched.

**Changing disks** (multi-disk games and demos): while the emulation runs,
Up/Down choose another D64 from the same directory and Select puts it in the
drive. As with a real disk change, the drive is empty for half a second (no
data, and the write protect sensor sees light, which is how the DOS and
loaders notice the change) before the new disk is in.

Tests on the PC (`make test-emu`, the test data is not included): Klaus
Dormann's 6502 functional test, Tom Harte's per-cycle SingleStepTests, VIA
timing, and a simulated C64 that reads the error channel, the directory
and a file of a D64 through the emulated drive over the serial bus, then
SAVEs a file and checks that it ends up in the D64 byte for byte (the D64
file on disk is not changed by the test).

## License

PiSD2IEC is an independent project based on
[sd2iec](https://www.sd2iec.de/) by Ingo Korb. It is not affiliated with or
endorsed by the sd2iec authors; please report PiSD2IEC issues here, not to
sd2iec.

PiSD2IEC's own files are free software under the GNU General Public License,
version 2 or (at your option) any later version; each file says so with an
[SPDX](https://spdx.org/licenses/) line at the top
(`SPDX-License-Identifier: GPL-2.0-or-later`). PiSD2IEC includes sd2iec,
which is GPL version 2 only, and a few files in `src/sd2iec/rpi/` are based
on sd2iec code (`GPL-2.0-only`). The firmware as a whole
(`kernel8.img`) is therefore distributed under **GPL version 2**. The full
text is in [COPYING](COPYING).

PiSD2IEC comes with **no warranty** (see sections 11 and 12 of
[COPYING](COPYING)). You use it at your own risk: connecting it to your
computer's serial bus, and letting the 1541 emulation write to D64 files
(keep copies of your images).

Third-party components (sd2iec, FatFs, newlib, libgcc, the 5x7 font) and
their notices are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
