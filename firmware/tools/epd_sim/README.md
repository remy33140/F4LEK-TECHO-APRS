# E-paper screenshot simulator

Pixel-exact screenshots of the T-Echo screens without the device: the **real**
`src/epaper_techo.cpp` is compiled for the PC (hardware replaced by `stubs/`, GPS/radio/battery
by `sim_stubs.cpp`) and driven through its own API, like the buttons do. `sim_main.cpp` holds the
scenario (menu, SOTA plan wizard, spot, carousel, Bluetooth menu).

```sh
./build.sh                     # needs g++ and the ArduinoJson copy fetched by `pio run`
./epd_sim out                  # writes out/*.pbm (200x200, 1 bit)
python3 shots.py out ../../../docs/tutorial/screens   # e-paper looking PNGs (needs Pillow)
```
