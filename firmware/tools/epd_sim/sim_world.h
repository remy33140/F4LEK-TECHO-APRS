#pragma once
#include "aprs.h"
#include "gps.h"
struct SimWorld {
  uint32_t nowMs = 3600000;
  GpsData gps;
  float batV = 3.98f;
  HeardStation heard[8];
  uint8_t nHeard = 0;
  uint32_t lastTxMs = 0;
};
extern SimWorld gWorld;
