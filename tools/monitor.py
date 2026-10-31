#!/usr/bin/env python3
"""Prints every EdgeGuard message (telemetry, alert, status) from the broker."""
import sys
import paho.mqtt.client as mqtt

host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect = lambda cl, u, f, rc, p=None: cl.subscribe("edgeguard/#")
c.on_message = lambda cl, u, m: print(f"{m.topic:34s} {m.payload.decode()}")
c.connect(host, 1883, 30)
c.loop_forever()
