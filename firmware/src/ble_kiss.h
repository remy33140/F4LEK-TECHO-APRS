// ble_kiss.h — EL BLUETOOTH, FUERA DE ESTE REPOSITORIO (sustituto VACIO).
//
// ★★ QUE ES ESTE FICHERO, Y POR QUE ESTA AQUI (2026-09-21) ★★
//
// Los cuatro ficheros de codigo que SI se publican (cli.cpp, power.cpp, sensors.cpp y
// tnc.cpp) llaman a cuatro funciones del enlace Bluetooth. En el arbol de desarrollo ese
// enlace existe y funciona en banco, PERO SU ARRANQUE ROMPIA EL NODO: encendiendolo, el
// aparato se quedaba sin pantalla, sin radio y sin USB. Por eso NO se publica.
//
// Este fichero es el sustituto: declara LAS MISMAS CUATRO FUNCIONES, vacias. Asi el codigo
// publicado compila y se comporta como si el Bluetooth no estuviera (que es la verdad: aqui
// no hay ninguna pila de Bluetooth que enlazar), sin tener que tocar los cuatro ficheros.
//
// QUE HACE CADA UNA AQUI:
//   - `bleOutputFrameText()`: nada. Sin Bluetooth no hay segundo huesped al que mandar tramas.
//   - `bleShutdown()`:       nada. No hay nada que apagar (el SoftDevice no se enciende).
//   - `bleSoftDeviceIsp()`:  false SIEMPRE. Es lo que hace que `sensors.cpp` lea la
//                            temperatura por el camino normal en vez de por la API del stack.
//   - `bleResumen()`:        un texto que dice justo eso, para que quien teclee `ble` en la
//                            consola no se quede pensando que el comando esta roto.
//
// ★ SI ALGUN DIA EL BLUETOOTH VUELVE a este repositorio: se borran estas cuatro lineas y se
//   pone aqui la cabecera de verdad (la que hay en el arbol de desarrollo). El resto del
//   codigo no se toca, porque las firmas son las mismas.
//
// Los dos ficheros del enlace (el .cpp y el .h originales) se publican con extension `.off`
// para que nadie los compile por error: `ble_kiss.cpp.off` y `ble_kiss.h.off`.

#pragma once

#include <Arduino.h>   // String

inline void bleOutputFrameText(const char *textFrame) { (void)textFrame; }

inline void bleShutdown() {}

inline bool bleSoftDeviceIsp() { return false; }

inline String bleResumen() {
  return String("Bluetooth FUERA de este firmware (no esta compilado). ");
}
