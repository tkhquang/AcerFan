# AcerFan

A small tray app that runs the fans of an Acer laptop from the CPU and GPU temperatures, through the
same firmware interface Acer's own PredatorSense, NitroSense and ConceptD Palette use.

The goal is **no thermal throttling, not low temperatures**. Acer's Auto mode on some models caps the
fans well below their maximum: on a ConceptD 9 it stops at about 2,750 rpm while the CPU sits at its
90 °C throttle point, although the fans reach 6,100 rpm. AcerFan leaves the fans to Auto while the
chips are cool and takes over only when heat builds up.

> **Tested on:** Acer ConceptD 9 (CN917-71, i9-9980HK, RTX 2080) only. Other Acer laptops that ship
> PredatorSense, NitroSense or ConceptD Palette expose the same `AcerGamingFunction` interface and will
> probably work, but sensor and fan ids may differ. Reports are welcome.

## How it works

- Every 2 seconds AcerFan reads the CPU and GPU temperatures from the firmware.
- The curves act on each chip's **baseline**: the 25th percentile of the last 60 seconds. Under light,
  bursty work a laptop CPU flashes to 85-90 °C for a second and drops straight back. Fans cannot help
  with those flashes, only with heat that stays, and a flash does not move the baseline.
- Below the curves the firmware's own Auto mode runs the fans. Once a baseline enters its curve, both
  fans switch to a custom speed and follow whichever chip asks for more:

  | CPU baseline | < 80 °C | 80 °C | 84 °C | 87 °C | 89 °C+ |
  |---|---|---|---|---|---|
  | Fans | Auto | 40% | 60% | 80% | 100% |

  | GPU baseline | < 75 °C | 75 °C | 80 °C | 84 °C | 86 °C+ |
  |---|---|---|---|---|---|
  | Fans | Auto | 40% | 60% | 80% | 100% |

- Speed changes in steps (+10% / -5% per poll) and only falls once every recent request is lower.
  Control returns to Auto after both baselines stay cool (CPU ≤ 74 °C, GPU ≤ 70 °C) for 30 seconds.
- Every 10 seconds AcerFan checks that nothing else changed the fan mode (another fan app, resume from
  sleep) and re-applies its own.
- On exit, shutdown or an error the fans go back to firmware Auto. If temperatures cannot be read for
  10 seconds the fans go to 100%.

Everything above is configurable in `AcerFan.ini`, created next to the exe on first start.

## Tray menu

- Live temperatures and fan speeds
- Mode: **Temperature curve** (default), **Firmware Auto**, **Full speed**
- **Start with Windows**: registers a logon task that starts AcerFan elevated without a UAC prompt
- Edit settings / Reload settings / Open log
- Exit (fans back to firmware Auto)

The icon shows the hotter chip's temperature: grey while the firmware runs the fans, green / orange /
red for AcerFan's custom speed (below 60%, below 85%, 85% and up).

## Requirements

- Windows 10 or 11, x64
- Administrator rights: the firmware fan methods only answer elevated processes. The exe asks for them.
- Close other fan tools (PredatorSense, NitroSense, ConceptD Palette) or leave their fan setting on Auto,
  otherwise they fight over the fans.

Command line, for scripted installs: `AcerFan.exe --autostart on` / `--autostart off`.

## Build

Visual Studio 2022 (MSVC, C++23) and CMake 3.28+:

```
cmake --preset msvc
cmake --build --preset release
ctest --preset release
```

The exe is `build/msvc/Release/AcerFan.exe`, statically linked, no runtime to install.

## Firmware interface

`root\WMI` class `AcerGamingFunction`; every method takes a 32-bit `gmInput` and returns a 64-bit
`gmOutput` whose low byte is a status (0 = success).

| Purpose | Method | gmInput | Result |
|---|---|---|---|
| CPU / GPU temperature | `GetGamingSysInfo` | `0x0101` / `0x0A01` | °C in bits 23:8 |
| CPU / GPU fan speed | `GetGamingSysInfo` | `0x0201` / `0x0601` | rpm in bits 23:8 |
| Both fans Auto / Custom | `SetGamingFanBehavior` | `0x410009` / `0xC30009` | |
| Fan mode read-back | `GetGamingFanBehavior` | `0x9` | CPU bits 9:8, GPU bits 15:14 (1 Auto, 2 Max, 3 Custom) |
| Custom speed | `SetGamingFanSpeed` | `percent << 8 \| fan` (CPU 1, GPU 4) | |

The encodings match the ones the Linux `acer-wmi` driver documents; no code was taken from it.

## Disclaimer

AcerFan writes fan settings to the laptop's embedded controller, as Acer's own tools do. The CPU and GPU
still protect themselves (throttling, thermal shutdown) whatever the fans do. Use at your own risk.

## License

[MIT](LICENSE)
