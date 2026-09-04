# Licensed variable-font regression fixture

`Roboto-Variable.ttf` is Roboto from Google Fonts, licensed under the accompanying
`OFL.txt`. Source: <https://github.com/google/fonts/blob/main/ofl/roboto/Roboto%5Bwdth%2Cwght%5D.ttf>.
Downloaded 2026-09-04; SHA-256:
`d7598e12c5dbef095ff8272cfc55da0250bd07fbdecbac8a530b9b277872a134`.

The static references are generated **independently of the production HarfBuzz
instancer**, using FontTools 4.59.2. They pin width to its default and weight to
400, 550 or 700, retaining the full glyph set. The WOFF2 fixture contains the same
variable font compressed by FontTools with Brotli. Reproduce with:

```sh
fonttools varLib.instancer Roboto-Variable.ttf wght=400 wdth=100 --output Roboto-400.ttf
fonttools varLib.instancer Roboto-Variable.ttf wght=550 wdth=100 --output Roboto-550.ttf
fonttools varLib.instancer Roboto-Variable.ttf wght=700 wdth=100 --output Roboto-700.ttf
fonttools ttLib.woff2 compress Roboto-Variable.ttf -o Roboto-Variable.woff2
```

These files are test assets, not application fonts or a runtime dependency on
Python/FontTools. Tests compare actual outline points, raster ink, glyph counts,
shaping tables and advances; a weight label or synthetic bold is insufficient.
