// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
import QtQuick

Rectangle {
    width: 480
    height: 480
    color: "#122230"

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 88
        text: "MOSAICO"
        color: "#d6e3ea"
        font.pixelSize: 14
    }

    Text {
        anchors.centerIn: parent
        text: "21 C"
        color: "#ffffff"
        font.pixelSize: 14
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 334
        text: "SUNNY"
        color: "#ffcb69"
        font.pixelSize: 14
    }
}
