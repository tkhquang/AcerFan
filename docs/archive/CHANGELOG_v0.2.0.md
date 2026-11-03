## Fewer fan cycles

- Fewer short fan cycles: the baseline window grows from 60 to 120 seconds, so a burst of work shorter than about 90 seconds no longer spins the fans up.
- No full fan speed right after logon: AcerFan takes over only after one full baseline window of readings. This replaces the `min_samples` setting.
- `AcerFan.log` gets a status line every 60 seconds instead of 10, so it keeps about ten days of history.
- An existing `AcerFan.ini` keeps its old values. For the new defaults, set `baseline_s = 120` and `status_every_s = 60`, or delete the file and restart AcerFan.
