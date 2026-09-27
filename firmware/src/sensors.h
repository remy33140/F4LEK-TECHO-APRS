// sensors.h — I2C sensors on the shared bus (P1.04/P0.11): INA219, AHT20, BMP280
// Detection/reading patterns from NavaTastic/Meshtastic (N-04) and CA2RXU
// wx_utils.cpp (BMP280 forced mode + AHT20 humidity). License: GPL-3.0

#pragma once

#include <stddef.h>   // size_t (lo usa sensorsI2cScan)

struct SensorReadings {
  bool hasBmp = false;  // BMP280 present
  bool hasBme = false;  // BME280 present (temp + humidity + pressure)
  bool hasBs6 = false;  // BME680 present (temp + humidity + pressure + gas)
  bool hasAht = false;  // AHT20 present
  bool hasIna = false;  // INA219 present
  bool wxOk = false;    // any weather sensor present
  bool inaOk = false;   // hasIna (alias)
  // What each magnitude can actually be read from (used by WX and telemetry,
  // so a missing probe shows as "na" instead of a made-up 0).
  bool tempOk = false;
  bool humOk = false;
  bool pressOk = false;
  float tempC = 0.0f;   // last valid temperature (0 if none)
  float hum = 0.0f;     // last valid humidity (0 if none)
  float pressHpa = 0.0f;  // last valid pressure hPa (0 if none)
  float gasKohm = 0.0f;   // BME680 air quality (kOhm, 0 if none)
  // nRF52 internal sensor: the die temperature (useful as "inside the box" and
  // as the fallback when there is no external probe). RAW value: add the user
  // offset (cfg.chipTempOffsetC) before using or publishing it.
  float chipTempC = 0.0f;
  bool chipTempOk = false;
  float vbatDivV = 0.0f;  // internal divider P0.31 (battery rail, always)
  float inaBusV = 0.0f;   // INA219 bus voltage
  float inaCurrentMa = 0.0f;  // signed: >0 consume, <0 charge
};

// Latest readings cache, refreshed by sensorsRead() (shared by all modules).
extern SensorReadings gSensorCache;

// Scan the I2C bus, init the sensors found. Call once after Wire.begin().
void sensorsInit(SensorReadings &r);

// Take a fresh measurement (blocking, ~20 ms) and refresh the cached struct.
void sensorsRead(SensorReadings &r);

// Name of the detected weather chip ("BMP280" / "BME280" / "BME680" / "AHT20"
// / "-" when there is none): shown in the status reply.
const char *sensorsWxName();

// Chip (die) temperature with the user offset applied. NOT the air temperature:
// the die runs hot (radio, regulator, enclosure), hence the measured offset.
inline float sensorsChipTemp(const SensorReadings &r, float offsetC) {
  return r.chipTempC + offsetC;
}

// True when any external probe is present (BMP280 / AHT20).
inline bool sensorsHasExternal(const SensorReadings &r) { return r.wxOk; }

// Detect one external sensor if none was found yet (retry helper: some modules
// need time after power-up, and a probe may also be plugged in later).
// Returns true when an external sensor is available.
bool sensorsRetryDetect(SensorReadings &r);

// Battery voltage for telemetry: INA219 when in Li range, else the divider.
// Returns 0 when no plausible source.
float sensorsBatteryVolt(const SensorReadings &r);

// ★ ESCANEO DEL BUS I2C (herramienta de taller, 2026-09-15). Escribe en `out` la lista
// de direcciones que CONTESTAN, mas el chip ID del sensor meteorologico en 0x76/0x77.
// SOLO LEE: no escribe en ningun chip ni en la configuracion.
// PARA QUE HACE FALTA: "el BME280 no se detecta" tiene dos causas muy distintas (el chip
// no esta / el bus no funciona) y desde fuera no se pueden separar. En el T-Echo hay DOS
// chips en este bus: el BME280 (0x76/0x77) y el reloj PCF8563 (0x51). Si sale el 0x51 y
// no el 0x77, el bus vive y el que falla es el BME280.
void sensorsI2cScan(char *out, size_t n);
