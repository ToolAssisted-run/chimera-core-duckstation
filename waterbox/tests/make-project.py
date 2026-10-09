#!/usr/bin/env python3
"""Writes a .chimeraProject for chimera-run and the frontend to open headless.

A project is what Chimera actually hands the core: the files in their slots
(order = swap order), a value for every declared setting, and an input log in
the machine's own button names. A disc arrives through the "slots" file under
its own name, with the files a cue sheet refers to mounted beside it - which
the bare-image path never exercises.

With no slot argument the console has an empty tray, and boots to its own
menu.

usage: make-project.py <package> <out.chimeraProject> <frames> [--set=<setting>=<value> ...]
                       [--idle-row=<row>] [--press=<frame>:<count>:<button> ...]
                       [<slot=file> ...]

A file a cue sheet names goes in as support=<file>. --press holds a button,
by the name the package declares ("P1 Cross"), for <count> frames from
<frame>: the row of frame N is what the pad says while frame N runs, as
run-native's --press has it.

A movie's row has a column for every control the MACHINE has, which is not
every control the package declares: a port with nothing in it has none, a
digital pad has no sticks, and the disc selector exists only when there is
more than one disc. Only the core knows, so the row is not guessed here:
this script restates the core's rule (which wires are live for these settings
and files) and makes both the LogKey and the rows from it. --idle-row is a row
the engine itself recorded for the same settings and files (chimera-run
--project <a project of 0 frames> --frames 1 --record <file>); given one, the
script stops if the row it worked out is not that row.
"""
import hashlib
import json
import os
import sys
import zipfile


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    package, out, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
    z = zipfile.ZipFile(package)
    cfg = json.loads(z.read("waterbox.config"))
    inputs = cfg["input"]
    buttons = inputs["buttons"]
    axes = inputs.get("axes", [])

    files = []
    chosen = {}
    idle_row = None
    presses = []
    for arg in sys.argv[4:]:
        if arg.startswith("--idle-row="):
            idle_row = arg[len("--idle-row="):]
            continue
        if arg.startswith("--press="):
            first, count, name = arg[len("--press="):].split(":", 2)
            if name not in buttons:
                sys.exit("make-project.py: the package declares no button named " + name)
            presses.append((int(first), int(count), name))
            continue
        if arg.startswith("--set="):
            name, value = arg[len("--set="):].split("=", 1)
            chosen[name] = value
            continue
        slot, path = arg.split("=", 1)
        files.append({"name": os.path.basename(path), "sha1": sha1(path), "slot": slot})

    settings = {d["name"]: d.get("default") for d in cfg.get("settings", [])}
    settings = {k: v for k, v in settings.items() if v is not None}
    for name, value in chosen.items():
        if name not in settings:
            sys.exit("make-project.py: the package declares no setting named " + name)
        # a setting keeps the type its declaration gives it
        if isinstance(settings[name], bool):
            value = value in ("true", "True", "1")
        settings[name] = value

    # Which of the declared controls THIS machine has. The frontend reads a
    # movie's rows by its LogKey, and the engine writes a row with a column
    # for every control the core says is live (IsButtonActive, IsAxisActive)
    # and no other - so key and row are both made from the same list, and the
    # list is the core's rule (waterbox/cinterface.cpp), restated: Reset
    # always, the lid with a disc, the selector with several; nothing for an
    # empty port, no sticks or stick buttons for a digital pad.
    discs = sum(1 for f in files if f["slot"] == "disc")

    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    def live(name):
        port = player_of(name)
        if port == 0:
            if name == "Reset":
                return True
            if name == "Disc Swap":
                return discs > 0
            return discs > 1
        kind = settings.get("port%d" % port, "none")
        if kind == "none":
            return False
        if kind == "digital":
            return "Stick" not in name and name.split(" ", 1)[1] not in ("L3", "R3", "Analog")
        return True

    # what a held button writes in its column: the package's own character
    mnemonics = inputs.get("mnemonics", {})

    def mnemonic(name):
        return mnemonics.get(name, mnemonics.get(name.partition(" ")[2], "!"))

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1

    def row_of(held):
        row, key = "", ""
        for g in range(groups):
            cells, names = "", ""
            for a in axes:
                if player_of(a["name"]) == g and live(a["name"]):
                    cells += "%5d," % a.get("neutral", 0)
                    names += a["name"] + "|"
            for b in buttons:
                if player_of(b) == g and live(b):
                    cells += mnemonic(b) if b in held else "."
                    names += b + "|"
            if g > 0 and not names:
                continue  # a port with nothing in it has no group
            row += "|" + cells
            key += "#" + names
        return row + "|", key

    row, key = row_of(())
    for first, count, name in presses:
        if not live(name):
            sys.exit("make-project.py: this machine has no %s to press" % name)
    # The engine's own word for the row, when the caller has it (a project of
    # no frames asked to record one): it must be the row worked out above, or
    # the rule restated here has drifted from the core's - upstream's game
    # database can swap a DualShock for a digital pad, for one.
    if idle_row is not None and idle_row != row:
        sys.exit("make-project.py: the engine records %r for an idle frame and this script worked out %r" % (idle_row, row))
    rows = [row] * frames
    for f in sorted(set(f for first, count, _ in presses for f in range(first, first + count) if f < frames)):
        rows[f] = row_of(set(name for first, count, name in presses if first <= f < first + count))[0]
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join(rows) + ("\n" if frames else "") + "[/Input]\n"

    # the firmware this machine needs, pinned as the wizard would pin it: every
    # declaration whose condition the settings meet (the ROMs themselves come
    # from --firmware on the command line, by id)
    def needed(decl):
        when = decl.get("requiredWhen")
        if when is None:
            return True
        value = settings.get(when["setting"])
        return value == when["is"] if "is" in when else value in when.get("in", [])
    firmware = [{"id": d["id"], "sha1": d["sha1"]} for d in cfg.get("firmware", []) if needed(d)]

    project = {
        "id": "duckstation-gate-01",
        "title": "PlayStation through Chimera",
        "description": "written by waterbox/tests/run-frontend.sh",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(package)},
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": firmware,
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": cfg["systemId"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": str(frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())
