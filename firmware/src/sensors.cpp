// sensors.cpp — I2C sensors: INA219 (0x40/0x41/0x43), AHT20 (0x38), BMP280
// (0x76/0x77). Battery rail on the internal divider P0.31 (AR_INTERNAL_3_0,
// divider 2x1M -> x2). License: GPL-3.0

#include "sensors.h"

#include <Arduino.h>
#include "diag.h"     // diagTrazaArranque(): el saludo del arranque no se cuela en modo TNC
#include <Adafruit_AHTX0.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_INA219.h>
#include <Wire.h>

// ★★ EL BUS I2C DE LOS SENSORES, EN TWIM1 (2026-09-15) ★★
//
// En el T-Echo, la pantalla de tinta electronica va por el periferico SPIM0. Y en el
// nRF52840 **SPIM0 y TWIM0 son EL MISMO BLOQUE DE HARDWARE** (los dos en 0x40003000): solo
// puede usarlo uno. El `Wire` de Arduino va en TWIM0 (Wire_nRF52.cpp:
// `TwoWire Wire(NRF_TWIM0, ...)`), asi que si queremos SPIM0 para la pantalla HAY QUE MOVER
// LOS SENSORES a otro bus.
//
// Esto es justo lo que hace el firmware de fabrica: su driver de pantalla usa SPIM0 (se ve
// en el binario: el control block de SPIM0 esta en 0x523D8) y su I2C lo tiene movido.
//
// Los sensores van a TWIM1 (los mismos pines, que en esta placa solo tienen un bus fisico:
// P0.26 SDA y P0.27 SCL). Las librerias de Adafruit aceptan el bus como parametro
// (`begin(addr, TwoWire *theWire = &Wire)`), asi que solo hay que pasarle este.
//
// En las placas Faketec (OLED) esto NO se hace: alli no hay tinta electronica, no hace
// falta SPIM0, y el I2C se queda en TWIM0 como estaba.
// ★★ DESACTIVADO TEMPORALMENTE (2026-09-15) ★★
// Se mueve el I2C a TWIM1 SOLO si hace falta SPIM0 para la pantalla. Ahora mismo la
// pantalla va por SPIM2 (que no comparte bloque con el I2C), asi que NO hace falta mover
// nada, y mover el bus es un riesgo añadido que estamos descartando: el firmware no
// arrancaba y este es el unico cambio que el banco de pruebas (que si arranca) no tiene.
#define WIRE_SENSORES Wire
#include <nrf.h>
// sd_temp_get(): la temperatura del chip se lee por el SoftDevice cuando esta arriba (TEMP
// es un periferico suyo, ver readChipTempC). El porque, en ble_kiss.h.
#include <nrf_soc.h>

#include "ble_kiss.h"  // bleSoftDeviceIsp(): quien manda hoy sobre el periferico TEMP

