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

## Game modes
Pick one on the title screen (each has its own save file, `ironroad_arcade.sav` / `ironroad_classic.sav`).

**Arcade** - the original. Wrecked cars respawn, ammo and armor refill every race, you start on the front
row with a fast car. Earn cash, buy armor, spikes, guns, missiles, laser, ammo bay.

**Classic** (Death Track style)
- No respawns, for you or the enemies. If you are destroyed the race is lost: you keep kill bounties, your
  wreck stays damaged and you retry the same track. Kill every enemy and you win on the spot.
- Damage and ammo carry over. Repairs ($5 per armor point) and every missile, laser cell, mine, caltrop and
  box of bullets cost money. Power-ups on the track are rare (3 per race, single use).
- A stock car cannot outrun the AI. Buy **engine**, **transmission** and **tires** to catch them.
- You start at the very back of the grid until you have won a race on that track; then you start at the front.
  (A sponsor tops your cash up to $600 if you go broke.)

## Tracks
10 races on 10 maps. Sharp corners get yellow diamond warning signs ahead of them (orange double-chevron
for hairpins) and chevron boards around the bend. Canyon Run, Harbor Docks and Death Track have hairpins;
Jungle Ruins and Death Track have a flyover where the road crosses over itself.
Track layouts live in `tools/gen_tracks.py` (it validates them and writes `tracks_data.h`).

## Controls
W/S throttle/brake, A/D steer, Space handbrake, F/Ctrl/LMB gun, G homing missile,
H laser, B mine, V caltrops, R reset, P pause, Esc menu/quit.
Garage: Up/Down select, Enter buy, Space start race.
