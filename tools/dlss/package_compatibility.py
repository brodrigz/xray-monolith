"""Build the three-page shader FOMOD. Does not build or deploy the engine."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "extras/dlss_fomod"
LEGACY = REPO / "extras/dlss_gamma/gamedata"
VERSION = "0.6"
ARCHIVE_NAME = f"dlss-binaries-GAMMA-compatibility-patch-{VERSION}.zip"
ATMOS_FILES = ("deffer_impl_flat.ps", "ssfx_ao.ps", "ssfx_il.ps", "ssfx_sss_ext.ps")


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def write_text(destination: Path, content: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(content, encoding="utf-8", newline="\n")


def build(output: Path) -> None:
    # A fresh staging directory prevents stale optional files entering an archive.
    output.mkdir(parents=True, exist_ok=False)
    stage = output / "package"
    stage.mkdir()

    for source in sorted(LEGACY.rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(LEGACY)
        if relative == Path("shaders/r3/wd_body.vs"):
            component = "wearable"
        elif relative.parent == Path("shaders/r3") and relative.name in ATMOS_FILES:
            component = "atmos_rc7"
        else:
            component = "base"
        copy_file(source, stage / "components" / component / "gamedata" / relative)

    # These four files have the SSS algorithms/materials, with temporal changes.
    for name in ATMOS_FILES:
        copy_file(
            SOURCE / "sss235/gamedata/shaders/r3" / name,
            stage / "components/base/gamedata/shaders/r3" / name,
        )

    for component in ("better_beefs", "better_beefs_atmos_rc7"):
        component_source = SOURCE / component / "gamedata"
        for source in sorted(component_source.rglob("*")):
            if source.is_file():
                copy_file(source, stage / "components" / component / "gamedata" / source.relative_to(component_source))

    for name in ("ModuleConfig.xml", "info.xml"):
        copy_file(SOURCE / "fomod" / name, stage / "fomod" / name)
    for name in ("README.txt", "CREDITS.txt"):
        copy_file(SOURCE / name, stage / "docs" / name)
    for name in ("SOURCE_NOTES.txt", "SOURCE_MANIFEST.json"):
        copy_file(SOURCE / name, output / name)
    copy_file(SOURCE / "README.txt", stage / "README.txt")

    selections = {
        "atmos_none.txt": "Selected: no Atmospherics.",
        "atmos_26893.txt": "Selected: Atmospherics 2.68.93 Hotfix 3 (stock GAMMA 0.9.5).",
        "atmos_rc7.txt": "Selected: Atmospherics 2.69 RC7.x (RC7.5 / RC7.6).",
        "wearable.txt": "Selected: WearableDevices compatibility.",
        "better_beefs.txt": "Selected: Better Beef's NVGs (.19) compatibility.",
    }
    for name, description in selections.items():
        write_text(stage / "selections" / name, f"{description}\nPatch: {VERSION}\n")

    manifest = {
        "package": ARCHIVE_NAME,
        "binary_release": VERSION,
        "status": "release package; no MO2, shader compilation or in-game validation performed for the new variants",
        "files": [
            {
                "path": path.relative_to(stage).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for path in sorted(stage.rglob("*")) if path.is_file()
        ],
    }
    write_text(output / "MANIFEST.json", json.dumps(manifest, indent=2) + "\n")

    archive = output / ARCHIVE_NAME
    with ZipFile(archive, "w", compression=ZIP_DEFLATED, compresslevel=9) as package:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                entry = ZipInfo(path.relative_to(stage).as_posix(), (2026, 10, 13, 0, 0, 0))
                entry.compress_type = ZIP_DEFLATED
                entry.external_attr = 0o644 << 16
                package.writestr(entry, path.read_bytes(), compresslevel=9)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    write_text(output / f"SHA256SUMS-{VERSION}.txt", f"{digest}  {ARCHIVE_NAME}\n")
    print(f"Created {archive} ({archive.stat().st_size:,} bytes)")
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path,
        default=REPO / "_build/releases" / VERSION,
        help="New output directory; existing directories are never overwritten.",
    )
    build(parser.parse_args().output.resolve())
