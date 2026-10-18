// nRF52 registers touched by the e-paper driver, as plain memory (screenshot simulator).
#pragma once
#include <cstdint>
struct SimSpim {
  struct { volatile uint32_t SCK, MOSI, MISO, CSN; } PSEL;
  volatile uint32_t ENABLE, FREQUENCY, CONFIG, STALLSTAT, ORC;
  struct { volatile uint32_t PTR, MAXCNT, AMOUNT, LIST; } TXD, RXD;
  volatile uint32_t EVENTS_END, EVENTS_ENDTX, EVENTS_ENDRX, EVENTS_STOPPED, EVENTS_STARTED;
  volatile uint32_t TASKS_START, TASKS_STOP, TASKS_SUSPEND, TASKS_RESUME;
  volatile uint32_t SHORTS, INTENSET, INTENCLR;
};
struct SimSaadc {
  struct { volatile uint32_t PSELP, PSELN, CONFIG, LIMIT; } CH[8];
  volatile uint32_t ENABLE;
};
struct SimGpio { volatile uint32_t OUT, OUTSET, OUTCLR, IN, DIR, DIRSET, DIRCLR, PIN_CNF[32]; };
struct SimPower { struct { volatile uint32_t POWER, POWERSET, POWERCLR; } RAM[9]; volatile uint32_t GPREGRET, GPREGRET2; };
extern SimSpim gSimSpim2;
extern SimSaadc gSimSaadc;
extern SimGpio gSimP0;
extern SimPower gSimPower;
#define NRF_SPIM2 (&gSimSpim2)
#define NRF_SAADC (&gSimSaadc)
#define NRF_P0 (&gSimP0)
#define NRF_POWER (&gSimPower)
#define NRF_SPIM_Type SimSpim

// --- GPIO / SPIM helpers (no-ops) ---
#define NRF_GPIO_Type SimGpio
extern SimGpio gSimP1;
#define NRF_P1 (&gSimP1)
enum { NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_DIR_OUTPUT };
enum { NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_INPUT_DISCONNECT };
enum { NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_PULLUP = 3 };
enum { NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_H0H1 = 3 };
enum { NRF_GPIO_PIN_NOSENSE };
inline void nrf_gpio_cfg(uint32_t, int, int, int, int, int) {}
inline void nrf_gpio_cfg_input(uint32_t, int) {}
inline void nrf_gpio_cfg_output(uint32_t) {}
inline void nrf_gpio_pin_set(uint32_t) {}
inline void nrf_gpio_pin_clear(uint32_t) {}
inline uint32_t nrf_gpio_pin_read(uint32_t) { return 0; }
struct nrfx_spim_config_t { uint32_t sck_pin, mosi_pin, miso_pin, ss_pin, frequency, mode, bit_order, orc, irq_priority; };
struct nrfx_spim_t { int id; };
#define NRFX_SPIM_INSTANCE(n) {n}
#define NRFX_SPIM_PIN_NOT_USED 0xFF
#define NRFX_SUCCESS 0
enum { NRF_SPIM_FREQ_4M = 0x40000000, NRF_SPIM_MODE_0 = 0, NRF_SPIM_BIT_ORDER_MSB_FIRST = 0 };
inline int nrfx_spim_init(nrfx_spim_t *, const nrfx_spim_config_t *, void *, void *) { return 0; }
inline void nrf_spim_enable(SimSpim *) {}
inline void nrf_spim_disable(SimSpim *) {}
#define SAADC_CH_PSELP_PSELP_NC 0
#define SAADC_CH_PSELP_PSELP_AnalogInput7 8
#define SPIM_CONFIG_CPHA_Leading 0
#define SPIM_CONFIG_CPHA_Pos 1
#define SPIM_CONFIG_CPOL_ActiveHigh 0
#define SPIM_CONFIG_CPOL_Pos 2
#define SPIM_CONFIG_ORDER_MsbFirst 0
#define SPIM_CONFIG_ORDER_Pos 0
#define SPIM_FREQUENCY_FREQUENCY_M4 0x40000000
inline void NVIC_SystemReset() {}
