#!/usr/bin/env python3
"""Writes a .chimeraProject for chimera-run and the frontend to open headless.

A project is what Chimera actually hands the core: the files in their slots
(order = swap order), a value for every declared setting, and an input log in
the machine's own button names. A disc arrives through the "slots" file under
its own name, with the files a cue sheet refers to mounted beside it - which
the bare-image path never exercises.

With no slot argument the console has an empty tray, and boots to its own
menu.

usage: make-project.py <package> <out.chimeraProject> <frames> [--set=<setting>=<value> ...] [<slot=file> ...]

A file a cue sheet names goes in as support=<file>.
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

    # the log mnemonic (see chimera-core-ares's make-project.py): grouped by
    # player, axes first as five-wide values with a comma, one char per button
    def player_of(name):
        if len(name) > 2 and name[0] in "Pp" and name[1].isdigit():
            return int(name[1])
        return 0

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1
    row, key = "", ""
    for g in range(groups):
        row += "|"
        key += "#"
        for a in axes:
            if player_of(a["name"]) == g:
                row += "%5d," % a.get("neutral", 0)
                key += a["name"] + "|"
        for b in buttons:
            if player_of(b) == g:
                row += "."
                key += b + "|"
    row += "|"
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join([row] * frames) + "\n[/Input]\n"

    files = []
    chosen = {}
    for arg in sys.argv[4:]:
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
        settings[name] = value

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
