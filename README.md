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
| Movies drawn at 4:3 with black bars instead of stretched | experimental |
| First menu sound follows the SFX volume (it was hard-coded above maximum) | tested (memory read) |

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
| `0x557BC0` / `0x557C0F` | Text placeholder expansion (`%b`, `%j`, ...). Redirected to return icon glyphs. |
| `Font_UI_Small.fnt` | Only font with the Xbox button glyphs (`0x80–0x83`, `0xA2–0xA7`); redrawn as PlayStation symbols by `tools\patch_font.py`. |
| `0x5FE480` | Vibration output, an empty `ret 8` on PC; called by the vibration manager (`0x551350`). |
| `0x61DBC0` / `0x61DC2B` | Direct3D 8 device create/reset; dereferenced the device without checking `CreateDevice`. Patched to check and retry (keeps the Widescreen Fix hook at `0x61DC12`); while the game window is inactive (alt-tab, lock screen) `CreateDevice` returns `D3DERR_DEVICELOST`, so it waits for the window to be active again. |
| `0x6125F0` | Device release-and-recreate, called from the main loop, resolution change and `WM_ACTIVATEAPP` (`0x61286B`). Guarded against re-entry from messages pumped during the wait. |
| `0x6AF6F0` | Pause-menu item table; a fifth item opens the manual save screen (screen 3). |
| `0x5515A0` / `0x551570` | UI scale (`0x6AEA00/04`, set once to 1.0) and safe rect (`0x7280F0..FC`). Hooked: scale = height / 480, safe-rect margins scaled to match. |
| `0x4CB74D` | Unanchored UI X (`xor ecx,ecx`); offset by `(width − height·4/3) / 2` so full-layout screens are centred. |
| `0x4DD720` / `0x609FD0` | HUD render thunk and 2D batch flush. The HUD sizes are raw 640×480 pixels, so the HUD pass runs in a virtual 480-high space (scale 1.0, screen and safe rect divided by height/480) and its pre-transformed vertices are multiplied back at flush. |
| `0x68207C` → `0x568390` | Episode-select screen render (vtable slot). Its map icons and bullet/trail images have scaled positions but raw sizes; wrapped in the same virtual pass. |
| `0x60E88B` | Bink movie quad drawn at `0,0,W,H`; narrowed to 4:3 after clearing the screen to black. |
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
