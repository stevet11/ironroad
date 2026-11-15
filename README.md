# IRON ROAD
Death Track style 3D combat racer. C + raylib 5.5. Linux and Windows.

## Build
**CMake (Linux, Windows/Visual Studio, MSYS2):**
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
raylib is taken from `third_party/raylib-5.5` if present, otherwise downloaded automatically.
Run from the project folder (`build/ironroad`, or `build\Release\ironroad.exe`).

**Plain make (Linux, or MSYS2 with `make OS=Windows_NT`):** `make && ./ironroad`

## Campaign
10 races on 10 different maps (Rust Valley, Neon City, Frozen Pass, Toxic Swamp, Canyon Run,
Volcano Ring, Harbor Docks, Moon Base, Jungle Ruins, Death Track). Finish a race to earn prize
money (+$250 per kill), then spend it in the garage: armor, front/rear spikes, machine gun,
missiles, laser, ammo bay. AI cars get tougher and better armed each race.
Progress auto-saves to `ironroad.sav`. Cars crumple where they are shot or rammed.

## Controls
W/S throttle/brake, A/D steer, Space handbrake, F/Ctrl/LMB gun, G homing missile,
H laser, B mine, V caltrops, R reset, P pause, Esc menu/quit.
Garage: Up/Down select, Enter buy, Space start race.
