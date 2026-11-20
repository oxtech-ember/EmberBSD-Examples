// SPDX-License-Identifier: BSD-2-Clause
import QtQuick
import QtTest
import ".."

TestCase {
    name: "WeatherFrame"
    when: windowShown
    visible: true
    width: 480
    height: 480

    Weather { id: scene }

    function test_capture() {
        const frame = grabImage(scene)
        compare(frame.width, scene.width)
        compare(frame.height, scene.height)
        compare(frame.pixel(0, 0), scene.color)
        let ink = false
        for (let y = 200; y < 280 && !ink; ++y)
            for (let x = 140; x < 340 && !ink; ++x)
                ink = Qt.colorEqual(frame.pixel(x, y), "#ffffff")
        verify(ink, "The temperature must be visible")
        frame.save("weather.bmp")
    }
}
