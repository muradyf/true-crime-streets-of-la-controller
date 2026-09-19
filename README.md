# True Crime: Streets of LA — Controller Mod (PC)

Native analog controller support for the 2004 PC version of *True Crime: Streets of LA*, with DualSense button prompts,
rumble, adaptive triggers and a few engine fixes. Works with a PS5 DualSense (USB or Bluetooth, no DS4Windows needed)
and with any XInput (Xbox) controller.

The PC port ships with the Xbox version's controller code, but the gameplay loop never reads the joystick — which is why
the game is listed as having "no controller support". This mod was built by reverse engineering the executable and
feeds the controller into the same input block the Xbox version used, so sticks, camera, steering and throttle are
fully analog.

## Features

| Feature | Status |
|---|---|
| Analog movement, camera, steering and throttle | tested in game |
| Original Xbox button layout (from the Xbox manual), translated to PlayStation buttons | tested in game |
| DualSense over USB and Bluetooth, XInput fallback | tested |
| DualSense button icons in tutorials and help text | experimental |
| Rumble (the game computes vibration, the PC build discarded it) | experimental |
| Lightbar colour and R2 resistance when a weapon is out | experimental |
| Fix for the crash when the game re-creates its graphics device (e.g. after a mission) | experimental |
| "save game" option in the pause menu | experimental |
| Menus, HUD and text scaled for high resolutions; full-screen layouts centred in 4:3 | tested (2560×1600 menus) |
| In-game HUD (portrait, street sign, radar, meters) and the episode-select map scaled with the screen height | tested (2560×1600) |
| Separate menu and HUD size settings (`MenuScale`, `HUDScale`, percent of filling the screen height; defaults 90 / 75) | tested (2560×1600: 3.0× / 2.5×) |
| Movies, the startup loading screen and the menu background drawn at 4:3 with black bars instead of stretched; menu items kept inside the 4:3 frame; city map keeps its proportions | tested (2560×1600, compared with 1024×768 PC screenshots) |
| City map markers (player, destinations) placed correctly on the centred map | tested (2560×1600) |
| Faster alt-tab back into the game: the DWM transition wait is capped and the resource reload no longer sleeps per resource (gameplay ~4.9 s → ~1.8–2.2 s) | tested (2560×1600, four alt-tabs per setting) |
| First menu sound follows the SFX volume (it was hard-coded above maximum) | tested (memory read) |
| Options → Display lists the monitor's resolutions (the game offered only five 4:3 sizes up to 1280×960); Apply saves the choice for the next start; MENU SIZE / HUD SIZE rows step the UI sizes live | tested (list, apply + restart at 1920×1080, menu size live) |

## Requirements

