# Changelog

All notable changes to AcerFan will be documented in this file.

## [0.2.0] - Fewer fan cycles

- Fewer short fan cycles: the baseline window grows from 60 to 120 seconds, so a burst of work shorter than about 90 seconds no longer spins the fans up.
- No full fan speed right after logon: AcerFan takes over only after one full baseline window of readings. This replaces the `min_samples` setting.
- `AcerFan.log` gets a status line every 60 seconds instead of 10, so it keeps about ten days of history.
- An existing `AcerFan.ini` keeps its old values. For the new defaults, set `baseline_s = 120` and `status_every_s = 60`, or delete the file and restart AcerFan.

## [0.1.0] - First release

- Runs both fans from the CPU and GPU temperatures through Acer's firmware interface. The Auto mode of the firmware keeps the fans while the chips stay cool.
- Acts on the baseline temperature of each chip, the 25th percentile of the last 60 seconds, so a one-second boost spike does not spin the fans up.
- Separate CPU and GPU curves just below the throttle points, editable in `AcerFan.ini`.
- A tray icon with the temperature of the hotter chip, and the modes Temperature curve, Firmware Auto and Full speed.
- Start with Windows through a logon task, with no UAC prompt at logon.
- Re-applies its fan setting after sleep or a change by another app, and runs the fans at 100% when the temperatures stay unreadable.

[0.2.0]: https://github.com/tkhquang/AcerFan/releases/tag/v0.2.0
[0.1.0]: https://github.com/tkhquang/AcerFan/releases/tag/v0.1.0
