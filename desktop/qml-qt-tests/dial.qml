// Copyright (C) 2017 The Qt Company Ltd.
// Copyright (c) 2026 EmberBSD contributors.
// SPDX-License-Identifier: BSD-3-Clause
// Touch adaptation of Qt's customitems/dialcontrol. Original artwork is in upstream/.
import QtQuick

Rectangle {
    width: 352; height: 376; color: "#1f2728"
    Text { x: 20; y: 17; text: "DIAL CONTROL"; color: "#f3efe3"; font.pixelSize: 18 }
    Text { id: readout; x: 255; y: 17; text: "0 %"; color: "#ffd16a"; font.pixelSize: 18 }
    Rectangle { x: 20; y: 46; width: 312; height: 1; color: "#59625e" }

    Item {
        x: 71; y: 57; width: 210; height: 210
        Image { source: "upstream/qtdeclarative-v6.12.0/dialcontrol/background.png" }
        Image { id: low; x: 51; y: 93; source: "dial-art/hand0.png" }
        Image { id: quarter; x: 43; y: 68; visible: false; source: "dial-art/hand25.png" }
        Image { id: half; x: 96; y: 33; visible: false; source: "dial-art/hand50.png" }
        Image { id: threeQuarter; x: 99; y: 67; visible: false; source: "dial-art/hand75.png" }
        Image { id: high; x: 97; y: 95; visible: false; source: "dial-art/hand100.png" }
        Image { x: 21; y: 18; source: "upstream/qtdeclarative-v6.12.0/dialcontrol/overlay.png" }
    }

    Text { x: 20; y: 282; text: "SET POINT"; color: "#bdc4ba"; font.pixelSize: 14 }
    Item {
        x: 21; y: 307; width: 310; height: 52
        Rectangle { width: 310; height: 52; color: "#30393a" }
        Rectangle { id: marker; x: 0; y: 48; width: 62; height: 4; color: "#ffbf44" }
        Text { x: 24; y: 15; text: "0"; color: "#f4f3ec"; font.pixelSize: 18 }
        Text { x: 81; y: 15; text: "25"; color: "#f4f3ec"; font.pixelSize: 18 }
        Text { x: 143; y: 15; text: "50"; color: "#f4f3ec"; font.pixelSize: 18 }
        Text { x: 205; y: 15; text: "75"; color: "#f4f3ec"; font.pixelSize: 18 }
        Text { x: 260; y: 15; text: "100"; color: "#f4f3ec"; font.pixelSize: 18 }
        MouseArea {
            anchors.fill: parent
            onClicked: (mouse) => {
                const x = mouse.x
                low.visible = x < 62
                quarter.visible = x >= 62 && x < 124
                half.visible = x >= 124 && x < 186
                threeQuarter.visible = x >= 186 && x < 248
                high.visible = x >= 248
                if (low.visible) { marker.x = 0; readout.text = "0 %" }
                else if (quarter.visible) { marker.x = 62; readout.text = "25 %" }
                else if (half.visible) { marker.x = 124; readout.text = "50 %" }
                else if (threeQuarter.visible) { marker.x = 186; readout.text = "75 %" }
                else { marker.x = 248; readout.text = "100 %" }
            }
        }
    }
}
