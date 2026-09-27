/* Arduino.h — SUSTITUTO MINIMO PARA EL BANCO DE PRUEBAS DEL BOTON (T-Echo Project Butter).
 *
 * QUE ES: un Arduino.h de mentira para poder compilar el button.cpp DE VERDAD en el
 * ordenador (Windows/Linux) y medir sus tiempos sin una placa delante. NO forma parte
 * del firmware: vive en tools/banco_boton/ y solo lo usa el banco de pruebas.
 *
 * Lo que hace: `millis()`, `digitalRead()` y `attachInterrupt()` no hablan con ningun
 * chip, hablan con el SIMULADOR que hay en banco.cpp (un reloj de mentira y dos pines
 * de mentira). Asi se pueden reproducir milisegundo a milisegundo toques, rebotes,
 * refrescos de 1,5 s y transmisiones de radio, y ver que gesto sale y CUANDO.
 *
 * Los valores (LOW/HIGH/INPUT_PULLUP/CHANGE...) son los mismos numeros que usa el core
 * de Adafruit para nRF52, y BUTTON_PIN = 42 y el tactil en 11 son los de la T-Echo
 * (variants/techo/variant.h y src/pins_techo.h).
 */
#ifndef BANCO_ARDUINO_H
#define BANCO_ARDUINO_H

#include <stdint.h>

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 3
#define RISING 2
#define FALLING 1

// Pines de la T-Echo (los mismos que el firmware de verdad).
#define BUTTON_PIN 42        // P1.10, el boton de la placa
#define PIN_BTN_TOUCH 11     // P0.11, la pastilla capacitiva

uint32_t millis(void);
int digitalRead(uint32_t pin);
void pinMode(uint32_t pin, int modo);
int attachInterrupt(uint32_t pin, void (*fn)(void), uint32_t modo);
uint32_t digitalPinToInterrupt(uint32_t pin);

#endif
