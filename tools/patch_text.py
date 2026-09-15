"""Replace PC mouse/keyboard wording in the tutorial text with controller wording (original Xbox layout).

TCPC??.txt: one string per line (LF), Latin-1, 3912 lines; 0x1B separates sub-lines inside a string.
Only whole exact substrings are replaced; the script fails if any expected text is missing,
and verifies that line count and every untouched line are identical.

usage: patch_text.py <original_dir> <output_dir>
"""
import os, sys

src_dir, out_dir = sys.argv[1], sys.argv[2]

# (line number, old bytes, new bytes) per file; Xbox manual: steer with left thumbstick,
# precision targeting reticule moves with the left analog stick.
EDITS = {
    "TCPCUS.txt": [
        (504, b"Steer with the mouse or keyboard.", b"Steer with the left stick."),
        (514, b"Use the mouse to move the reticule", b"Use the left stick to move the reticule"),
        (1977, b"USE YOUR MOUSE TO \x1b", b"USE THE LEFT STICK TO \x1b"),
    ],
    "TCPCUK.txt": [
        (504, b"Steer with the mouse or keyboard.", b"Steer with the left stick."),
        (514, b"Use the mouse to move the reticule", b"Use the left stick to move the reticule"),
        (1977, b"USE YOUR MOUSE TO \x1b", b"USE THE LEFT STICK TO \x1b"),
    ],
    "TCPCDE.txt": [
        (504, b"Steuere mit Maus oder Tastatur.", b"Steuere mit dem linken Stick."),
        (514, b"Mit der Maus bewegst du das Fadenkreuz.", b"Mit dem linken Stick bewegst du das Fadenkreuz."),
        (1977, b"DIE GEW\xdcNSCHTE STELLE MIT \x1b%a", b"DIE GEW\xdcNSCHTE STELLE MIT \x1bDEM LINKEN STICK"),
    ],
    "TCPCFR.txt": [
        (504, b"\xe0 l'aide de la souris ou du clavier.", b"\xe0 l'aide du stick gauche."),
        (514, b"Utilisez la souris pour d\xe9placer le r\xe9ticule", b"Utilisez le stick gauche pour d\xe9placer le r\xe9ticule"),
        # line 1977 already says "JOYSTICK ANALOGIQUE GAUCHE" (console text)
    ],
    "TCPCIT.txt": [
        (504, b"Sterza con mouse o tastiera.", b"Sterza con la levetta sinistra."),
        (514, b"Usa il mouse per muovere il mirino", b"Usa la levetta sinistra per muovere il mirino"),
        (1977, b"USA IL MOUSE PER\x1b", b"USA LA LEVETTA SINISTRA PER\x1b"),
    ],
    "TCPCSP.txt": [
        (504, b"Conduce con el rat\xf3n o el teclado.", b"Conduce con el stick izquierdo."),
        (514, b"Usa el rat\xf3n para mover la ret\xedcula", b"Usa el stick izquierdo para mover la ret\xedcula"),
        # line 1977 already says "STICK ANAL\xd3GICO" (console text)
    ],
}

os.makedirs(out_dir, exist_ok=True)
for name, edits in EDITS.items():
    if not os.path.exists(os.path.join(src_dir, name)):
        print(f"{name}: not installed, skipped")
        continue
    data = open(os.path.join(src_dir, name), "rb").read()
    lines = data.split(b"\n")
    new = list(lines)
    for ln, old, rep in edits:
        cur = new[ln - 1]
        if cur.count(old) != 1:
            raise SystemExit(f"{name}:{ln}: expected text not found exactly once: {old!r}")
        new[ln - 1] = cur.replace(old, rep)
    out = b"\n".join(new)
    changed = [i + 1 for i, (a, b) in enumerate(zip(lines, new)) if a != b]
    assert len(new) == len(lines) and changed == sorted(e[0] for e in edits), (name, changed)
    open(os.path.join(out_dir, name), "wb").write(out)
    print(f"{name}: changed lines {changed}")
    for ln in changed:
        print(f"   {ln}: {new[ln - 1].decode('latin-1')[:150]}")
