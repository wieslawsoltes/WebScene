"""Regenerate container variants with fontTools 4.59.2 + brotli.

No glyph subsetting: independent static outline references remain applicable.
"""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.ttLib.woff2 import WOFF2FlavorData

root = Path(__file__).resolve().parent
for name, flavor, transforms in [
    ("Roboto-Variable.woff", "woff", None),
    ("Roboto-Variable-null.woff2", "woff2", set()),
    ("Roboto-Variable-hmtx.woff2", "woff2", {"glyf", "loca", "hmtx"}),
]:
    font = TTFont(root / "Roboto-Variable.ttf", recalcTimestamp=False)
    font.flavor = flavor
    if transforms is not None:
        font.flavorData = WOFF2FlavorData(transformedTables=transforms)
    font.save(root / name)
