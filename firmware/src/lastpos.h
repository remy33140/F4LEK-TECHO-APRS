// lastpos.h — ultima posicion conocida del GPS, guardada en la flash.
//
// PARA QUE SIRVE (peticion del operador, 2026-09-13): en modo rastreador y dual
// el nodo NO debe publicar una posicion que no sea real. Si no hay fijacion, no
// manda posicion: manda el aviso "buscando satelites" y ya esta. Pero el
// operador quiere poder FORZAR a mano (doble toque del boton) el envio de la
// ULTIMA posicion conocida: la del pico mas reciente del paseo.
//
// Por eso la ultima posicion se guarda en la flash: tiene que sobrevivir a un
// reinicio, para poder encender el nodo en casa, salir a la calle y poder
// mandar "por aqui anduve" sin esperar a que el GPS fije otra vez.
//
// DONDE: pagina 0xE7000, la ultima libre antes de la configuracion (el registro
// de viaje ocupa 0xC8000..0xE7FFF y la config 0xE8000/0xE9000). No se toca nada
// de lo que ya funciona.
//
// CUANDO SE ESCRIBE: solo cuando hay una posicion REAL (fijacion de verdad) y
// como mucho una vez cada kMinWriteIntervalMs. Nunca se escribe sin fijacion, y
// nunca se escribe la posicion fija configurada del repetidor: esa no es una
// posicion "conocida por GPS", es una posicion inventada por el usuario.
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "gps.h"

// Lee la flash y deja la ultima posicion en memoria. Se puede llamar cuando se
// quiera; devuelve true si habia una posicion valida guardada.
bool lastPosLoad();

// Datos guardados (solo validos despues de lastPosLoad()).
bool lastPosValid();

// Hora del GPS del momento en que se tomo esa posicion (para el sello "@").
bool lastPosTimeValid();
uint8_t lastPosHour();
uint8_t lastPosMinute();
uint8_t lastPosSecond();
uint8_t lastPosDay();
uint8_t lastPosMonth();

// Rellena un GpsData con la posicion guardada, listo para pasarselo a
// aprsSendTrackerBeacon(). Devuelve false si no hay nada guardado.
//
// OJO IMPORTANTE sobre `fixTimeValid`: se deja a false A PROPOSITO. En APRS, una
// posicion con la hora ACTUAL significa "estoy aqui ahora mismo". Una posicion
// vieja tiene que llevar la hora a la que se tomo, no la de ahora, para que
// quien la vea sepa que es la ultima conocida y no una posicion nueva.
bool lastPosFill(GpsData &out, bool &fixTimeValid);

// Guarda una posicion REAL. Solo escribe si hay fijacion y si ha pasado el
// intervalo minimo (o si `force`). Devuelve true si escribio.
bool lastPosSave(const GpsData &g, bool force = false);

// Borra la posicion guardada (factory reset).
bool lastPosWipe();
