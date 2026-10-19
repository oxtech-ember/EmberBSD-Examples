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

    Repeater {
        model: ["#ff0000", "#00ff00", "#0000ff", "#ffffff"]
        delegate: Rectangle {
            required property int index
            required property string modelData
            width: root.width / 2
            height: root.height / 2
            x: (index % 2) * width
            y: (index >> 1) * height
            color: modelData
        }
    }
}
