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
