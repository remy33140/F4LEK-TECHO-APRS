// haptic.h — Motor haptico del T-Echo Plus (DRV2605 por I2C 0x5A, enable en P0.08).
//
// PARA QUE EXISTE: avisos que NO se pueden ver. La pantalla de tinta electronica no se
// ilumina y tarda 1,5 s por refresco, asi que hay cosas que solo se notan si el nodo vibra.
//
// ★ LOS AVISOS LOS DECIDIO EL OPERADOR (2026-09-15). Ver `_memoria/PENDIENTE.md`:
//     - al coger fijacion GPS (patron CORTO y con limite de repeticion: en el monte el GPS
//       coge y pierde fix continuamente y no puede estar vibrando sin parar)
//     - al tocar el boton CAPACITIVO (feedback: con la pantalla lenta no se sabe si conto)
//     - al arrancar (el nodo tarda ~17 s en pintar el splash)
//     - al recibir un mensaje APRS
//     - al llegar el acuse de un mensaje enviado
//     - al preguntarle por radio, para ENCONTRAR el nodo perdido
//   DESTERRADO a proposito: vibrar en cada baliza transmitida (gasto) y vibrar por bateria
//   baja (el operador: "va a reventar la bateria"); ese aviso lo da el buzzer.
//
// IMPORTANTE: en el T-Echo NORMAL no hay motor. Alli (y en las Faketec) todo esto queda
// inerte y devuelve false, para que el resto del firmware no tenga que poner ramas.
#pragma once

#include <Arduino.h>

// Arranca el chip la primera vez que se usa. Es PEREZOSO a proposito: el bus I2C tiene que
// estar ya iniciado por sensorsInit(), y asi el orden de arranque no importa.
// Devuelve true si el DRV2605 ha contestado.
bool hapticInit();

// True si el chip esta listo. Sin inicializar todavia devuelve false (no inicializa).
bool hapticReady();

// ★ ¿ESTA PLACA ES UN PLUS? (2026-09-15)
// El motor haptico y el zumbador SOLO los lleva el Plus, y de los dos el unico que se puede
// interrogar es el motor (va por I2C y contesta o no contesta). El zumbador es un GPIO
// normal: no hay forma de preguntarle si existe.
// Asi que se pregunta por el MOTOR y se usa como carne de identidad del Plus: si contesta,
// es un Plus. Sirve para NO tocar el pin del zumbador (P0.06) en un T-Echo normal, donde no
// consta a que va ese pin.
// La respuesta se recuerda: la primera llamada habla por I2C, las siguientes no.
bool hapticEsPlus();

// Dispara UN efecto de la ROM interna del DRV2605 (0..123). Es lo que usa el comando de
// taller `vibra N`, que existe para ELEGIR los patrones de oido antes de engancharlos.
// NO BLOQUEA: la secuencia la ejecuta el propio chip y el firmware sigue a lo suyo.
bool hapticEffect(uint8_t efecto);

// Dispara una secuencia de hasta 8 efectos seguidos (los que se quieran, la ROM se
// encarga). NO BLOQUEA. Devuelve false si el motor no esta.
bool hapticSequence(const uint8_t *efectos, uint8_t n);

// ★★ LOS AVISOS (elegidos el 2026-09-15) ★★
// CRITERIO: pocos, MUY distintos entre si, y que se reconozcan de memoria. El orden va de
// menos a mas "importante": un golpe, dos, tres, un golpe suave, un zumbido y una alerta
// larga. Los numeros son efectos de la ROM del DRV2605 (tabla del datasheet, verificada):
//   1 = Strong Click 100%   7 = Soft Bump 100%      10 = Double Click 100%
//   12 = Triple Click 100%  47 = Buzz 1 100%        15 = 750 ms Alert 100%
enum HapAviso {
  HAP_TOQUE = 0,   // efecto 1  (clic seco)    -> el toque CAPACITIVO se ha registrado
  HAP_ARRANQUE,    // efecto 7  (golpe suave)  -> he arrancado
  HAP_GPS,         // efecto 10 (dos golpes)   -> he cogido fijacion GPS
  HAP_MENSAJE,     // efecto 12 (tres golpes)  -> mensaje APRS para mi
  HAP_ACUSE,       // efecto 47 (zumbido)      -> ha llegado el acuse de mi mensaje
  HAP_BUSCAR,      // efecto 15 (alerta 750ms) -> alguien me pregunta: "estoy aqui"
};

// Dispara un aviso de los de arriba. NO bloquea (lo reproduce el chip el solo).
// En las placas sin motor no hace nada.
void hapticAviso(HapAviso a);

// Vigila el CAMBIO de fijacion del GPS y avisa al COGERLA. Hay que llamarla desde el bucle
// (es lo unico que se llama en cada vuelta, y no hace nada si el fix no ha cambiado).
// Lleva dos guardas a proposito:
//   - solo avisa al cogerla, no al perderla (perderla te enteras porque deja de haber track);
//   - y no repite el aviso si hace menos de un minuto: en el monte o entre edificios el GPS
//     coge y pierde fix continuamente y no puede estar vibrando sin parar.
void hapticTickGPS(bool fixAhora);
