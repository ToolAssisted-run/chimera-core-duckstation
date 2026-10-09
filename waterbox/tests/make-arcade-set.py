#!/usr/bin/env python3
"""Writes a MAME rom set the way ArcadeDuck's database names it, out of dumps
that hold the same bytes under other names.

MAME renames roms between releases, and a set from an older release has the
right bytes under names the database does not know. This looks every rom of
<set id> up by CRC-32 in the archives given and writes <out.zip> with each
under its current name. It invents nothing: a rom that is in none of the
archives is reported and the set is not written.

usage: make-arcade-set.py <arcadedb.yaml> <set id> <out.zip> <archive.zip>...
"""
import sys
import zipfile

import yaml


def main():
    db_path, set_id, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    db = yaml.safe_load(open(db_path))
    game = db["games"].get(set_id)
    if game is None:
        sys.exit("make-arcade-set.py: no set called %s in the database" % set_id)

    have = {}
    for path in sys.argv[4:]:
        z = zipfile.ZipFile(path)
        for info in z.infolist():
            have.setdefault(info.CRC, (z, info))

    members, missing = [], []
    for rom in game.get("roms", []):
        if "crc32" not in rom:
            continue
        hit = have.get(int(str(rom["crc32"]), 16))
        if hit is None:
            if rom.get("required", True):
                missing.append("%s (crc %s)" % (rom["name"], rom["crc32"]))
            continue
        members.append((rom["name"], hit[0].read(hit[1])))

    if missing:
        sys.exit("make-arcade-set.py: %s is missing %s" % (set_id, ", ".join(missing)))

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as out:
        for name, data in sorted(set(members)):
            out.writestr(name, data)
    print("%s: %d roms" % (out_path, len(set(members))))


if __name__ == "__main__":
    main()
