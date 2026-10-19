// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
// Origin: EmberBSD, AI-assisted. A fixed compositor pixel reference.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    width: 640
    height: 480
    Accessible.ignored: true
    property int solidIndex: -1
    readonly property list<color> referenceColors: ["#ff0000", "#00ff00", "#0000ff", "#ffffff"]

    Repeater {
        model: root.referenceColors
        delegate: Rectangle {
            required property int index
            required property color modelData
            width: root.width / 2
            height: root.height / 2
            x: (index % 2) * width
            y: (index >> 1) * height
            color: root.solidIndex >= 0 ? root.referenceColors[root.solidIndex] : modelData
        }
    }
}
