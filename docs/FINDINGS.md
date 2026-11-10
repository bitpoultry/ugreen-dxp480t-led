# Findings: DXP480T Plus power LED

Research date: 10.11.2026. Hardware: UGREEN NASync DXP480T Plus,
DMI `UGREEN / DXP480T Plus`, BIOS 5.26, Proxmox VE 9.2, kernel 7.0.14-25-pve.

## 1. Summary

- The LED is driven by a **Nuvoton N76E003 MCU** at **I2C address 0x26** on the
  **Intel I801 SMBus** (`i2c-0` on the test unit; the bus number can differ between
  units and boots, so detect it by name). The vendor driver also probes 0x31.
- It is a different controller from the `0x3a` chip used by the HDD models,
  which is why `miskcoo/ugreen_leds_controller` does not support the 480T.
- **UGREEN has published the driver source** under GPL in
  [`ugreen-opensource/kernel-6.12`](https://github.com/ugreen-opensource/kernel-6.12/tree/c75c5abd60dd45f45966720293ce1ee505fb8615/drivers/ugreen/dxp480t)
  (`drivers/ugreen/dxp480t/leds-mcu-n76e003.c`, first committed 03.04.2026).
  That file is the authoritative register map; the community commands in
  miskcoo issue #6 work, but their comments name the registers wrongly.
- The driver's only action on load (`mcu_init`) is: both modes steady, orange
  off, white on. That is what turns the blinking LED steady under UGOS.
  Everything else (shutdown blink, error blink) is UGOS userspace toggling the
  LED class triggers.

## 2. Register map (from the vendor driver, verified on hardware)

| Reg | Name in driver | Meaning |
| --- | --- | --- |
| `0x5A`,`0x5B` | `MCU_CHIP_ID` | read `0xA5`,`0xB5` = signature (`0xA5B5`) |
| `0x5D` | `MCU_FW_VER` | firmware, printed as `v1.0.<n>` (test unit: 6) |
| `0xA0` | `DATA_SET_REG` | write bitmask: turn channel(s) **on** |
| `0xB1` | `DATA_CLS_REG` | write bitmask: turn channel(s) **off** |
| `0x50` | `LED_MODE_BASE_REG` | white channel mode: 0 steady, 1 timer (blink), 2 breath |
| `0x51` | `LED_MODE_BASE_REG+1` | orange channel mode (breath is white-only in the driver) |
| `0xC0`/`0xC1` | `DELAY_ON_BASE_REG` | white on-time in ms, u16 little endian |
| `0xD0`/`0xD1` | `DELAY_OFF_BASE_REG` | white off-time in ms |
| `0xC2`/`0xC3`, `0xD2`/`0xD3` | (+2) | orange on/off time |
| `0xE0`,`0xE1` | `LED_BRIGHTNESS_BASE_REG` | PWM level 2..254 (driver writes both) |
| `0x60`, `0x72` | `DELAY_*_BREATH_BASE_REG` | defined, never written by the driver |

Bitmask for `0xA0`/`0xB1` on the power LED (`led_bit = 0`): `0x01` white,
`0x02` orange, `0x03` both. The driver has commented-out `disk1` (bit 4) and
`disk2` (bit 2) entries; those belong to the DH2600 and do not exist on the 480T.

Timing: the driver waits 2 ms after a set/clear write and 10 ms after any other
write. The script waits a little longer.

### Correcting the comments in miskcoo issue #6

The sequences posted there work, but the labels are guesses:

| Posted | Actually |
| --- | --- |
| `0xa0 1` "red off" | **set white on** |
| `0xa0 2` "white off" | **set orange on** |
| `0xb1 1` "red on" | **clear white** (orange stays → looks like "red on") |
| `0xb1 2` "white on" | **clear orange** (white stays) |
| `0x50 1` "fast flash" | white channel timer mode, default 500/500 ms |
| `0x51 1` "slow flash" | orange channel timer mode, default 500/1000 ms |
| `0x50 2` "breathing" | white breath mode (correct) |

"Red" is the orange channel. The "if both are on, the first one wins"
observation follows from the set/clear semantics.

## 3. Read-back evidence (new)

Earlier work (BearHero520) says the MCU lacks reliable read-back. On firmware
v1.0.6 these registers **do** read back:

- Untouched after boot (Proxmox, never initialised):
  `0x50=0x01` (white blinking), `0x51=0x00`, `0xC0/C1=0x01F4` (500 ms),
  `0xD0/D1=0x01F4` (500 ms), `0xC2/C3=0x01F4`, `0xD2/D3=0x03E8` (1000 ms),
  `0xE0=0xE1=0xFF`, `0x60=0x72=0x00`. `0xA0` reads `0x00`, `0xB1` `0x03`
  (these are write-strobes; their read value means nothing).
- After `blink orange 1000 1000`: `0x51=1`, `0xD2/D3=1000`, `0xC2/C3=1000`.
- After `steady white`: `0x50=0`, `0x51=0`, `0xE0=0xE1=0xFE` (254).

So mode, timing and PWM registers are readable; the on/off output state is not.
The firmware default (white timer 500/500) is exactly UGOS's "powering off"
pattern, which is why the box looks like it is forever shutting down.

## 4. UGOS behaviour (official)

DXP480T Plus user manual ("Indicator & Buzzer"):

| LED | Colour | Status | Meaning |
| --- | --- | --- | --- |
| Power | white | static | working properly |
| Power | white | flashing (once per 0.5 s) | powering off |
| Power | orange | slow flashing (once per 1 s) | device error |

The general UGOS LED page (support.ugnas.com article 499) also lists white
breathing = system sleeping; the 480T manual does not mention it.

Buzzer (not implemented here): short beep on power on / reset, a beep every
3 s on drive error, every 2 s on system error. The vendor beeper driver is
`ug-beeper-dxp480t.c` in the same directory.

## 5. What counts as "device error" here

UGOS's exact rules are closed source. This project uses:

- `zpool status -x` is anything but "all pools are healthy" / "no pools".
  Encrypted datasets whose keys are not loaded are not an error; the pool stays ONLINE.
- Any NVMe where `smartctl -H` reports `passed=false` or a non-zero
  `critical_warning` (covers spare, temperature, reliability, read-only and
  volatile-memory-backup warnings). Checked every 5 minutes; ZFS every minute.

## 6. Other projects surveyed

| Project | 480T status |
| --- | --- |
| [miskcoo/ugreen_leds_controller](https://github.com/miskcoo/ugreen_leds_controller) | 480T not supported (0x3a protocol only); [issue #6](https://github.com/miskcoo/ugreen_leds_controller/issues/6) holds the community i2cset recipe, boot/shutdown scripts, and the "bus number can change" tip |
| [BearHero520/LLLED_FPK / UGreen-Control](https://github.com/BearHero520/LLLED_FPK), [UGREEN-NAS-Hardware](https://github.com/BearHero520/UGREEN-NAS-Hardware) | fnOS app; "limited support" for 480T, power LED only, and by default only through the vendor kernel module. Its docs pin the vendor source hashes and an `--n76-direct` mode. Also has a 480T fan map (IT8613) |
| [smashah/ugreen-dxp-led-cli](https://github.com/smashah/ugreen-dxp-led-cli) | DXP4800 Plus only (0x3a) |
| [pjrobot/ugreen-leds-truenas](https://github.com/pjrobot/ugreen-leds-truenas) | UGOS-like behaviour for 0x3a models; not 480T |
| v64.tech forum, fnnas.com forum | copies of the issue #6 scripts |

## 7. Alternative: build the vendor module

`leds-mcu-n76e003.c` + `ledtrig-normal-n76e003.c` + `ledtrig-breath-n76e003.c`
could be built out-of-tree (DKMS) against `proxmox-headers`, giving
`/sys/class/leds/power` with `timer`/`breath`/`normal` triggers. Not chosen:
it needs a rebuild for every PVE kernel, relies on legacy I2C `.detect`
probing (writes to 0x31 too), and gains nothing the userspace script lacks.

## 8. Provenance

`vendor/` holds unmodified copies of the GPL-2.0-or-later vendor files from
commit `c75c5abd60dd45f45966720293ce1ee505fb8615`:

```
352bd2d58053e57122eaad4fe9e1c671e9447eb1c8937f87951665a804f4ed7a  leds-mcu-n76e003.c
d3da0de5fe25914ce507ffd34ba6b9ae25392f97bc9da964bfc4a84fb850b29f  ledtrig-breath-n76e003.c
```
