#!/usr/bin/env python3
"""Hardware-free node simulator: publishes the same telemetry/status/alert messages as the
ESP32 firmware, using a Python port of firmware/include/anomaly.h, with scheduled faults.

  python3 tools/sensor_sim.py --host localhost --device node01 --period 1 --fault-every 60
"""
import argparse, json, math, random, time
import paho.mqtt.client as mqtt


class AnomalyDetector:
    """Python port of edgeguard::AnomalyDetector (same parameters and behaviour)."""
    def __init__(self, alpha=0.05, z_thr=4.0, warmup=30, raise_n=3, clear_n=5, min_std=0.05):
        self.alpha, self.z_thr, self.warmup = alpha, z_thr, warmup
        self.raise_n, self.clear_n, self.min_std = raise_n, clear_n, min_std
        self.n = 0; self.out = 0; self.norm = 0
        self.mean = 0.0; self.var = 0.0; self.z = 0.0; self.alert = False

    def update(self, x):
        if self.n == 0:
            self.mean, self.var, self.z, self.n = x, 0.0, 0.0, 1
            return False
        sd = max(math.sqrt(self.var), self.min_std)
        self.z = (x - self.mean) / sd
        outlier = self.n >= self.warmup and abs(self.z) > self.z_thr
        if outlier: self.out += 1; self.norm = 0
        else:       self.norm += 1; self.out = 0
        if not self.alert and self.out >= self.raise_n: self.alert = True
        elif self.alert and self.norm >= self.clear_n:  self.alert = False
        if not outlier:
            d = x - self.mean
            self.mean += self.alpha * d
            self.var = (1 - self.alpha) * (self.var + self.alpha * d * d)
        self.n = min(self.n + 1, 65535)
        return self.alert


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="sim01")
    ap.add_argument("--period", type=float, default=1.0, help="seconds between samples")
    ap.add_argument("--fault-every", type=int, default=60, help="inject a fault every N samples (0 = never)")
    ap.add_argument("--fault-len", type=int, default=10)
    ap.add_argument("--count", type=int, default=0, help="stop after N samples (0 = run forever)")
    a = ap.parse_args()

    base = f"edgeguard/{a.device}"
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=a.device)
    c.will_set(f"{base}/status", "offline", qos=1, retain=True)
    c.connect(a.host, a.port, keepalive=30)
    c.loop_start()
    c.publish(f"{base}/status", "online", qos=1, retain=True)

    dt, dh = AnomalyDetector(), AnomalyDetector()
    prev_alert, i, fault_left = False, 0, 0
    try:
        while a.count == 0 or i < a.count:
            i += 1
            if a.fault_every and i % a.fault_every == 0 and i > 40:
                fault_left = a.fault_len
            temp = 22.0 + 0.8 * math.sin(i / 200) + random.uniform(-0.15, 0.15)
            hum = 45.0 + random.uniform(-0.5, 0.5)
            if fault_left:
                temp += 4.0; hum += 12.0; fault_left -= 1
            alert = dt.update(temp) | dh.update(hum)
            msg = {"ts": int(time.time()), "temperature": round(temp, 2), "humidity": round(hum, 2),
                   "pressure": round(1013.0 + random.uniform(-0.3, 0.3), 2),
                   "z_temp": round(dt.z, 2), "z_hum": round(dh.z, 2), "alert": int(alert)}
            c.publish(f"{base}/telemetry", json.dumps(msg))
            if alert != prev_alert:
                c.publish(f"{base}/alert", json.dumps({"ts": msg["ts"], "state": "raised" if alert else "cleared",
                                                       "z_temp": msg["z_temp"], "z_hum": msg["z_hum"]}))
                print(f"[{i}] alert {'RAISED' if alert else 'cleared'}")
            prev_alert = alert
            time.sleep(a.period)
    except KeyboardInterrupt:
        pass
    finally:
        c.publish(f"{base}/status", "offline", qos=1, retain=True).wait_for_publish()
        c.loop_stop(); c.disconnect()


if __name__ == "__main__":
    main()
