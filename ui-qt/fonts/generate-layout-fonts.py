"""Generate empty presentation fonts; requires fonttools. MIT, Kern contributors.

Source characters remain in QTextDocument. Hidden glyphs have zero advance;
spacer glyphs reserve room for painted equation and image previews.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

for name, advance in [("Kern Hidden", 0), ("Kern Spacer", 1000)]:
    font = FontBuilder(1000, isTTF=True)
    font.setupGlyphOrder([".notdef", "blank"])
    font.setupCharacterMap({i: "blank" for i in range(32, 127)})
    font.setupGlyf({key: TTGlyphPen(None).glyph() for key in [".notdef", "blank"]})
    font.setupHorizontalMetrics({key: (advance, 0) for key in [".notdef", "blank"]})
    font.setupHorizontalHeader(ascent=800, descent=-200)
    font.setupNameTable({"familyName": name, "styleName": "Regular", "uniqueFontIdentifier": name,
                         "fullName": name, "psName": name.replace(" ", ""), "version": "Version 1.0"})
    font.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    font.setupPost()
    font.setupMaxp()
    font.font["head"].created = font.font["head"].modified = 2082844800
    font.save(Path(__file__).parent / (name.lower().replace(" ", "-") + ".ttf"))