- *True Crime: Streets of LA* for PC (`TrueCrime.exe`, 2004).
- An ASI loader (`dinput8.dll` in the game folder). ThirteenAG's
  [Widescreen Fix](https://github.com/ThirteenAG/WidescreenFixesPack) for this game already includes one; otherwise use
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- Python 3 (only for the optional button prompts).

## Install

1. Copy `TrueCrimeDualSense.asi` (from Releases or your own build) and `config\TrueCrimeDualSense.ini` into the game's
   `scripts` folder.
2. Optional, button icons: `python tools\install.py "C:\path\to\True Crime Streets of LA"`.
   It backs up the original font and text files to `scripts\TrueCrimeDualSense-originals` and patches your copy.
   Undo with `--restore`.
3. Close DS4Windows / Steam Input for this game, then start the game normally.

## Controls

Layout follows the original Xbox manual: A→✕, B→○, X→□, Y→△, White→L1, Black→R1, LT→L2, RT→R2, left-stick click→L3.

**On foot / fighting / shooting**

| Button | Action |
|---|---|
| Left stick | Move |
| Right stick | Camera / select target |
| ✕ | Kick (shooting: hold to take cover) |
| □ | Punch |
| △ | Jump kick / roll / dive (hold for slow-motion dive) |
| ○ | Grab, throw, pick up, human shield |
| R2 | Fire (tap draws guns, hold for precision targeting) |
| R1 | Reload / drop weapons |
| L1 (hold) | Block |
| L2 | Get in / commandeer vehicle |
| L3 + □ | Flash badge |
| L3 + R2 | Warning shot |
| L3 + ○ | Arrest / frisk |
| D-pad ← ↓ → | Fighting / normal / shooting mode |
| Left stick (precision targeting) | Move the reticule |

**Stealth**

| Button | Action |
|---|---|
| ✕ (hold) | Take cover |
| △ | Roll |
| □ | Stun attack |
| ○ | Deadly attack |
| R2 | Tranquiliser gun |

**Driving**

| Button | Action |
|---|---|
| Left stick | Steer |
| Right stick ↑ / ↓ | Accelerate / brake (analog) |
| ✕ / □ | Accelerate / brake |
| ○ | Handbrake |
| △ | Rear view |
| L2 | Get in / out |
| R2 | Fire |
| D-pad ↑ / ↓ | Siren / horn |
| D-pad ← → | Change view |

Options pauses, the touchpad (or Create) opens the map, R3 centres the camera on foot and skips the radio song in cars.
`TriggersDrive=1` switches cars to R2/L2 analog gas/brake.

**Remapping:** Options → Controls → **Controller** (the former Mouse Controls page) shows the button for every action in
each mode (on foot, fighting, guns, driving, stealth). D-pad / left stick move the blinking cursor, ✕ edits the selected
cell (then press the new button; L3 + a face button or R2 makes a combo, Options cancels), △ restores the default
layout, ○ goes back. Custom bindings are saved to `[ControllerMap]` in `TrueCrimeDualSense.ini`. Sticks, Options and
the touchpad are not remappable.

## Configuration

All settings are in `scripts\TrueCrimeDualSense.ini` (stick deadzone, camera inversion, driving layout, precision-aim
speed, rumble strength, trigger effects, lightbar colour, and switches for each fix). Set `DebugLog=1` to write
controller and input values to `scripts\TrueCrimeDualSense.log` when reporting a problem.

## How it works

| Address | What it is |
|---|---|
| `0x5EA160` → `0x5E9350` | Per-frame input update; fills the input block from keyboard and mouse. Hooked: run the original, then merge the controller. |
| `0x72BAC0` | Xbox-style input block: `+04` held action bits, `+08` system flags, `+0C` pressed bits, `+10/+14` camera, `+18/+1C` movement (signed bytes, deadzone, /128). |
| `0x752818` | Keyboard bindings table (same order as `[Keyboard2]` in `TrueCrime.ini`), used to identify every action bit. |
| `0x6D9578` | Player; `+09` control state (1 on foot, 2 shooting, 3 fighting, 4 stealth, 5 driving), `+D68 == 3` precision targeting. |
| `0x61E6A0` | Controller identification by DirectInput product name (a whitelist of 2004 gamepads). The joystick is created and configured but only read in the button-rebinding screen. |
| `0x682F38` (Mouse Controls screen) | Per-mode grid of action rows (masks at `+0x368`) backed by the `[Mouse2]` button masks. Repurposed as the controller remap screen: cell labels (`0x56EC10`), capture type 3 → 4 so the game stops polling the mouse (`0x56EDF9`), assignment (`0x56EE4A`), DEFAULT (jump table `0x580258`), update slot `0x682F54` for controller navigation, titles via string ids `0xD95`/`0xD8C`. |
| `0x557BC0` / `0x557C0F` | Text placeholder expansion (`%b`, `%j`, ...). Redirected to return icon glyphs. |
| `Font_UI_Small.fnt` | Only font with the Xbox button glyphs (`0x80–0x83`, `0xA2–0xA7`); redrawn as PlayStation symbols by `tools\patch_font.py`. |
| `0x5FE480` | Vibration output, an empty `ret 8` on PC; called by the vibration manager (`0x551350`). |
| `0x61DBC0` / `0x61DC2B` | Direct3D 8 device create/reset; dereferenced the device without checking `CreateDevice`. Patched to check and retry (keeps the Widescreen Fix hook at `0x61DC12`); while the game window is inactive (alt-tab, lock screen) `CreateDevice` returns `D3DERR_DEVICELOST`, so it waits for the window to be active again. |
| `0x54EFAD` | Crash at any resolution other than the desktop's (also without mods): vertices are written into a locked vertex buffer with `movaps`, which needs 16-byte alignment the lock pointer doesn't always have. The four writes are changed to `movups`. |
| `0x61DBE2` / Present | Exclusive fullscreen at a non-desktop resolution rendered black: the device is already lost when `CreateDevice` returns (`TestCooperativeLevel` = `D3DERR_DEVICENOTRESET`, every `Present` fails with `D3DERR_DEVICELOST`), and re-creating it is lost the same way. `BorderlessFullscreen=1` (default) runs such resolutions in a borderless desktop-sized window with `D3DSWAPEFFECT_COPY` (`2` = every resolution; the windowed device is created in ~100 ms and kept across alt-tab) and presents the back buffer into an aspect-correct rectangle with black bars. |
| `0x6216C4` | `[Renderer] Windowed` is read with a registry flag but no registry key is ever opened, so it always falls back to 0 and is written back to the ini. `ForceWindowed=1` (debug) changes that default to 1. |
| `0x6125F0` | Device release-and-recreate, called from the main loop, resolution change and `WM_ACTIVATEAPP` (`0x61286B`). Guarded against re-entry from messages pumped during the wait. |
| `0x6AF6F0` | Pause-menu item table; a fifth item opens the manual save screen (screen 3). |
| `0x61D995` / `0x6B16C8` / `0x571555` / `0x6AF67C` | Options → Display. The renderer kept only D3D modes matching a 15-entry table (`0x68D5A8`: 640×480 … 1280×960 × 16/24/32 bits) and the menu listed a 5-entry table; both now use every size ≥ 640×480 (menu: up to 16, desktop shape, 16:9 and classic 4:3 first). The Widescreen Fix NOPs the size writes of Apply and forces the back buffer size at `0x61DC12`, so Apply writes `TrueCrime.ini` and the fix's `ResX`/`ResY` for the next start. Adapter is greyed by the game when D3D reports one adapter. Opening the screen also runs the game’s own list refresh (`0x571430` with `0`) so the lists start on the current settings; that refresh stores its search counter even when the size is not in the list, and the entry lookup (`0x55CE60`) then dereferences the null it produced (`0x55CE93`), so it is skipped unless the current size is actually listed. Three rows appended to the Display item table (text ids `0xD70`/`0xD71`/`0xD72`, blank lines in every `TCPC??.txt`) cycle `MenuScale`, `HUDScale` and `SubtitleScale`; the subtitle row has an extra AUTO step (`0`) that follows the menu size. |
| `0x5515A0` / `0x551570` | UI scale (`0x6AEA00/04`, set once to 1.0) and safe rect (`0x7280F0..FC`). Hooked: scale = height / 480, safe rect placed inside the centred 640×480 box with scaled margins, so right-aligned menu items line up with the logo and art instead of the real screen edge. |
| `0x4CB74D` | Unanchored UI X (`xor ecx,ecx`); offset by `(width − height·4/3) / 2` so full-layout screens are centred. |
| `0x4DD720` / `0x609FD0` | HUD render thunk and 2D batch flush. The HUD sizes are raw 640×480 pixels, so the HUD pass runs in a virtual 480-high space (scale 1.0, screen and safe rect divided by height/480) and its pre-transformed vertices are multiplied back at flush. |
| `0x4CB7F0` | Layout rect builder for menu images, sprites and the logo: positions went through the anchors, but width was `w × [0x6A6FDC]` (1.0) and height raw. Width now uses the UI scale (`0x4CB812`) and height goes through a stub (`0x4CB7F9`). |
| `0x55AEA0` | Shell streak lines: Y was `y × scale` with no layout offset, thickness and length raw. Stubs at `0x55B0DD` / `0x55B10D` and the displacement at `0x55B119` add the offset and scale both. |
| `0x68207C` → `0x568390` | Episode-select screen render (vtable slot). Its map icons and bullet/trail images have scaled positions but raw sizes; wrapped in the same virtual pass. |
| `0x60E88B`, `0x4E7CBE`, `0x4E7D43`, `0x610AC9`, `0x6113C5` | Full-screen quads at `0,0,W,H`: the Bink movie frame and `Title.xpr` (the loading screen after the intro movies). Narrowed to 4:3 after clearing the screen to black. |
| `0x55DEBA` / `0x55DFAC` | Shell background (`ShellBG.xpr`) drawn through `0x4CB1A0` with a `W+1 × H+1` rect; narrowed to the centred 4:3 box, side bars cleared to black. |
| `0x4D5BCE` / `0x4D8495` | City map (`UI_Map.xpr`) renders size the map and its markers with `W/640` horizontally and `H/480` vertically, stretching it on wide screens. Both scales now use `H/480`; the existing `(W − width)/2` centring does the rest. |
| Process DPI awareness | Windows reports a scaled-down screen to a process that has not declared itself DPI aware: at 150% scaling a 2560×1600 display reads as 1707×1067, so the game renders small, the desktop stretches the result, and the made-up size is saved to `TrueCrime.ini` as a resolution no display can set. `SetProcessDPIAware` is called at load (`DpiAware=1`) instead of relying on the per-executable **HIGHDPIAWARE** compatibility flag, which is keyed to the game’s full path and is lost if the folder is ever moved or renamed. |
| `0x5F2C10` / `0x66191C` | Starting a voice Plays its DirectSound buffer before the category volume is applied (`0x5F2800`), so the first use of each sound (e.g. the first menu enter/back transition) played at full volume. The voice's category volume is now set on the buffer just before `Play`. |
| `0x4E71E8` | Sound init sets the menu category (`0x20`) to 0.65 before the ini volumes are applied; the SFX slider maps 0–10 to 0–0.585. The volumes are now applied right after it. |

DualSense report formats follow the Linux `hid-playstation` driver (Bluetooth output reports need a CRC32 with seed
`0xA2`, and Windows requires writes padded to the device's largest output report).

## Building

Run `build.bat` (Visual Studio with the C++ workload). Output: `build\TrueCrimeDualSense.asi` and `build\ds_test.exe`
(prints raw DualSense state, useful for checking the controller outside the game).

## Credits

- ThirteenAG — Widescreen Fix and Ultimate ASI Loader.
- Linux `hid-playstation` driver — DualSense report layouts.
- *True Crime: Streets of LA* Xbox manual — original control scheme.

Not affiliated with Activision or Luxoflux. No game files are included; you need your own copy of the game.

## License

MIT — see [LICENSE](LICENSE).