namespace {

// One weather chip can be present: BMP280 (pressure+temp), BME280 (adds
// humidity) or BME680 (adds air quality). They share the 0x76/0x77 addresses,
// so the chip ID register (0xD0) decides which driver to start.
enum class WxChip { None, Bmp280, Bme280, Bme680 };

Adafruit_BMP280 *gBmp = nullptr;
Adafruit_BME280 *gBme = nullptr;
Adafruit_BME680 *gBs6 = nullptr;
Adafruit_AHTX0 gAht;
Adafruit_INA219 *gIna = nullptr;  // allocated once at the detected address
WxChip gWx = WxChip::None;
bool gHasAht = false;
bool gHasIna = false;

constexpr uint8_t kChipIdReg = 0xD0;
constexpr uint8_t kIdBmp280 = 0x58;
constexpr uint8_t kIdBme280 = 0x60;
constexpr uint8_t kIdBme680 = 0x61;

uint8_t readChipId(uint8_t addr) {
  WIRE_SENSORES.beginTransmission(addr);
  WIRE_SENSORES.write(kChipIdReg);
  if (WIRE_SENSORES.endTransmission(false) != 0) return 0;
  if (WIRE_SENSORES.requestFrom((int)addr, 1) != 1) return 0;
  return (uint8_t)WIRE_SENSORES.read();
}

// nRF52 internal temperature sensor (TEMP peripheral): 0.25 °C per unit.
// Reads the DIE, not the air (that is what the user offset compensates).
//
// ★★ CON EL BLUETOOTH ARRANCADO HAY QUE LEERLO POR EL SOFTDEVICE (2026-09-17) ★★
// POR QUE: TEMP esta en la lista de perifericos que el SoftDevice se reserva
// (`__NRF_NVIC_SD_IRQS_0` en nrf_nvic.h: POWER_CLOCK, RADIO, RTC0, TIMER0, RNG, ECB,
// CCM_AAR, TEMP, NVMC, SWI5). Con el stack arriba, escribir `NRF_TEMP->TASKS_START` a mano
// no es que "no haga nada": es un acceso a un periferico protegido y puede acabar en falta.
// La API del stack para esto es `sd_temp_get()`, y devuelve lo mismo (un cuarto de grado).
// Esta rama ya existia y se quito el 2026-09-13 al sacar el Bluetooth del binario; vuelve
// porque el Bluetooth vuelve. La condicion es el estado REAL del stack (`bleSoftDeviceIsp()`,
// que se pone al arrancar `Bluefruit.begin()` y se quita en `bleShutdown()`), no el ajuste de
// configuracion: entre que se pide encender y que arranca hay un trecho.
float readChipTempC() {
  if (bleSoftDeviceIsp()) {
    int32_t t = 0;
    if (sd_temp_get(&t) == 0) return (float)t * 0.25f;
    // Si el stack lo rechaza no se cae nada: se sigue por el camino del registro, que a
    // partir de aqui es el unico que queda.
  }
  NRF_TEMP->TASKS_START = 1;
  uint32_t guard = 0;
  while (NRF_TEMP->EVENTS_DATARDY == 0 && guard++ < 2000000u) {
  }
  NRF_TEMP->EVENTS_DATARDY = 0;
  int32_t raw = (int32_t)NRF_TEMP->TEMP;
  NRF_TEMP->TASKS_STOP = 1;
  return (float)raw * 0.25f;
}

void detectWx() {
  if (gWx != WxChip::None) return;
  const uint8_t addrs[2] = {0x76, 0x77};
  for (uint8_t addr : addrs) {
    const uint8_t id = readChipId(addr);
    if (id == kIdBmp280) {
      gBmp = new Adafruit_BMP280(&WIRE_SENSORES);   // el bus va en el CONSTRUCTOR
      if (gBmp->begin(addr)) {
        gBmp->setSampling(Adafruit_BMP280::MODE_FORCED,
                          Adafruit_BMP280::SAMPLING_X1,
                          Adafruit_BMP280::SAMPLING_X1,
                          Adafruit_BMP280::FILTER_OFF);
        gWx = WxChip::Bmp280;
        if (diagTrazaArranque()) Serial.println("sensors: BMP280 OK");
        return;
      }
      delete gBmp;
      gBmp = nullptr;
    } else if (id == kIdBme280) {
      gBme = new Adafruit_BME280();
      if (gBme->begin(addr, &WIRE_SENSORES)) {
        gBme->setSampling(Adafruit_BME280::MODE_FORCED,
                          Adafruit_BME280::SAMPLING_X1,   // temperature
                          Adafruit_BME280::SAMPLING_X1,   // pressure
                          Adafruit_BME280::SAMPLING_X1,   // humidity
                          Adafruit_BME280::FILTER_OFF);
        gWx = WxChip::Bme280;
        if (diagTrazaArranque()) Serial.println("sensors: BME280 OK");
        return;
      }
      delete gBme;
      gBme = nullptr;
    } else if (id == kIdBme680) {
      gBs6 = new Adafruit_BME680();
      if (gBs6->begin(addr, &WIRE_SENSORES)) {
        gBs6->setTemperatureOversampling(BME680_OS_2X);
        gBs6->setHumidityOversampling(BME680_OS_2X);
        gBs6->setPressureOversampling(BME680_OS_4X);
        gBs6->setIIRFilterSize(BME680_FILTER_SIZE_3);
        gBs6->setGasHeater(320, 150);  // 320 °C for 150 ms (Bosch default)
        gWx = WxChip::Bme680;
        if (diagTrazaArranque()) Serial.println("sensors: BME680 OK");
        return;
      }
      delete gBs6;
      gBs6 = nullptr;
    }
  }
}

void detectAht() {
  if (gHasAht) return;
  gHasAht = gAht.begin(&WIRE_SENSORES);
  if (gHasAht && diagTrazaArranque()) Serial.println("sensors: AHT20 OK");
}

void detectIna() {
  if (gHasIna) return;
  const uint8_t inaAddrs[3] = {0x40, 0x41, 0x43};
  for (int i = 0; i < 3 && !gHasIna; i++) {
    Adafruit_INA219 *cand = new Adafruit_INA219(inaAddrs[i]);
    if (cand->begin(&WIRE_SENSORES)) {
      cand->setCalibration_32V_2A();
      gIna = cand;
      gHasIna = true;
      if (diagTrazaArranque()) {
        Serial.print("sensors: INA219 OK @0x");
        Serial.println(inaAddrs[i], HEX);
      }
    } else {
      delete cand;
    }
  }
}

// Fill the found/available flags from the detected hardware.
void fillFlags(SensorReadings &r) {
  r.hasBmp = (gWx == WxChip::Bmp280);
  r.hasBme = (gWx == WxChip::Bme280);
  r.hasBs6 = (gWx == WxChip::Bme680);
  r.hasAht = gHasAht;
  r.hasIna = gHasIna;
  r.inaOk = gHasIna;
  // BMP280: temp + pressure. BME280/BME680: temp + humidity + pressure.
  r.tempOk = (gWx != WxChip::None) || gHasAht;
  r.pressOk = (gWx != WxChip::None);
  r.humOk = (gWx == WxChip::Bme280) || (gWx == WxChip::Bme680) || gHasAht;
  r.wxOk = r.tempOk || r.pressOk || r.humOk;
}

}  // namespace

