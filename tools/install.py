"""Install or remove the DualSense button prompts (icon font + controller wording in tutorial text).

The game's own files are backed up once to <game>\\scripts\\TrueCrimeDualSense-originals\\ and patched in place;
nothing from the game is distributed with this project.

usage:
  python tools\\install.py "C:\\Games\\True Crime Streets of LA"            patch font + text
  python tools\\install.py "C:\\Games\\True Crime Streets of LA" --restore  put the original files back
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT_FILES = ["TCPCUS.txt", "TCPCUK.txt", "TCPCDE.txt", "TCPCFR.txt", "TCPCIT.txt", "TCPCSP.txt"]

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    game = sys.argv[1]
    restore = "--restore" in sys.argv[2:]
    if not os.path.isfile(os.path.join(game, "TrueCrime.exe")):
        print(f"TrueCrime.exe not found in {game}"); return 1

    backup = os.path.join(game, "scripts", "TrueCrimeDualSense-originals")
    font = os.path.join(game, "Data", "Textures", "Font_UI_Small.fnt")
    font_bak = os.path.join(backup, "Font_UI_Small.fnt")
    shell = os.path.join(game, "Data", "Shell")
    shell_bak = os.path.join(backup, "Shell")

    if restore:
        if not os.path.isdir(backup):
            print("no backup found - nothing to restore"); return 1
        shutil.copy2(font_bak, font)
        for name in TEXT_FILES:
            if os.path.exists(os.path.join(shell_bak, name)):
                shutil.copy2(os.path.join(shell_bak, name), os.path.join(shell, name))
        print("original font and text restored"); return 0

    os.makedirs(shell_bak, exist_ok=True)
    if not os.path.exists(font_bak):
        shutil.copy2(font, font_bak)
    for name in TEXT_FILES:
        src = os.path.join(shell, name)
        if os.path.exists(src) and not os.path.exists(os.path.join(shell_bak, name)):
            shutil.copy2(src, os.path.join(shell_bak, name))

    # The button symbols have to be drawn against the same UIPixelAspect the mod will narrow them by, or they come
    # out as ellipses; read it from the installed ini so the two cannot drift apart.
    aspect = 100
    ini = os.path.join(game, "scripts", "TrueCrimeDualSense.ini")
    if os.path.isfile(ini):
        with open(ini, "r", errors="ignore") as f:
            for line in f:
                if line.strip().lower().startswith("uipixelaspect"):
                    try: aspect = int(line.split("=", 1)[1].split(";")[0].strip())
                    except ValueError: pass
    subprocess.check_call([sys.executable, os.path.join(HERE, "patch_font.py"), font_bak, font,
                           f"--aspect={aspect}"])
    print(f"button symbols drawn for UIPixelAspect={aspect}")
    subprocess.check_call([sys.executable, os.path.join(HERE, "patch_text.py"), shell_bak, shell])
    print("button prompts installed (originals in scripts\\TrueCrimeDualSense-originals)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
