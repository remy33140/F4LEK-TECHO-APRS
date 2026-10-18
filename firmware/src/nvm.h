// nvm.h — escritura en la flash interna que funciona CON y SIN el SoftDevice arriba.
//
// ★★ POR QUE EXISTE (2026-10-17): ERA LA CAUSA DEL "BUCLE" AL ARRANCAR EL BLUETOOTH ★★
// store.cpp, flog.cpp y lastpos.cpp escribian la flash tocando NRF_NVMC a mano. Con el
// SoftDevice habilitado, NVMC es un periferico PROTEGIDO: una escritura de la aplicacion en
// sus registros dispara NRF_FAULT_ID_APP_MEMACC (id 0x1001, ver nrf_sdm.h) y el SoftDevice
// reinicia el chip. Como el arranque del Bluetooth deja una linea en el registro de viaje
// ("BLE inicio 2/6") justo despues de `Bluefruit.begin()`, el nodo se reiniciaba en cuanto el
// stack subia. El 0x1001 que se vio entonces NO era un problema de prioridades de IRQ: era esto.
//
// Con el SoftDevice abajo se escribe por registro (como siempre). Con el SoftDevice arriba se
// pide la operacion por su API (sd_flash_page_erase / sd_flash_write), que la encaja entre
// los eventos de radio, y se espera a que la flash diga lo que tiene que decir.
// License: GPL-3.0

#pragma once

#include <stddef.h>
#include <stdint.h>

// Borra la pagina de 4 KB que empieza en `addr`. Devuelve false si no se pudo.
bool nvmErasePage(uint32_t addr);

// Escribe `len` bytes en `addr` (alineada a 4). La ultima palabra se rellena con 0xFF.
// La zona tiene que estar borrada. Devuelve false si no se pudo.
bool nvmWrite(uint32_t addr, const void *data, size_t len);
