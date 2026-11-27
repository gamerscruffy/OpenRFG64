# OpenRFG

## Quick start

- [Original bugs](original_bugs.md)
- [Port deviations](deviations.md)

```
git clone https://github.com/wongfei/OpenRFG.git
cd OpenRFG

cmake -S "." -B "build\x86" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOPENRFG_GAME_DIR="C:/Program Files (x86)/Steam/steamapps/common/Red Faction Guerrilla Re-MARS-tered"

cmake --build "build\x86"

set SHIM_MCCOMPAT=0x800000001
"build\x86\src\openrfg\OpenRFG.exe" -gamebuild "C:\Program Files (x86)\Steam\steamapps\common\Red Faction Guerrilla Re-MARS-tered\data"
```

## Building VOM (64-bit)

VOM-edition builds a 64-bit `OpenRFG.exe` with optional backends: the path tracer (RENDERER: PATH TRACED), DLSS, DLSS Frame Generation and NVIDIA PhysX debris. If a backend's requirements are missing, it is left out and the game still builds. CMake's output lists what was built.

You need:

- Visual Studio 2022 or 2026 with *Desktop development with C++* and a Windows 10/11 SDK. The SDK's `fxc` and `dxc` compile the path tracer's shaders.
- An NVIDIA RTX card for path tracing and DLSS. Frame generation needs an RTX 40 or 50.
- Internet access the first time you configure. CMake downloads NVIDIA's DLSS SDK and Streamline into `external/rfgrt/third_party`: pinned releases, checked, and kept out of git because of their licences. `-DOPENRFG_FETCH_SDKS=OFF` skips the download.

From an *x64 Native Tools Command Prompt*:

```
cmake -S "." -B "build\x64" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOPENRFG_GAME_DIR="C:/Program Files (x86)/Steam/steamapps/common/Red Faction Guerrilla Re-MARS-tered"

cmake --build "build\x64"

tools\bink_helper\build_bink_helper.bat

set SHIM_MCCOMPAT=0x800000001
"build\x64\src\openrfg\OpenRFG.exe" -gamebuild "C:\Program Files (x86)\Steam\steamapps\common\Red Faction Guerrilla Re-MARS-tered\data" --no-steam
```

`build_bink_helper.bat` builds the 32-bit helper that plays the game's videos; without it they stay black. If Visual Studio is installed elsewhere, edit the batch file's `VCVARS` line.

**PhysX** (optional): build the PhysX SDK with rfgphys's patch from [RedFactionGuerrillaRemarstered-PhysX](https://github.com/slopkhayzo/RedFactionGuerrillaRemarstered-PhysX). Its README explains `tools\build_physx.cmd`, which needs CUDA 12.8. Then add `-DOPENRFG_PHYSX_INSTALL=<that repository>\third_party\physx-build\release-gpu\install` to the first command. Without it, PHYSICS offers only Havok.

The backends' files go into `build\x64\src\openrfg\backends\`. Their settings are on *Display > General* (RENDERER, PHYSICS, KEEP DEBRIS, DLSS, FRAME GENERATION) and can only be changed from the main menu.

## Debug command line arguments

```
--game-trace trace.log
--notitle # skip intro and title
--sp # start new game
--sp-load N # load game from slot N
--no-steam # disable steam
--host # host LAN game
--players N # wait for N players to join
--join # join LAN game
--ip 127.0.0.1
--port 1005
--name Player
--window-pos 0 0
```

## Multiplayer debug scenario

```
set SHIM_MCCOMPAT=0x800000001

start "OpenRFG server" "build\x86\src\openrfg\OpenRFG.exe" -gamebuild "C:\Program Files (x86)\Steam\steamapps\common\Red Faction Guerrilla Re-MARS-tered\data" --no-steam --name Host --host --players 2 --window-pos 0 0

ping -n 6 127.0.0.1 >nul

start "OpenRFG client" "build\x86\src\openrfg\OpenRFG.exe" -gamebuild "C:\Program Files (x86)\Steam\steamapps\common\Red Faction Guerrilla Re-MARS-tered\data" --no-steam --name Client --join --window-pos 1200 0
```
