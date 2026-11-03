## First release

- Runs both fans from the CPU and GPU temperatures through Acer's firmware interface. The Auto mode of the firmware keeps the fans while the chips stay cool.
- Acts on the baseline temperature of each chip, the 25th percentile of the last 60 seconds, so a one-second boost spike does not spin the fans up.
- Separate CPU and GPU curves just below the throttle points, editable in `AcerFan.ini`.
- A tray icon with the temperature of the hotter chip, and the modes Temperature curve, Firmware Auto and Full speed.
- Start with Windows through a logon task, with no UAC prompt at logon.
- Re-applies its fan setting after sleep or a change by another app, and runs the fans at 100% when the temperatures stay unreadable.
