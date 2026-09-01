#!/usr/bin/env python3
"""Turn an lupdate-generated Qt TS catalog into deterministic en_XA text.

This maintainer tool never translates user content. It accents and expands source UI
text so automated and manual layout checks expose hard-coded English and clipping.
Placeholders, markup, command-line switches, filename patterns, and key sequences stay
byte-for-byte intact.
"""

from __future__ import annotations

import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


ACCENTS = str.maketrans(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "ÅƁÇÐËƑƓĦÏĴҠĿḾŃÖƤǪŔŠŦÜṼŴẊŸŽ"
    "åƀçðëƒɠħïĵҡŀḿńöƥǫŕšŧüṽŵẋÿž",
)
PROTECTED = re.compile(
    r"(%L?\d+|%n|%%|<[^>]+>|&[A-Za-z0-9#]+;|"
    r"--?[A-Za-z][A-Za-z0-9-]*|\*\.[A-Za-z0-9]+|"
    r"\b[A-Za-z0-9_.-]+\.(?:bin|bps|chd|cht|cue|gen|gg|ips|md|m3u|qm|"
    r"sg|slang|slangp|smd|sms|ts|ups|zip)\b)"
)
KEY_SEQUENCE = re.compile(
    r"^(?:(?:Ctrl|Alt|Shift|Meta)\+)*(?:[A-Z0-9]|F(?:[1-9]|1[0-2])|"
    r"Backspace|Escape|Insert|Space|Tab)$"
)


def pseudo_fragment(text: str) -> str:
    accented = text.translate(ACCENTS)
    letters = sum(character.isalpha() for character in text)
    return accented + ("~" * ((letters + 3) // 4))


def pseudolocalize(source: str) -> str:
    if not source or KEY_SEQUENCE.fullmatch(source):
        return source
    pieces: list[str] = []
    cursor = 0
    for match in PROTECTED.finditer(source):
        pieces.append(pseudo_fragment(source[cursor : match.start()]))
        pieces.append(match.group(0))
        cursor = match.end()
    pieces.append(pseudo_fragment(source[cursor:]))
    return "⟦" + "".join(pieces) + "⟧"


def generate(path: Path) -> None:
    tree = ET.parse(path)
    root = tree.getroot()
    root.set("language", "en_XA")
    root.set("sourcelanguage", "en")
    for message in root.findall("./context/message"):
        source = message.find("source")
        translation = message.find("translation")
        if source is None or source.text is None:
            continue
        if translation is None:
            translation = ET.SubElement(message, "translation")
        translation.attrib.pop("type", None)
        translation.text = pseudolocalize(source.text)
    ET.indent(tree, space="    ")
    payload = ET.tostring(root, encoding="unicode", short_empty_elements=True)
    path.write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE TS>\n' + payload + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    arguments = parser.parse_args()
    generate(arguments.catalog)


if __name__ == "__main__":
    main()
