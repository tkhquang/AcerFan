## First release

- Runs both fans from the CPU and GPU temperatures through Acer's firmware interface, and leaves them to the firmware's Auto mode while the chips are cool.
- Acts on each chip's baseline temperature (the 25th percentile of the last 60 seconds), so one-second boost spikes do not spin the fans up.
- Separate CPU and GPU curves set just below the throttle points, in `AcerFan.ini`.
- Tray icon with the hotter chip's temperature; modes Temperature curve, Firmware Auto and Full speed.
- Start with Windows through a logon task, with no UAC prompt at logon.
- Re-applies its fan setting after sleep or when another app changes it, and runs the fans at 100% if the temperatures cannot be read.
