# EdgeGuard IoT

An end-to-end **edge-intelligent environmental monitoring system**: an ESP32 node detects anomalies *on the device*, streams telemetry over MQTT (TLS-capable), keeps working through network outages, and feeds a containerized time-series stack (Mosquitto, Telegraf, InfluxDB, Grafana).

```
 BME280 --I2C--> ESP32 (EWMA z-score detector, offline buffer, OTA, watchdog)
                    |  MQTT  edgeguard/<id>/{telemetry,alert,status}
                    v
              Mosquitto --> Telegraf --> InfluxDB --> Grafana
```

## Design highlights

| Concern | Approach |
|---------|----------|
| Edge analytics | Streaming EWMA mean/variance with a z-score test; no heap, no dependencies (`firmware/include/anomaly.h`) |
| False-alarm control | Warm-up period, debounce (3 consecutive outliers to raise, 5 normal samples to clear), outliers excluded from the baseline |
| Reliability | Non-blocking Wi-Fi / MQTT reconnect with exponential backoff (1 s up to 60 s), 64-sample offline ring buffer flushed on reconnect, 30 s hardware task watchdog |
| Presence | MQTT last-will + retained `status` topic (`online` / `offline`) |
| Security | TLS option with CA pinning (`USE_TLS`), MQTT credentials, OTA password; dev broker config is plaintext on purpose and documented as such |
| Maintainability | OTA firmware updates over Wi-Fi, host-side unit tests, hardware-free simulator |

## Repository layout

```
firmware/        PlatformIO project (ESP32, Arduino framework)
  include/       anomaly.h  ring_buffer.h  config.example.h
  src/main.cpp   application
  test/          host unit tests (g++)
backend/         docker-compose: mosquitto, telegraf, influxdb, grafana
tools/           sensor_sim.py (node simulator), monitor.py (live subscriber)
```

## 1. Test the logic on your PC (no hardware)

```bash
cd firmware/test
g++ -std=c++11 -Wall -Wextra -I../include test_core.cpp -o test_core && ./test_core
```

The test checks: no false positives over 2000 clean samples, an alert after exactly 3 outlier samples, clearing after exactly 5 normal samples, an undisturbed baseline after an incident, and ring-buffer FIFO / overwrite-oldest behaviour.

## 2. Run the full pipeline with the simulator

```bash
cp .env.example .env            # set your own secrets
cd backend && docker compose --env-file ../.env up -d
cd .. && pip install -r tools/requirements.txt
python3 tools/sensor_sim.py --device sim01 --period 1 --fault-every 60   # publishes + injects faults
python3 tools/monitor.py                                                  # watch all topics
```

- InfluxDB UI: http://localhost:8086 (bucket `telemetry`, measurement `environment`, tag `device`)
- Grafana: http://localhost:3000 - the InfluxDB data source is pre-provisioned; build a panel with
  `from(bucket:"telemetry") |> range(start: -1h) |> filter(fn:(r)=> r._measurement=="environment" and r._field=="temperature")`

The simulator uses a Python port of the same detector, so alerts appear on `edgeguard/<id>/alert` just like on the real node.

## 3. Flash the ESP32 node

1. Wire a BME280 to the ESP32 I2C pins (SDA 21, SCL 22, 3.3 V, GND).
2. `cp firmware/include/config.example.h firmware/include/config.h` and fill in Wi-Fi, broker host, device ID, OTA password.
3. `cd firmware && pio run -t upload && pio device monitor`
4. Later updates over Wi-Fi: uncomment the `espota` lines in `platformio.ini`.

### MQTT payloads

```json
// edgeguard/<id>/telemetry
{"ts":1790000000,"temperature":22.41,"humidity":45.2,"pressure":1012.9,"z_temp":0.35,"z_hum":-0.2,"alert":0}
// edgeguard/<id>/alert (only on state change)
{"ts":1790000100,"state":"raised","z_temp":6.1,"z_hum":0.4}
// edgeguard/<id>/status (retained, last-will)
online | offline
```

## Production hardening checklist

- Set `allow_anonymous false`, add a Mosquitto password file and ACLs per device.
- Enable the TLS listener (snippet in `backend/mosquitto/mosquitto.conf`) and set `USE_TLS 1`.
- Replace default tokens and passwords through `.env`.

## Verification status

Verified in simulation: detector and ring buffer (host unit tests) and the simulator through a local Mosquitto broker (telemetry, alert raise/clear, status). The ESP32 firmware and the Docker stack must be built and run on your own setup; check them there before quoting hardware results.
