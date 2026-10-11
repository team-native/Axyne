#!/usr/bin/env python3
"""Validate the final native installer, including every staged payload entry."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import unquote
import xml.etree.ElementTree as ET


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def validate(package, staged):
    require(package.is_file(), f"Package does not exist: {package}")
    require((staged / "Axyne.app/Contents/MacOS/Axyne").is_file(),
            "Staged install has no Axyne executable")
    with tempfile.TemporaryDirectory(prefix="axyne-productbuild-") as work:
        expanded = Path(work) / "expanded"
        subprocess.run(["pkgutil", "--expand-full", str(package), str(expanded)],
                       check=True)
        distribution = ET.parse(expanded / "Distribution").getroot()
        lines = distribution.findall("./choices-outline//line")
        require(lines, "Distribution has an empty choices-outline")
        choices = {choice.get("id"): choice for choice in distribution.findall("choice")}
        references = {ref.get("id"): (ref.text or "").strip()
                      for ref in distribution.findall("pkg-ref")
                      if (ref.text or "").strip().startswith("#")}
        payloads = []
        for line in lines:
            choice = choices.get(line.get("choice"))
            require(choice is not None, "Distribution choice is missing")
            require(choice.get("start_selected", "true") == "true",
                    "Application payload choice is not selected")
            for ref in choice.findall("pkg-ref"):
                filename = unquote(references.get(ref.get("id"), "").removeprefix("#"))
                require(filename and Path(filename).name == filename,
                        "Distribution has no local component package reference")
                component = expanded / filename
                info = ET.parse(component / "PackageInfo").getroot()
                require(info.get("identifier") == ref.get("id"),
                        "Distribution and component identifiers differ")
                location = Path(info.get("install-location", "/"))
                require(location.is_absolute(), "Component install location is not absolute")
                payloads.append((component / "Payload", location))
        require(payloads, "Distribution has no referenced payload")
        for source in staged.rglob("*"):
            relative = source.relative_to(staged)
            destination = Path("/Applications") / relative
            candidates = [root / destination.relative_to(location)
                          for root, location in payloads
                          if destination.is_relative_to(location)]
            matches = [path for path in candidates if path.exists() or path.is_symlink()]
            require(len(matches) == 1, f"Missing or duplicate installer payload: {destination}")
            target = matches[0]
            if source.is_symlink():
                require(target.is_symlink() and target.readlink() == source.readlink(),
                        f"Changed payload symlink: {destination}")
            elif source.is_dir():
                require(target.is_dir(), f"Missing payload directory: {destination}")
            else:
                require(target.is_file() and not target.is_symlink()
                        and digest(target) == digest(source),
                        f"Changed or empty payload file: {destination}")
                require((target.stat().st_mode & 0o111) == (source.stat().st_mode & 0o111),
                        f"Changed executable permissions: {destination}")
    print(f"Validated productbuild Distribution and complete /Applications payload: {package}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("staged_install", type=Path)
    args = parser.parse_args()
    try:
        validate(args.package.resolve(), args.staged_install.resolve())
    except (ValueError, OSError, ET.ParseError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Invalid productbuild package: {error}\n")
