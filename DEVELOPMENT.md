# Development and validation

The `codex/reliability-improvements` branch fixes dropped text events, unsafe
header fallback, optional calibration progress handling, shared instance state,
passive version detection, and the minimal example's UART logging and OTA setup.
The examples now reference the HomeAutomationsXYZ fork.

## Run the checks

Use Python 3.12 or later in a virtual environment:

```sh
python -m pip install esphome==2026.9.1 ziglang==0.16.0
python tests/run_tests.py
python tests/validate_examples.py
```

Alternatively set `CXX=g++` for the native tests. The tests compile the actual
component with fake ESPHome UART, sensor, and clock classes. They cover rapid
presence/OFF transitions while distance publication remains throttled, text
fragmentation, independent radar instances, malformed headers, passive version
detection, and calibration completion with/without a progress sensor in both
existing response formats.

Example validation substitutes dummy credentials and a local component source.
The complete example is a configuration fragment, so the validator supplies an
ESP32 board, device name, Wi-Fi and logger when missing. Build artifacts stay in
`.build/`. This check validates schemas and generates C++; it does not compile
or flash ESP firmware.

## Test on hardware before deployment

After publishing this branch, set your ESPHome external component source to:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/HomeAutomationsXYZ/hlk_ld2402_esphome
      ref: codex/reliability-improvements
    components: [hlk_ld2402]
    refresh: 0s
```

Keep `logger.baud_rate: 0` when using GPIO1/GPIO3 for the radar. Compile your
actual configuration and check presence transitions, distance throttling,
calibration completion without a progress sensor, save/reboot, and engineering
mode against the radar's serial output. Pin a tested commit for everyday use.

## Remaining protocol work

Binary data frame assembly still assumes an entire frame is buffered and can
consume consecutive frames together. Command response callers still use
inconsistent payload offsets. Correcting these requires an authoritative frame
layout or captured traffic from the relevant firmware. The malformed-header fix
does not resolve binary frame fragmentation.

Blocking command waits also remain. A later change should centralize response
validation and introduce a nonblocking command queue with deadlines. Native
tests do not establish compatibility with physical hardware or all ESPHome
versions.


## Read-only hardware diagnostics

Install the current branch and press **Read Radar Diagnostics** in Normal mode,
after calibration has finished. Capture ESPHome logs from `RADAR DIAGNOSTICS BEGIN`
through `RADAR DIAGNOSTICS END`. This reads the current hardware maximum distance,
disappearance delay, and all 16 motion and micromotion threshold values (raw and dB).
It enters/exits configuration mode but never writes parameters or saves flash.
Detection may pause briefly during the synchronous read. A failed exit is reported;
select Normal mode before retrying. These are current settings, not proof of flash persistence.

The YAML `max_distance` and `timeout` options currently only store component values;
they do not program the radar. Diagnostics print them separately for comparison.
Zero raw threshold has a logarithmic value of negative infinity; use the raw value.
Only documented threshold ranges 0x0010–0x001F and 0x0030–0x003F are queried.

Readback follows the manufacturer's [LD2402 V1.08 manual, sections 5.2.6–5.2.7](https://h.hlktech.com/download/HLK-LD2402-24G/1/HLK-LD2402%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C%20V1.08%20.pdf).
Parameter requests contain IDs without a count prefix. Replies are length-delimited,
validated for command/ACK/footer, and decoded after the ACK. Readbacks and active
firmware queries use this stricter reader; older command paths still use the legacy reader.

For template threshold numbers, use `set_action` and its `x` value to write only on
explicit user changes. Do not attach writes to `on_value`, which also runs when
publishing a number's state. Read buttons should update threshold sensors only.


## Apply distance and timeout

Press **Apply Detection Settings** once in idle Normal mode to program the YAML
`max_distance` (rounded to 0.1 m) and `timeout` (seconds). It validates the write ACK,
reads both values back, and only saves to flash if they match. Save commits the
radar's current configuration, including its existing thresholds; this action does
not write threshold parameters or run calibration. Check for `verified and saved`
and `config exit confirmed`, then run diagnostics. Failed operations can leave
partially changed settings; inspect diagnostics before retrying. A power-cycle and
another readback are needed to independently confirm persistence. It does not run
on boot, so installing firmware alone does not overwrite radar settings.


Configuration exit is retried up to three times if its ACK is missing. These retries
only send exit commands, never repeat writes or saves. If exit remains unconfirmed,
**Normal Mode** retries exit even when the cached output mode is already Normal.
Missing exit ACKs can block the action for about 3.5 seconds; this recovery path is
still synchronous. A save ACK alone does not prove post-power-cycle persistence.


## Engineering capture

Engineering reports use payload length 0x0083, not a frame-type byte. The decoder
buffers all 141 bytes across UART reads, validates the footer/status, and reads
16 motion energies at offset 9 and 16 micro/static energies at offset 73.
Every two seconds, `RADAR ENERGY` logs status/distance followed by 16 `ENERGY`
lines containing both raw energies and dB. Zero energy is reported as -inf dB.
Existing energy sensors expose motion energy; micro/static energy is in the logs.
Binary status 2 drives the Micromovement entity in engineering mode. Normal text
mode still has no separate stationary classification and retains its legacy
range-based Micromovement entity; do not compare those entity semantics across modes.

Capture 60 seconds empty, 60 seconds standing still at the test position, and
60 seconds empty after departure. Do not change thresholds during capture. Return
to Normal Mode afterward. Tests cover every split boundary, adjacent reports,
bad footers, incomplete-frame timeout, and reported detection status. Actual device
capture is still required to validate firmware behaviour.
