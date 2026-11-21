# Dial hand artwork

Copyright (C) The Qt Company Ltd. Licensed under BSD-3-Clause. The five PNGs
derive from `needle.png` and `needle_shadow.png` in Qt Declarative's
`examples/quick/customitems/dialcontrol`. Its root `REUSE.toml` assigns the
example images `LicenseRef-Qt-Commercial OR BSD-3-Clause`. See the
[original inputs](../upstream/qtdeclarative-v6.12.0/dialcontrol),
[checksums](../upstream/qtdeclarative-v6.12.0/UPSTREAM.json) and
[license](../upstream/qtdeclarative-v6.12.0/BSD-3-Clause.txt).

The host test renders the two original PNG layers at each selected angle using
the pivots in `Dial.qml` and verifies the decoded pixels against these crops.
To regenerate using the built [Qt reference tool](../qt-reference/dial.cpp),
run from the integration test directory with the same Qt environment as
`run.sh`:

```sh
"$OUT/qt-reference-build/qt_dial" hands upstream/qtdeclarative-v6.12.0/dialcontrol dial-art
```

Each output line gives the percentage followed by the cropped x, y, width
and height in the original 210 × 210 dial.