SensorReadings gSensorCache;

void sensorsInit(SensorReadings &r) {
  // Los pines del bus los pone la VARIANTE de la placa (en el T-Echo, P0.26/P0.27:
  // PIN_WIRE_SDA/PIN_WIRE_SCL de variants/techo/variant.h). El comentario anterior decia
  // "P1.04/P0.11", que son los de la Faketec: era un resto de cuando este fichero era suyo.
  WIRE_SENSORES.begin();

  // Three passes with a short pause: some I2C modules need time after the rail
  // comes up, and a missed probe here would mean "no sensors" until reboot.
  for (uint8_t pass = 0; pass < 3; pass++) {
    detectWx();
    detectAht();
    if (pass < 2) delay(120);
  }
  detectIna();

  fillFlags(r);
  sensorsRead(r);
}

// ★ ESCANEO DEL BUS I2C (herramienta de taller, 2026-09-15). Ver sensors.h.
// Recorre las direcciones validas de 7 bits y apunta las que contestan con ACK. Ademas
// lee el registro de chip ID (0xD0) de las dos direcciones del sensor meteorologico:
// 0x58 = BMP280, 0x60 = BME280, 0x61 = BME680, 0x00/0xFF = ahi no hay nada.
// SOLO LEE (beginTransmission + endTransmission no escriben datos; readChipId solo lee).
void sensorsI2cScan(char *out, size_t n) {
  if (!out || n < 16) return;
  String s = "I2C:";
  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    WIRE_SENSORES.beginTransmission(a);
    if (WIRE_SENSORES.endTransmission() != 0) continue;   // nadie contesta en esa direccion
    char b[8];
    snprintf(b, sizeof(b), " %02X", (unsigned)a);
    s += b;
    found++;
  }
  if (found == 0) s += " (nada)";
  char tail[64];
  snprintf(tail, sizeof(tail), " | n=%u | wx: 76=%02X 77=%02X (58=BMP 60=BME 61=BME680)",
           (unsigned)found, (unsigned)readChipId(0x76), (unsigned)readChipId(0x77));
  s += tail;
  snprintf(out, n, "%s", s.c_str());
}

bool sensorsRetryDetect(SensorReadings &r) {
  if (r.tempOk || r.humOk || r.pressOk) return true;
  detectWx();
  detectAht();
  detectIna();
  fillFlags(r);
  return r.wxOk;
}

