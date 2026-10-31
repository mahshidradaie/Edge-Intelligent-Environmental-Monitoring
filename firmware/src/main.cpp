// EdgeGuard node: ESP32 + BME280 -> on-device anomaly detection -> MQTT (optionally TLS).
//
// Features: non-blocking Wi-Fi/MQTT reconnect with exponential backoff, MQTT last-will
// status topic, offline ring buffer with flush on reconnect, hardware task watchdog,
// ArduinoOTA updates, NTP timestamps.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config.h"
#include "anomaly.h"
#include "ring_buffer.h"

using namespace edgeguard;

struct Sample {
    uint32_t ts;
    float temp, hum, pres, zTemp, zHum;
    bool alert;
};

#if USE_TLS
static WiFiClientSecure netClient;
#else
static WiFiClient netClient;
#endif
static PubSubClient mqtt(netClient);
static Adafruit_BME280 bme;

static AnomalyDetector tempDet, humDet;
static RingBuffer<Sample, 64> offline;

static String tTelemetry, tStatus, tAlert;
static uint32_t nextSample = 0, nextWifi = 0, nextMqtt = 0, mqttBackoff = 1000;
static bool prevAlert = false;

static void connectWifi() {
    if (WiFi.status() == WL_CONNECTED || millis() < nextWifi) return;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    nextWifi = millis() + 10000;           // retry window
}

static bool publishSample(const Sample &s) {
    JsonDocument doc;
    doc["ts"] = s.ts;
    doc["temperature"] = serialized(String(s.temp, 2));
    doc["humidity"]    = serialized(String(s.hum, 2));
    doc["pressure"]    = serialized(String(s.pres, 2));
    doc["z_temp"]      = serialized(String(s.zTemp, 2));
    doc["z_hum"]       = serialized(String(s.zHum, 2));
    doc["alert"]       = s.alert ? 1 : 0;
    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    return mqtt.publish(tTelemetry.c_str(), (const uint8_t *)payload, n, false);
}

static void connectMqtt() {
    if (mqtt.connected() || WiFi.status() != WL_CONNECTED || millis() < nextMqtt) return;
    const bool ok = strlen(MQTT_USER)
        ? mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, tStatus.c_str(), 1, true, "offline")
        : mqtt.connect(DEVICE_ID, nullptr, nullptr, tStatus.c_str(), 1, true, "offline");
    if (ok) {
        mqtt.publish(tStatus.c_str(), "online", true);
        mqttBackoff = 1000;
        Sample s;
        while (offline.pop(s)) { publishSample(s); mqtt.loop(); }   // flush backlog
    } else {
        nextMqtt = millis() + mqttBackoff;
        mqttBackoff = min<uint32_t>(mqttBackoff * 2, 60000);
    }
}

void setup() {
    Serial.begin(115200);
    tTelemetry = String("edgeguard/") + DEVICE_ID + "/telemetry";
    tStatus    = String("edgeguard/") + DEVICE_ID + "/status";
    tAlert     = String("edgeguard/") + DEVICE_ID + "/alert";

    Wire.begin();
    if (!bme.begin(BME280_ADDR)) { Serial.println("BME280 not found"); delay(2000); ESP.restart(); }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_ID);
#if USE_TLS
    netClient.setCACert(MQTT_CA_CERT);
#endif
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setBufferSize(512);

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    ArduinoOTA.setHostname(DEVICE_ID);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();

    esp_task_wdt_init(30, true);           // reset if loop() stalls for 30 s
    esp_task_wdt_add(NULL);
}

void loop() {
    esp_task_wdt_reset();
    connectWifi();
    connectMqtt();
    ArduinoOTA.handle();
    mqtt.loop();

    if ((int32_t)(millis() - nextSample) < 0) return;
    nextSample = millis() + SAMPLE_PERIOD_MS;

    Sample s;
    s.ts   = (uint32_t)time(nullptr);
    s.temp = bme.readTemperature();
    s.hum  = bme.readHumidity();
    s.pres = bme.readPressure() / 100.0f;
    if (isnan(s.temp) || isnan(s.hum)) return;       // skip bad reads

    const bool aT = tempDet.update(s.temp);
    const bool aH = humDet.update(s.hum);
    s.zTemp = tempDet.zscore();
    s.zHum  = humDet.zscore();
    s.alert = aT || aH;

    if (mqtt.connected()) publishSample(s); else offline.push(s);

    if (s.alert != prevAlert && mqtt.connected()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ts\":%lu,\"state\":\"%s\",\"z_temp\":%.2f,\"z_hum\":%.2f}",
                 (unsigned long)s.ts, s.alert ? "raised" : "cleared", s.zTemp, s.zHum);
        mqtt.publish(tAlert.c_str(), msg, false);
    }
    prevAlert = s.alert;
    Serial.printf("T=%.2f H=%.2f P=%.1f zT=%.2f alert=%d buffered=%u\n",
                  s.temp, s.hum, s.pres, s.zTemp, s.alert, (unsigned)offline.size());
}
