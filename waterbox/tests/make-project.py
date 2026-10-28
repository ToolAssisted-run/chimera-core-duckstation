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
                       [--idle-row=<row>] [--arcadedb=<arcadedb.yaml>] [--firmware=<id>=<file>]
                       [--press=<frame>:<count>:<button> ...] [<slot=file> ...]

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


def inputs_of(log):
    """A movie's text block ("[Input]", "LogKey:...", its entries, "[/Input]")
    as a project file's "inputs": the key's groups of column names, and one
    entry a frame. A group starts where a '#' begins a name; a name ends at
    '|' (as Chimera splits a key)."""
    lines = log.splitlines()
    out = {}
    key = next((l[len("LogKey:"):] for l in lines if l.startswith("LogKey:")), None)
    if key is not None:
        groups, at = [], 0
        while at < len(key):
            assert key[at] == "#", key
            groups.append([])
            at += 1
            while at < len(key) and key[at] != "#":
                bar = key.index("|", at)
                groups[-1].append(key[at:bar])
                at = bar + 1
        out["columns"] = groups
    out["entries"] = [l for l in lines if l.startswith("|")]
    return out


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

    files = []
    chosen = {}
    idle_row = None
    live_names = None
    arcadedb = None
    firmware_files = {}
    presses = []
    for arg in sys.argv[4:]:
        if arg.startswith("--idle-row="):
            idle_row = arg[len("--idle-row="):]
            continue
        if arg.startswith("--firmware="):
            fid, fpath = arg[len("--firmware="):].split("=", 1)
            firmware_files[fid] = fpath
            continue
        if arg.startswith("--arcadedb="):
            arcadedb = arg[len("--arcadedb="):]
            continue
        if arg.startswith("--press="):
            first, count, name = arg[len("--press="):].split(":", 2)
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

    # The machine the project is, and the wire that machine has: a package of
    # several machines declares one controller for each (waterbox.config,
    # "machines"), and the setting named by machineSetting picks.
    which = settings.get(cfg.get("machineSetting", ""), None)
    machine = next((m for m in cfg.get("machines", []) if which in m.get("when", [])), None)
    if cfg.get("machines") and machine is None:
        sys.exit("make-project.py: no machine of this package answers to %r" % which)
    inputs = machine["input"] if machine else cfg["input"]
    system_id = machine["id"] if machine else cfg["systemId"]
    buttons = inputs["buttons"]
    axes = inputs.get("axes", [])
    for first, count, name in presses:
        if name not in buttons:
            sys.exit("make-project.py: this machine declares no button named " + name)
    arcade = system_id != "PS1"

    # An arcade panel's live wires are the cabinet's, and the core reads them
    # from ArcadeDuck's record of the game (waterbox/machine-arcade.inc,
    # DescribePanel). That rule, restated from the same record: Service and
    # Test always; per port the controls its layout names, a Start button and
    # a coin slot; a gun's aim where the port is a gun.
    if arcade:
        if arcadedb is None:
            sys.exit("make-project.py: an arcade project needs --arcadedb=<arcadedb.yaml>")
        import yaml
        db = yaml.safe_load(open(arcadedb))
        sets = [f["name"] for f in files if f["slot"] == "romset" and f["name"].lower().endswith(".zip")]
        if not sets:
            sys.exit("make-project.py: an arcade project needs romset=<set>.zip")
        game = db["games"].get(os.path.splitext(sets[0])[0])
        if game is None:
            sys.exit("make-project.py: %s is not in the arcade database" % sets[0])
        spelled = {"Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right", "Start": "Start", "Coin": "Coin",
                   "Trigger": "Trigger", "Reload": "Reload", "GunX": "Gun X", "GunY": "Gun Y"}
        spelled.update({"Button%d" % n: "Button %d" % n for n in range(1, 7)})
        live_names = {"Service", "Test"}
        for i, port in enumerate(game.get("controls", {}).get("ports", [])):
            if port.get("type", "none") == "none":
                continue
            layout = db["controlLayouts"].get(port.get("layout", ""))
            keys = [b["key"] for b in layout["bindings"]] if layout else list(spelled)
            for key in keys + ["Start", "Coin"]:
                if key in spelled:
                    live_names.add("P%d %s" % (i + 1, spelled[key]))
            if port["type"] == "lightgun":
                live_names |= {"P%d Gun X" % (i + 1), "P%d Gun Y" % (i + 1)}

    # Which of the declared controls THIS machine has. The frontend reads a
    # movie's rows by its LogKey, and the engine writes a row with a column
    # for every control the core says is live (IsButtonActive, IsAxisActive)
    # and no other - so key and row are both made from the same list, and the
    # list is the core's rule (waterbox/cinterface.cpp), restated: Reset
    # always, the lid with a disc, the selector with several; nothing for an
    # empty socket, and for each controller the controls that controller has.
    discs = sum(1 for f in files if f["slot"] == "disc")

    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    def live(name):
        if arcade:
            return name in live_names
        port = player_of(name)
        if port == 0:
            if name == "Reset":
                return True
            if name == "Disc Swap":
                return discs > 0
            return discs > 1
        # the socket: P1 and P2 are the ports, P3..P5 a multitap's 1B 1C 1D,
        # P6..P8 the other's 2B 2C 2D - and those are there only with the tap
        key = ("port1", "port2", "port1b", "port1c", "port1d", "port2b", "port2c", "port2d")[port - 1]
        tap = settings.get("multitap", "none")
        if 3 <= port <= 5 and tap not in ("port1", "both"):
            return False
        if port >= 6 and tap not in ("port2", "both"):
            return False
        kind = settings.get(key, "none")
        control = name.split(" ", 1)[1]
        pad = ("Up", "Down", "Left", "Right", "Start", "Select", "Square", "Cross", "Circle", "Triangle",
               "L1", "R1", "L2", "R2")
        has = {
            "digital": pad,
            "dualshock": pad + ("L3", "R3", "Analog", "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y"),
            "negcon": ("Up", "Down", "Left", "Right", "Start", "A", "B", "R", "Twist", "I", "II", "L"),
            "guncon": ("Trigger", "A", "B", "Shoot Offscreen", "Gun X", "Gun Y"),
            "justifier": ("Trigger", "Start", "Back", "Shoot Offscreen", "Gun X", "Gun Y"),
            "mouse": ("Mouse Left", "Mouse Right", "Mouse X", "Mouse Y"),
        }
        has["analogJoystick"] = has["dualshock"]
        has["negconRumble"] = has["negcon"]
        return control in has.get(kind, ())

    # what a held button writes in its column: the package's own character
    mnemonics = inputs.get("mnemonics", {})

    def mnemonic(name):
        return mnemonics.get(name, mnemonics.get(name.partition(" ")[2], "!"))

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1

    def row_of(held):
        parts = []
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
            parts.append((cells, names))
        # A socket with nothing in it has no group - once nothing live comes
        # after it. One with a live socket AFTER it keeps its place, empty: a
        # multitap with a pad in 1B and nothing in port 2 is "...|P1||P3|".
        while len(parts) > 1 and not parts[-1][1]:
            parts.pop()
        row = "".join("|" + cells for cells, _ in parts) + "|"
        key = "".join("#" + names for _, names in parts)
        return row, key

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
    def holds(when):
        if when is None:
            return True
        if "all" in when:
            return all(holds(w) for w in when["all"])
        if "any" in when:
            return any(holds(w) for w in when["any"])
        value = settings.get(when["setting"])
        return value == when["is"] if "is" in when else value in when.get("in", [])

    # A MAME archive is firmware with no hash declared - archives of one rom
    # differ - so a project pins the hash of the archive it was made with, as
    # the wizard does: --firmware=<id>=<file> says which that is.
    firmware = []
    for d in cfg.get("firmware", []):
        if not holds(d.get("requiredWhen")):
            continue
        pinned = d.get("sha1")
        if pinned is None:
            if d["id"] not in firmware_files:
                sys.exit("make-project.py: %s has no declared hash; say which file with --firmware=%s=<file>" % (d["id"], d["id"]))
            pinned = sha1(firmware_files[d["id"]])
        firmware.append({"id": d["id"], "sha1": pinned})

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
        "inputs": inputs_of(log),
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": system_id,
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
