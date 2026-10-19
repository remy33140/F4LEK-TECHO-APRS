// battery.h — la bateria del T-Echo: UNA sola lectura y UN solo porcentaje para todo el firmware.
//
// ★★ POR QUE EXISTE (2026-10-18) ★★
// Habia dos lecturas de la bateria (sensors.cpp para la pantalla y la telemetria, power.cpp para
// el sueño por bateria baja), las dos con el muestreo por defecto del core (3 us, 10 bits), y un
// porcentaje en RECTA de 3,0 V a 4,2 V. Tres problemas:
//   1. El divisor es de 2 x 1 MOhm (500 kOhm vistos desde el ADC). Nordic pide 40 us de
//      adquisicion para fuentes de hasta 800 kOhm: con 3 us el condensador de muestreo no llega
//      a cargarse y la lectura sale baja y con ruido.
//   2. Una LiPo no es una recta: pasa casi toda la descarga en una meseta 3,7-3,9 V y se hunde
//      al final. La recta daba porcentajes falsos en medio y abajo.
//   3. El 0 % de la recta estaba en 3,0 V, pero el nodo se duerme en `sleepCutMv` (3,2 V): se
//      apagaba enseñando ~16 %.
// Aqui: lectura a 40 us / 12 bits promediada, factor de correccion (`batteryCal`, medido con un
// multimetro), suavizado que ignora la caida del TX, y curva LiPo con el 0 % en el corte.
// License: GPL-3.0

#pragma once

#include <stdint.h>

#include "config.h"

void batteryBindConfig(const DigiConfig *cfg);

// Tension de la bateria en mV, corregida y suavizada. 0 = sin lectura creible.
// Durante un TX y los 3 s siguientes devuelve la ultima buena (la radio hunde la tension).
uint16_t batteryMv();

// Porcentaje 0..100 segun la curva de una LiPo, con el 0 % en `sleepCutMv`. Con histeresis
// (no cambia por menos de 2 puntos) para que la tinta no se repinte por ruido. -1 = sin lectura.
int batteryPercent();

// Lectura NUEVA y corregida, sin suavizar (mV, 0 = fuera de rango). Para quien necesita varias
// lecturas seguidas de verdad: el sueño por bateria baja (power.cpp) cuenta 8 bajas seguidas.
uint16_t batteryReadNowMv();

// El USB alimenta el nodo (cargando o llena): la tension no dice entonces la carga real.
bool batteryOnUsb();
