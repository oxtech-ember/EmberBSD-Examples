// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
import QtQuick

Item {
    id: root
    width: 480
    height: 480

    property int touchCount: 0
    property int lastX: -1
    property int lastY: -1
    property string touchState: "waiting"

    Rectangle {
        x: 0
        width: 120
        height: root.height
        color: "#d32f2f"
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
            text: "RED"
            color: "white"
            font.pixelSize: 22
        }
    }
    Rectangle {
        x: 120
        width: 120
        height: root.height
        color: "#388e3c"
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
            text: "GREEN"
            color: "white"
            font.pixelSize: 22
        }
    }
    Rectangle {
        x: 240
        width: 120
        height: root.height
        color: "#1976d2"
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
            text: "BLUE"
            color: "white"
            font.pixelSize: 22
        }
    }
    Rectangle {
        x: 360
        width: 120
        height: root.height
        color: "white"
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 24
            text: "WHITE"
            color: "black"
            font.pixelSize: 22
        }
    }

    Rectangle {
        x: 8
        y: root.height - 82
        width: root.width - 16
        height: 74
        color: "black"
        opacity: 0.82

        Text {
            anchors.fill: parent
            anchors.margins: 8
            color: "white"
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: "Touch " + root.touchState + "  count=" + root.touchCount
                  + "\nx=" + root.lastX + "  y=" + root.lastY
        }
    }

    MouseArea {
        id: touchArea
        anchors.fill: parent

        onPressed: {
            root.touchCount += 1
            root.lastX = Math.round(touchArea.mouseX)
            root.lastY = Math.round(touchArea.mouseY)
            root.touchState = "pressed"
        }
        onPositionChanged: {
            root.lastX = Math.round(touchArea.mouseX)
            root.lastY = Math.round(touchArea.mouseY)
            root.touchState = "moving"
        }
        onReleased: root.touchState = "released"
        onCanceled: root.touchState = "cancelled"
    }
}