void sensorsRead(SensorReadings &r) {
  fillFlags(r);

  // Weather chip: BMP280 / BME280 / BME680 (whichever was detected).
  if (gWx == WxChip::Bmp280 && gBmp) {
    gBmp->takeForcedMeasurement();
    r.tempC = gBmp->readTemperature();
    r.pressHpa = gBmp->readPressure() / 100.0f;
  } else if (gWx == WxChip::Bme280 && gBme) {
    gBme->takeForcedMeasurement();
    r.tempC = gBme->readTemperature();
    r.pressHpa = gBme->readPressure() / 100.0f;
    r.hum = gBme->readHumidity();
  } else if (gWx == WxChip::Bme680 && gBs6) {
    if (gBs6->performReading()) {
      r.tempC = gBs6->temperature;
      r.pressHpa = gBs6->pressure / 100.0f;
      r.hum = gBs6->humidity;
      r.gasKohm = gBs6->gas_resistance / 1000.0f;
    }
  } else {
    r.tempC = 0.0f;
    r.pressHpa = 0.0f;
  }

  // AHT20 (humidity, and temperature when there is no weather chip).
  if (gHasAht) {
    sensors_event_t humEv, tempEv;
    gAht.getEvent(&humEv, &tempEv);
    const bool wxHasHum = (gWx == WxChip::Bme280) || (gWx == WxChip::Bme680);
    if (!wxHasHum) r.hum = humEv.relative_humidity;  // AHT20 is the humidity source
    if (gWx == WxChip::None) r.tempC = tempEv.temperature;
  } else if (gWx == WxChip::Bmp280) {
    r.hum = 0.0f;  // BMP280 has no humidity
  }
  if (gWx != WxChip::Bme680) r.gasKohm = 0.0f;  // air quality only from BME680

  // ★★★ BATERIA: P0.04 (AIN2), NO P0.31 — CORREGIDO (2026-09-14) ★★★
  //
  // Esto medía `analogRead(31)`, o sea AIN7 = **P0.31**, que es la batería de las placas
  // Faketec. **En el T-Echo y el T-Echo Plus, P0.31 es el SCK DE LA PANTALLA DE TINTA
  // ELECTRONICA** (src/pins_techo.h: `PIN_EPD_SCK 31`) y la batería está en **P0.04**
  // (`PIN_BATTERY_ADC 4`, AIN2; lo mismo dice `variants/techo/variant.h`).
  //
  // Dos consecuencias, y las dos medidas en hardware:
  //   1. La lectura salía siempre 0 -> el nodo anunciaba `Bat:--` y `bat` contestaba
  //      0.00 V. O sea: la batería NUNCA se estaba midiendo en esta placa.
  //   2. El driver del ADC de este core (`analogRead_internal`) deja `CH[0].PSELP`
  //      apuntando al pin que acaba de leer, y **así se queda** aunque el ADC se
  //      deshabilite. Con esa entrada analógica enganchada a P0.31, el pin del reloj de
  //      la pantalla no puede subir a 3,3 V (medido: PIN_CNF correcto, salida de alta
  //      corriente, y el pin clavado en 0) -> **la pantalla se queda sin reloj**.
  //
  // Se deja configurable por si alguna placa vuelve a necesitar AIN7, pero el valor por
  // defecto es el que dice el hardware de esta placa.
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN 4        // P0.04 = AIN2 (T-Echo / T-Echo Plus)
#endif
  // 10-bit, internal ref gain 1/5 -> 0..3.0 V, divider factor 2.0 (N-01)
  analogReference(AR_INTERNAL_3_0);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(BATTERY_ADC_PIN);
  r.vbatDivV = (float)(sum / 8) / 1023.0f * 3.0f * 2.0f;

  if (gHasIna && gIna) {
    r.inaBusV = gIna->getBusVoltage_V();
    r.inaCurrentMa = gIna->getCurrent_mA();  // signed: >0 consumo, <0 carga
  } else {
    r.inaBusV = 0.0f;
    r.inaCurrentMa = 0.0f;
  }
  // Internal chip sensor (always available): die temperature, raw value.
  r.chipTempC = readChipTempC();
  r.chipTempOk = true;
  gSensorCache = r;
}

float sensorsBatteryVolt(const SensorReadings &r) {
  if (r.inaOk && r.inaBusV >= 2.4f && r.inaBusV <= 4.5f) return r.inaBusV;
  if (r.vbatDivV >= 2.4f && r.vbatDivV <= 4.5f) return r.vbatDivV;
  return 0.0f;
}

const char *sensorsWxName() {
  switch (gWx) {
    case WxChip::Bmp280: return "BMP280";
    case WxChip::Bme280: return "BME280";
    case WxChip::Bme680: return "BME680";
    default: break;
  }
  if (gHasAht) return "AHT20";
  return "-";
}
