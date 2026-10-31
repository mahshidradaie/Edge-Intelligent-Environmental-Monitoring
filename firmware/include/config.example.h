// Copy to config.h (git-ignored) and fill in your values.
#pragma once

#define WIFI_SSID        "your-wifi"
#define WIFI_PASSWORD    "your-password"

#define MQTT_HOST        "192.168.1.10"
#define MQTT_PORT        1883            // 8883 when USE_TLS is 1
#define MQTT_USER        ""              // leave empty for anonymous (dev only)
#define MQTT_PASSWORD    ""
#define DEVICE_ID        "node01"

#define USE_TLS          0               // 1 = TLS with CA certificate below
static const char MQTT_CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
(paste your broker CA certificate here when USE_TLS = 1)
-----END CERTIFICATE-----
)EOF";

#define OTA_PASSWORD     "change-me"
#define SAMPLE_PERIOD_MS 5000
#define BME280_ADDR      0x76
