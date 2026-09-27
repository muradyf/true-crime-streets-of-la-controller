# TrueCrimeDualSense — agent notes

This file is the one shared memory for every coding agent working on this repo (Codex, Claude Code, anything else). Write new notes here, not in CLAUDE.md or an app's own memory. `CLAUDE.md` is just `@AGENTS.md`.

`README.md` is the user-facing doc and already carries the full feature list, controls and the address table ("How it works"). This file holds what a developer needs on top of that: how to build, install and test on this machine, decisions, gotchas, and open items. Don't duplicate the README's address table here; extend it there.

Last updated 2026-09-27 (migrated from Claude Code memory + session transcripts, 2026-09-14 .. 2026-09-20).

---

## 1. State

- Public repo: https://github.com/muradyf/true-crime-streets-of-la-controller (MIT, copyright Murad Yousuf). `main` is the only live branch; `sr1-0b` and `sr1-5f` are old worktree branches, fully merged, worktrees removed.
- Releases: `v1.0.0` and `v1.0.1` (2026-09-16). Release zip = `TrueCrimeDualSense-vX.zip` containing `scripts\TrueCrimeDualSense.asi` + `.ini`, `tools\*.py`, README, LICENSE; built from `dist\` (gitignored). Everything on `main` after v1.0.1 (DPI awareness, Display list guard, reticle/subtitle/counter/menu-background/UI-aspect work, HUD glyph narrowing) is pushed but **not in a release yet**.
- Creating releases, pushing, or changing repo visibility are public actions: ask Murad first, every time.

## 2. Layout

- `src\tcla_dualsense.cpp` — DllMain, ini reads, install order, input hook, controller merge. Feature files are headers included from it: `dualsense_hid.h` (HID reports), `extras.h` (rumble, lightbar), `device_fix.h`, `borderless.h`, `device_profile.h`, `d3dtrace.h`, `ui_fix.h` (UI scale, layout offset, menu background, counter anchor), `hud_fix.h` (HUD virtual pass, reticle), `subtitle_fix.h` / `subtitle_trace.h`, `display_fix.h` (Options > Display), `controls_menu.h`, `controller_map.h`, `remap_screen.h`, `pause_save.h`, `sound_fix.h`, `shot.h` (debug screenshot trigger).
- `tools\install.py` (patches font + text in the player's copy, backs up originals, `--restore`), `patch_font.py`, `patch_text.py`, `ds_test.cpp` (standalone DualSense HID reader).
- `config\TrueCrimeDualSense.ini` — shipped defaults, every setting documented inline. When adding a setting, document it there.
- Never commit game files (`*.fnt`, `TCPC*.txt` are copyrighted; `.gitignore` blocks them). `tools/patch_text.py` embeds a few one-line game strings as search patterns (fair functional use; could become hashes if that ever matters).

## 3. Build and install

- Build: `build.bat` (finds VS via vswhere, `vcvarsamd64_x86.bat`, x86 `cl`). Output `build\TrueCrimeDualSense.asi` and `build\ds_test.exe`. Prints `BUILD OK` on success.
  - Run it from PowerShell (`cmd /c build.bat`). Launching it from Git Bash hung twice and left stray background builds.
  - Only install when the output says `BUILD OK` (a stale .asi was installed once after a failed build).
- Game install: `C:\Games\True Crime Streets of LA\` (renamed on 2026-09-20 from `C:\Games\TrueCrimeStreetsOfLA`; older notes and commit messages use the old path). Mod files go in `<game>\scripts\`: `TrueCrimeDualSense.asi`, `TrueCrimeDualSense.ini`, `TrueCrimeDualSense.log`. Loaded by the Widescreen Fix's ASI loader (`dinput8.dll`, `scripts\TrueCrimeStreetsofLA.WidescreenFix.asi/.ini`).
- **The installed ini is Murad's personal config — don't overwrite it with the repo default.** Add new keys to it instead. His values as of 2026-09-27: `MenuScale=60`, `HUDScale=60`, `UIPixelAspect=80`, `MenuBackgroundAspect=0`, `MenuBackgroundShape=170`, `MenuFillWidth=0`, `MenuSpread=50`, `SubtitleScale=125`, `CameraSpeed=50`, `CameraCurve=200`, `ReticleScale=2`, `ReticleBox=0`, `BorderlessFullscreen=2`.
- Font/text patch: `python tools\install.py "C:\Games\True Crime Streets of LA"` (undo with `--restore`; originals in `<game>\scripts\TrueCrimeDualSense-originals\`). The button-symbol glyphs are drawn pre-widened for the installed `UIPixelAspect` (the installer reads it from the ini) — **re-run the installer whenever `UIPixelAspect` changes.** The text step fails harmlessly if the text files were already converted.
- The repo is the source of truth. `C:\Games\_Setup\True Crime Streets of LA\Mods\TrueCrimeDualSense\` holds an older source copy and the original font/text backups from the first builds — never build from it, never build from a scratch copy.

## 4. Game environment on this machine

- PC version (2004), MagiPacks repack, installed by Murad (agents don't run repack installers). Display: 2560x1600, 150% Windows scaling, 240 Hz. GPU: RTX 4070 + Intel Iris Xe; native D3D8 picks the Iris Xe, dgVoodoo reports the RTX.
- dgVoodoo2 is installed in the game folder (`D3D8.dll` + `dgVoodoo.conf`, since 2026-09-15). **dgVoodoo masks native d3d8 bugs — test device/alt-tab changes on both backends** (move dgVoodoo's `D3D8.dll` aside for the native run, put it back after).
- `BorderlessFullscreen=2` is Murad's choice (2026-09-16). Side effect: borderless forces windowed, so the game persists `Windowed=1` in `TrueCrime.ini` on exit; reset it to 0 before switching back to exclusive fullscreen.
- Resolution: the Widescreen Fix's `ResX/ResY` (0 = desktop) override `TrueCrime.ini`; resolution changes need a restart.
- DPI: the game used to rely on a per-exe `HIGHDPIAWARE` AppCompat flag keyed to the old full path (the HKCU entry is now orphaned). The mod calls `SetProcessDPIAware` (`DpiAware=1`). Without it the game reads 2560x1600 as 1707x1067 and saves that into `TrueCrime.ini`.
- `TrueCrime.ini [Controller] Type` must stay 0. Type 5 points at DS4Windows' virtual Xbox pad and crashes when DS4Windows isn't running; the game's own joystick code is only read in the rebinding screen anyway. Close DS4Windows / Steam Input for this game.
- WineD3D was quarantined by Defender (Trojan:Win32/Posilod.CA!cl) on 2026-09-15 and removed. Never restore it or suggest bypassing Defender.
- Saves: `<game>\Saves\save0.bin` (+ `options.bin`). Starting mission 1 autosaves over `save0.bin` (the Resume slot). Backups: `C:\Games\_Setup\True Crime Streets of LA\SavesBackup-2026-09-15` and `SavesBackup-2026-09-16_0343`. For New Game tests swap `Saves` for an empty folder (keep `options.bin`).
- A PS2 copy of the game (`<game>\PS2\...iso`) and PCSX2 (`C:\Games\_Emulators\PCSX2`) were restored on 2026-09-20 only as a visual reference for the UI; PCSX2 per-game ini `SLES-51754_6B9AEA0D.ini`.

## 5. Testing in the game — rules Murad set

The game takes over the whole 2560x1600 screen and he works in other sessions at the same time, so:

1. **Always ask before launching `TrueCrime.exe`** (or PCSX2). Do all offline work first (build, install, static analysis, reading `scripts\TrueCrimeDualSense.log`), then ask once with what needs checking and why. Most questions are answerable from the log.
2. **Lock file** `<game>\scripts\GAME_TURN.txt` (`<session> | <purpose> | <time>`): check it before installing an .asi, editing the mod ini, launching or closing the game; write it while you hold the game; delete it when done. A stale one from 2026-09-20 ("session 25edf4d3, PCSX2") is still there — confirm neither the game nor PCSX2 is running, then delete it.
3. **Be fast.** Poll the log for a marker instead of sleeping (`episode map pass`, `gameplay started`, `subtitles: scale`, `HUD pass`). Menu keys need ~300 ms. Only level loads need real waiting.
4. **Minimise the game whenever it isn't being driven** (`ShowWindow SW_MINIMIZE` + focus back to the agent's window) and **close it straight after the capture.** Borderless + `KeepDeviceOnAltTab=1` makes minimising free.
5. **Only capture when the game is verified foreground** (window rect not -32000); delete any capture that shows non-game content.
6. Path to gameplay without touching saves: **Resume Game (the SECOND main-menu item — never New Game, the first)** → Enter (save 1) → Enter (episode) → Enter twice (mission; skips the cutscene). Skip in-engine cutscenes with an ESC press-and-release (the skip tests the release bit, `0x4BFCEE` tests `0x10`); if it doesn't take, the cutscene is in its no-input window — retry.
7. Synthesized input: `keybd_event` with scancodes works only when the game is foreground; arrow keys need `KEYEVENTF_EXTENDEDKEY` or DirectInput reads them as numpad; this only worked when sent from the PowerShell tool, not from `powershell.exe` launched out of Git Bash. Right arrow on a yes/no prompt selects YES.
8. Capture options: the mod's trigger file `scripts\TrueCrimeDualSense.shot` (Present-hook back buffer, most reliable), DXGI capture, GDI `PrintWindow` (windowed only). Helper scripts from earlier sessions (`game.ps1`, `gameshot.ps1`, `ddcap.exe`, `alttab-test.ps1`, `poke.ps1`) lived in session scratchpads and are probably gone.
9. Debug pad: a fake-pad file `scripts\TrueCrimeDualSense.pad` drives the controller path without a real pad.
10. **Measure, don't eyeball.** A glyph reported "round" was 13x16 (0.81) — exactly the bug. Check shapes by pixel measurement or by the source texture, and say "unverified" when you couldn't.

## 6. Decisions (Murad's calls)

- Button layout = the original **Xbox** layout from the Xbox manual, translated A→✕ B→○ X→□ Y→△ White→L1 Black→R1 LT→L2 RT→R2 L-click→L3. Only non-manual extras: R3 centre camera (foot) / skip radio track (car), touchpad/Create = map. `TriggersDrive=0` default.
- Adaptive trigger resistance was built and then **removed** ("too hard / annoying").
- A "KEY | BUTTON" controls page was reverted as confusing; the controller remap screen (repurposed Mouse Controls page) replaced it.
- Exclusive vs borderless: kept exclusive fullscreen at first, then chose **borderless** after the alt-tab measurements.
- PS2-look city map (black behind the map) was built, merged and then **reverted** at his request (`4f78dfa`). PS2 captures are still the reference for how UI should look, but ask before more PS2-look work.
- Main menu: **no black bars.** The background keeps its shape and is scaled to cover (cropping overflow): `MenuBackgroundAspect=0` + `MenuBackgroundShape=170`.
- UI sizes are shown to him as multipliers in the Display menu (1.67x, 2x, ...), not percents — talk in the menu's units.

## 7. Reverse-engineering notes not in the README

- Input block `0x72BAC0`: `+4` held action bits, `+8` system flags (0x100 confirm/Enter, 0x220 back/Esc press, 0x10 pause on Esc release, 0x20 map), `+0xC` pressed edge, `+0x10/+0x14` camera X/Y (driving: throttle), `+0x18/+0x1C` move X/Y (negative = up). Signed bytes, deadzone 24-48, /128. Camera X must be written **negated** (mouse path `0x5E987D` stores -mouseX). In precision aim (`[player+0xD68]==3`) `+0x18/+0x1C` are full-int per-frame deltas, value/1024 = degrees per frame.
- Action bits (verified against the live bindings table `0x752818`): Jump 0x11, Kick 0x402, Punch 0x804, Block 0x20, Grab 0x48, Frisk 0x40, Arrest 0x80, FlashBadge 0x200, Cover 0x4000, Reload 0x2000, Fire 0x1000, WarningShot 0x8000100, Commandeer 0x8000, CenterCam 0x10000, Normal/Fight/Gun mode 0x20000/0x40000/0x80000, Accel 0x100000, Brake 0x200000, Handbrake 0x400000, Horn 0x800000, Siren 0x1000000, CarCamera 0x2000000, RearView 0x4000000, SkipTrack 0x10000000.
- `[0x70CE48]` < 2 = gameplay controls, ≥ 2 = menu. Stealth: ○ = Grab (deadly), □ = Punch (stun).
- Camera: the axis is a signed byte, so a low `CameraSpeed` leaves few steps and the camera moves in jumps; `CameraCurve` (v·|v| at 200) gives fine control near centre instead.
- Button prompts: `%<c>` tokens expanded at `0x557BC0` (action = c-'a', binding via `0x56B920`, name via `0x61E5C0`); the call at `0x557C0F` is redirected to return glyph codes. `Font_UI_Small.fnt` (A4R4G4B4 256x128, records at `0x18+(ch-0x20)*8`): 0x80 ✕, 0x81 △, 0x82 ○, 0x83 □, 0xA3/0xA4 L/R stick; L1/R1/L2/R2 drawn into unused wide slots 0x98-0x9B via 0xA6/0xA7/0xA2/0xA5. Tutorial text lines 504, 514, 1977 rewritten mouse→left stick in every `TCPC??.txt` (string id = line − 1).
- Pause-menu save: header `0x6AF6F0`, items ptr `0x6AF704`, count `0x6AF708`, 20-byte records `{1,textId,callback,arg,0}`; added `{1,0x46,0x558670,3,0}` (manual save screen 3). The main menu has no save item.
- Alt-tab: the "cut out" after missions was really alt-tab / lock-screen device loss (`WM_ACTIVATEAPP` handler `0x61286B`; CreateDevice returns DEVICELOST while not foreground). Slow alt-tab root cause: Windows `d3d8.dll` waits on `DWM_DX_FULLSCREEN_TRANSITION_EVENT` for 3000 ms (d3d8+0x2580D), never signalled → `FullscreenTransitionWait` caps it (0 gives black DXGI captures; 250-2000 fine). Restore reloads each resource from disk; the requester polls with Sleep(5) (`0x60E0CE`) and the reader thread sleeps 10 ms when idle (`0x60DE4C`); spinning starves the reader through a shared critical section, so only Sleep(1) + timeBeginPeriod helped (`RestorePollFix`). Reset can never work: VB/IB are `DYNAMIC|WRITEONLY` in `POOL_DEFAULT`. `KeepDeviceOnAltTab=1` crashed on **native** d3d8 (0xC0000005 at d3d8+0x4FDE3) — now only keeps a windowed device with no Reset attempt. Measured: gameplay alt-tab 4.9 s → 1.8-2.2 s exclusive; borderless + keep = no frame gap. `FrameGapCheck` logs gaps. `D3DTrace=1` once made CreateDevice hang 81 s (debug only).
- Sound: first play of each DirectSound buffer ran at 0 dB because `Play` came before the volume set; `SoundTrace=1` names each voice's sound.
- UI aspect: the PC port's 2D UI was drawn for a 512-wide frame buffer and handed 640, so text and menu art are 25% too wide even at native 640x480 with every fix off (Murad confirmed in game). `UIPixelAspect=80` (512/640) is the derived fix. The menu background has no derivable shape; 170 (1.70) was calibrated against the box cover art (Murad's own manual squash 1024→872 plus a registration of his corrected image at 1.684).
- Menu spread: the 640x480 layout is drawn `640 × s × UIPixelAspect` wide and centred, so a small `MenuScale` plus `UIPixelAspect=80` leaves the menu bunched in the middle (e.g. 1024 of 2560 px). `MenuSpread` widens the anchor box without enlarging text; `MenuFillWidth=1` is its old name (=100).
- HUD text: the HUD pass sets the UI scale globals to 1, so `HUDTextAspect=1` narrows HUD glyphs per font per draw (the street-name banner is left alone — it was already the right shape). Font glyph transform at `[font+0x10]`, gated by bit 0 of `[font+0x22]` (subtitles needed that bit set).
- Button symbols get squashed with the letters because a whole line is one draw with one glyph matrix. Found the seam: `0x60A2C0` is a **per-vertex writer** (`[ecx+0x14]` vertex ptr, pushed vec4 = x/y, two floats = UV), called once per glyph corner from `0x60C134`, `0x60C2DE`, `0x60C482`, `0x60C5F1` with `esi` = glyph record; corner order is not consistent per glyph. Failed approaches (don't retry): writing `[esp+0x1C0]` (holds the font's x scale and is re-read, but doesn't size the quad — traces looked right, pixels unchanged); writing every frame slot holding the letter scale (garbles text). **Shipped instead:** pre-widened symbol glyphs in the font (14x12 in the atlas → ~0.93 on screen after the 0.8 squeeze), via `install.py`.
- Objective counter ("0/10" at the firing range) went missing because the unanchored-path layout offset pushed it off screen; fixed by subtracting the offset in its anchor calls (`0x4DBF09`/`0x4DBF15`). `CounterTrace=1` logs it.
- Reticle: the game draws an even-width box with odd-width bars; `ReticleFix` shrinks the box 16→15 units and collapses the stray corner rect (a zero-size rect draws backwards; `x2 = x1 + 1`). Scaled by whole multiples only (`ReticleScale`, 0 = auto) so a 1-px stroke stays even. `ReticleBox=0` removes the box (replaces `call 0x60A830` at `0x4DD416`). Reference look = the reticle preview in Options > Controls > Mouse settings.
- Display crash: opening Options > Display at an unlisted resolution made `0x55CE60` deref null at `0x55CE93` (TrueCrime.exe+0x15CE93); the refresh now runs only when the current size is listed. Vanilla crash at non-desktop resolutions: `movaps` into an unaligned locked VB at `0x54EFAD` → `movups`.
- Commit messages record dead ends on purpose (e.g. `e29350e`, `726173d`) so they aren't repeated.

## 8. Open items (2026-09-27)

- **Rumble**: implemented (`extras.h`, hook `0x5FE480`, `Rumble=1`, `RumbleStrength=100`, game `Vibration=1`), but never confirmed by feel. Next step: Murad plays and says, or set `DebugLog=1`, drive/fight, and read the `extras: game vibration calls=… nonzero=… | output writes=… errors=…` line.
- **Button symbols in menus, loading-screen hints and the controls screen**: font pre-compensation installed 2026-09-20 and measured in the atlas, **not yet confirmed on screen.**
- Portrait (HUD face) still stretched ~1.29; radio stream path (sound id) traced but not fixed.
- Reticle even stroke at a fractional HUD size — unverified (Murad's HUDScale is an exact 2.0x).
- Optional: make the short-lived `MenuBackgroundHeight` key fall back to `MenuBackgroundShape`.
- Unverified in play: button placement feel, `InvertThrottle` sign, free-aim speed and `[player+0xD68]==3` detection, pause-menu save.
- Main menu showed ALL CAPS on first start and small caps after returning from a game (reported 2026-09-20) — cause not recorded as fixed.
- The Elite Operations Division building renders as a bright slab under dgVoodoo (seen 2026-09-15); a fix was asked for, outcome not recorded.
- Cut a new release (v1.0.2) once the above settle — ask first.

## 9. Working with Murad

- Short answers in plain words; answer first. "Concise summary" means a few lines.
- **Evidence over guessing**: debug and reverse engineer; don't hand him guessed settings to try one by one. Only ask him for the one step that needs a human, and say what evidence led there.
- Don't claim something is fixed until it was measured. If you said so wrongly, say so plainly.
- Commit in small, one-concern commits with one-line why-messages; never bundle unrelated changes (use explicit paths, not `git add -A`). No AI co-author lines. Push only when he says.
