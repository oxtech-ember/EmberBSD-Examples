// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
// Original EmberBSD instrument layout and illustrations.
import QtQuick

Rectangle {
    id: lab
    signal refreshRequested()
    width: 480; height: 480; color: "#0b1c23"

    Item {
        x: 40; y: 40; width: 400; height: 400

        Item {
            x: 122; y: 14; width: 24; height: 22
            Rectangle { x: 1; y: 12; width: 4; height: 8; color: "#83dec8" }
            Rectangle { x: 8; y: 7; width: 4; height: 13; color: "#83dec8" }
            Rectangle { x: 15; y: 1; width: 4; height: 19; color: "#83dec8" }
        }
        Text { x: 150; y: 15; text: "NETWORK LAB"; font.pixelSize: 21; color: "#d9e8e5" }

        Rectangle { x: 40; y: 51; width: 320; height: 46; color: "#20333b" }
        Rectangle {
            id: clockTab; x: 41; y: 52; width: 158; height: 44; color: "#193c40"
            Rectangle { x: 13; y: 40; width: 132; height: 2; color: "#92e4c9" }
        }
        Rectangle {
            id: dnsTab; x: 201; y: 52; width: 158; height: 44; color: "#473836"; visible: false
            Rectangle { x: 13; y: 40; width: 132; height: 2; color: "#ffb982" }
        }
        Item {
            x: 40; y: 51; width: 160; height: 46
            Text { anchors.centerIn: parent; text: "CLOCK"; font.pixelSize: 18; color: "#e1eeea" }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    clockPage.visible = true
                    dnsPage.visible = false
                    clockTab.visible = true
                    dnsTab.visible = false
                    console.log("clock.selected")
                }
            }
        }
        Item {
            x: 200; y: 51; width: 160; height: 46
            Text { anchors.centerIn: parent; text: "DNS PULSE"; font.pixelSize: 18; color: "#e1eeea" }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    clockPage.visible = false
                    dnsPage.visible = true
                    clockTab.visible = false
                    dnsTab.visible = true
                    console.log("dns.selected")
                }
            }
        }

        Item {
            id: clockPage; width: 400; height: 400
            Text { x: 47; y: 125; text: "UTC TIME"; font.pixelSize: 15; color: "#94cfc0" }
            Text { id: utcTime; x: 43; y: 148; font.pixelSize: 44; color: "#f3f6df" }
            Text { id: utcDate; x: 47; y: 198; font.pixelSize: 17; color: "#b1c5c6" }
            Image { x: 258; y: 115; source: "art/utc-orbit.png" }

            Rectangle { x: 47; y: 228; width: 306; height: 1; color: "#384b52" }
            Text { x: 47; y: 245; text: "SECONDS"; font.pixelSize: 14; color: "#aec4c1" }
            Item {
                x: 49; y: 268; width: 301; height: 30
                Image { y: 4; source: "art/ruler.png" }
                Rectangle { y: 4; width: 298; height: 2; color: "#537772" }
                Rectangle {
                    id: secondMarker; x: 0; y: 0; width: 5; height: 25; color: "#99f3cb"
                    Behavior on x { NumberAnimation { duration: 180 } }
                }
            }
            Text { x: 47; y: 300; text: "00"; font.pixelSize: 12; color: "#aec4c1" }
            Text { x: 117; y: 300; text: "15"; font.pixelSize: 12; color: "#aec4c1" }
            Text { x: 192; y: 300; text: "30"; font.pixelSize: 12; color: "#aec4c1" }
            Text { x: 267; y: 300; text: "45"; font.pixelSize: 12; color: "#aec4c1" }
            Text { x: 334; y: 300; text: "59"; font.pixelSize: 12; color: "#aec4c1" }

            Rectangle { x: 47; y: 326; width: 306; height: 1; color: "#384b52" }
            Text { x: 47; y: 340; text: "TIME SOURCE"; font.pixelSize: 14; color: "#90a6a5" }
            Text { id: clockStatus; x: 47; y: 359; font.pixelSize: 17; color: "#cfe5dc" }
        }

        Item {
            id: dnsPage; width: 400; height: 400; visible: false
            Text { x: 47; y: 119; text: "HOST"; font.pixelSize: 14; color: "#e5ab82" }
            Text { id: dnsHost; x: 47; y: 136; font.pixelSize: 19; color: "#eef1dc" }
            Text { x: 47; y: 170; text: "IPV4"; font.pixelSize: 14; color: "#9bb3b1" }
            Text { id: dnsAddress; x: 46; y: 189; font.pixelSize: 23; color: "#e6eeeb" }
            Image { x: 274; y: 154; source: "art/dns-route.png" }

            Rectangle { x: 47; y: 224; width: 306; height: 1; color: "#384b52" }
            Text { x: 47; y: 241; text: "RESOLVE TIME"; font.pixelSize: 14; color: "#b5bcb0" }
            Text { id: dnsLatency; x: 223; y: 230; font.pixelSize: 25; color: "#ffd1a5" }
            Item {
                x: 49; y: 268; width: 301; height: 28
                Image { y: 4; source: "art/ruler.png" }
                Rectangle { y: 4; width: 298; height: 2; color: "#886252" }
                Rectangle {
                    id: pulseTrail; x: 0; y: 5; width: 5; height: 13; color: "#bf7559"
                    Behavior on x { NumberAnimation { duration: 800 } }
                }
                Rectangle {
                    id: pulseMarker; x: 0; y: 0; width: 5; height: 25; color: "#ffc082"
                    Behavior on x { NumberAnimation { duration: 450 } }
                }
            }
            Text { x: 47; y: 300; text: "0"; font.pixelSize: 12; color: "#b4b9af" }
            Text { x: 181; y: 300; text: "250"; font.pixelSize: 12; color: "#b4b9af" }
            Text { x: 303; y: 300; text: "500 MS"; font.pixelSize: 12; color: "#b4b9af" }

            Rectangle { x: 47; y: 326; width: 306; height: 1; color: "#384b52" }
            Text { id: dnsStatus; x: 47; y: 346; font.pixelSize: 14; color: "#cbdad3" }
            Rectangle {
                x: 244; y: 332; width: 109; height: 36; color: "#4b3932"
                Rectangle { y: 0; width: 109; height: 2; color: "#bf8968" }
                Text { anchors.centerIn: parent; text: "REFRESH"; font.pixelSize: 17; color: "#ffcfaa" }
                MouseArea { anchors.fill: parent; onClicked: lab.refreshRequested() }
            }
        }
    }
}
