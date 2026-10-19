// E-paper screenshot simulator for the F4LEK T-Echo firmware.
//
// Compiles the REAL src/epaper_techo.cpp (included below, so its internal state is reachable)
// against PC stand-ins for the hardware (stubs/) and the outside world (sim_stubs.cpp), drives
// it through its own API (scenes, menu, SOTA wizard, message/RX/TX notes) and writes each
// 200x200 frame buffer as a PBM image. tools/epd_sim/shots.py turns them into PNGs.
#include "../../src/epaper_techo.cpp"

#include <string>

#include "sim_world.h"

namespace {
DigiConfig gSimCfg;
std::string gOutDir = "out";

void heard(const char *call, float rssi, float snr, uint32_t agoS, double lat = 0, double lon = 0) {
  HeardStation &h = gWorld.heard[gWorld.nHeard++];
  snprintf(h.call, sizeof h.call, "%s", call);
  h.rssi = rssi; h.snr = snr; h.ms = gWorld.nowMs - agoS * 1000u;
  h.hasPos = lat != 0; h.lat = lat; h.lon = lon;
}

void setupWorld() {
  configSetDefaults(gSimCfg);
  snprintf(gSimCfg.callsign, sizeof gSimCfg.callsign, "F4LEK-7");
  gSimCfg.mode = 1;
  gSimCfg.bleEnabled = true;
  snprintf(gSimCfg.sotaPlan, sizeof gSimCfg.sotaPlan, "F/PO-001,,014062,CW");
  snprintf(gSimCfg.sotaLast, sizeof gSimCfg.sotaLast, "F/PO-001,");
  snprintf(gSimCfg.sotaRecent, sizeof gSimCfg.sotaRecent, "F,EA2");
  displayBindConfig(&gSimCfg);
  gReady = true;

  GpsData &g = gWorld.gps;
  g.fix = true; g.lat = 42.77389; g.lon = -0.14722; g.altM = 3298; g.altValid = true;
  g.sats = 9; g.satsInView = 12; g.hdop = 0.9f; g.speedKmh = 0;
  g.timeValid = true; g.utcH = 10; g.utcM = 42; g.utcS = 15;
  g.dateValid = true; g.utcYear = 2026; g.utcMonth = 10; g.utcDay = 18;
  g.lastFixMs = gWorld.nowMs;
  gWorld.batV = 3.98f;

  gSens.wxOk = true; gSens.tempOk = gSens.humOk = gSens.pressOk = true;
  gSens.tempC = 8.4f; gSens.hum = 61; gSens.pressHpa = 702.3f;
  gRx = 142; gTx = 23; gDg = 0;

  heard("F4ABC-9", -97, 6.2f, 40, 42.70, -0.05);
  heard("EA2XYZ-10", -104, -3.5f, 300, 42.81, -0.40);
  heard("F1XYZ-7", -88, 9.0f, 900, 43.10, -0.20);
}

void save(const char *name) {
  const std::string path = gOutDir + "/" + name + ".pbm";
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); return; }
  fprintf(f, "P4\n%d %d\n", EPD_W, EPD_H);
  // PBM: 1 = black. The driver buffer: 0 = black. Rotation forced to 0 = identity.
  for (int i = 0; i < EPD_BUFSZ; i++) fputc((uint8_t)~gBuf[i], f);
  fclose(f);
  printf("%s\n", path.c_str());
}

void shot(const char *name) {
  gRotacion = 0;
  gLinea1[0] = gLinea2[0] = '\0';
  dibujaEscena();
  save(name);
}

void scene(uint8_t n, const char *name) {
  gEscena = n;
  shot(name);
}
// Presses, as the buttons deliver them (see button.cpp): touch = navigate, short = confirm.
void touch(int n = 1) { for (int i = 0; i < n; i++) { if (sotaIsOpen()) sotaNavigate(); else menuNavigate(); } }
void press() { if (sotaIsOpen()) sotaShort(); else menuShort(); }
void later(uint32_t s) { gWorld.nowMs += s * 1000u; gWorld.gps.lastFixMs = gWorld.nowMs; }

// Moves the wizard cursor to the option whose label is `want` (exact match).
void pick(const char *want) {
  char b[24];
  for (int guard = 0; guard < 300; guard++) {
    if (strcmp(sotaWizardOptionLabel(sotaWizardCursor(), b, sizeof b), want) == 0) return;
    sotaNavigate();
  }
  fprintf(stderr, "option %s not found\n", want);
}
}  // namespace

int main(int argc, char **argv) {
  if (argc > 1) gOutDir = argv[1];
  setupWorld();
  gSimCfg.smartBeaconPreset = 1;   // walking: the SOTA profile
  gSimCfg.sotaPlan[0] = '\0';      // the tutorial plans the activation from scratch

  // --- 1. Main menu -> SOTA section -------------------------------------------------
  menuOpen();                 shot("10_menu_main");
  touch(2);                   shot("11_menu_main_sota");
  press();                    shot("12_menu_sota");
  touch(2);                   shot("13_menu_sota_plan");

  // --- 2. Plan the activation: F/PO-001, 14.062 CW ------------------------------------
  press();                    shot("20_plan_assoc");
  pick("* F");                shot("21_plan_assoc_f");
  press();  pick("PO");       shot("22_plan_region");
  press();  pick("0");        shot("23_plan_digit1");
  press();  pick("0");
  press();  pick("1");        shot("24_plan_digit3");
  press();                    shot("25_plan_recap");
  press();  pick("0");
  press();  pick("1");
  press();  pick("4");        shot("26_plan_freq");
  press();  pick("0");
  press();  pick("6");
  press();  pick("2");
  press();  pick("CW");       shot("27_plan_mode");
  press();                    shot("28_plan_prefix");
  press();                    shot("29_plan_confirm");
  press();                    // saves the plan, back in the menu
  menuClose();
  gEscena = 1;                shot("30_sota_planned");

  // --- 3. Spot from the summit --------------------------------------------------------
  later(1800);
  menuOpen(); touch(2); press(); touch(3);  shot("40_menu_sota_spot");
  press();                    shot("41_spot_comment");
  pick("QRV");                shot("42_spot_comment_qrv");
  press();                    shot("43_spot_confirm");
  press();                    // sends to APRS2SOTA
  menuClose();
  gEscena = 1;                shot("44_sota_sending");
  later(40);
  displayNoteMsg("APRS2SOTA", "Spotted F4LEK/P on F/PO-001 14.062 CW{AB");
  gEscena = 1;                shot("45_sota_spotted");

  // --- 4. Carousel ----------------------------------------------------------------------
  later(300);
  displayNoteRx("F1XYZ-7", -88, 9.0f, "Bcn");
  displayNoteRx("EA2XYZ-10", -104, -3.5f, "Bcn");
  later(60);
  displayNoteRx("F4ABC-9", -97, 6.2f, "Msg");
  displayNoteMsg("F4ABC-9", "Heard you 599 on 14.062, GL!{12");
  later(90);
  displayNoteTx("BCN");
  scene(0, "50_home");
  scene(2, "51_messages");
  scene(3, "52_stations");
  scene(4, "53_last_rx");
  scene(5, "54_last_tx");
  gWorld.usb = true;
  scene(0, "55_home_usb");
  gWorld.usb = false;

  // --- 5. Bluetooth -----------------------------------------------------------------------
  menuOpen(); touch(12);      shot("60_menu_main_bluetooth");
  press(); touch(2);          shot("61_menu_bluetooth");
  menuClose();
  return 0;
}
