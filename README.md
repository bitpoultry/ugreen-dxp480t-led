# ugreen-dxp480t-led

Make the power LED of a **UGREEN NASync DXP480T / DXP480T Plus** behave like it
does under UGOS when the box runs Proxmox VE (or any Debian-based OS).

Without UGOS the LED blinks white forever: that is the MCU's power-on default,
and nothing on a third-party OS ever tells it the system has booted.

| UGOS state (DXP480T Plus manual) | Pattern | This project |
| --- | --- | --- |
| Working properly | white, steady | set when the service starts |
| Powering off | white, flashing every 0.5 s | set when the service stops (shutdown/reboot) |
| Device error | orange, slow flashing (1 s) | set while a ZFS pool is not healthy or an NVMe reports a SMART failure / critical warning |
| Booting | white flashing | MCU firmware default until the service starts |

The DXP480T has no LAN or drive LEDs, only the power LED (white + orange).

Tested on: DXP480T Plus, BIOS 5.26, MCU firmware v1.0.6, Proxmox VE 9.2,
kernel 7.0.14-25-pve (10.11.2026).

## Install

```sh
apt install -y i2c-tools smartmontools
install -m 755 ugreen-power-led /usr/local/sbin/
install -m 644 ugreen-power-led.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now ugreen-power-led.service
```

Run it on the Proxmox host, not in a VM: the SMBus is on the host.

## Manual control

```sh
ugreen-power-led status                 # read back MCU registers
ugreen-power-led steady white|orange
ugreen-power-led blink  white|orange [ON_MS OFF_MS]
ugreen-power-led breath                 # white only (UGOS "sleeping")
ugreen-power-led off
ugreen-power-led check                  # health findings, no LED change
```

If you change the LED by hand while the service runs, `systemctl restart
ugreen-power-led` puts it back (the monitor only writes on a state change).

## How it works

See [docs/FINDINGS.md](docs/FINDINGS.md) for the full research: the register
map taken from UGREEN's own GPL driver, read-back evidence from real hardware,
and a survey of the other projects.

No kernel module is needed. The script replays the exact register sequences of
UGREEN's driver (`leds-mcu-n76e003.c`, copied in [vendor/](vendor/)) through
`i2cset`, with a lock so manual commands and the monitor never interleave.
