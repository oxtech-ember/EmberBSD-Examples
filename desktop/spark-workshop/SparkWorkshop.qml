// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
// Original EmberBSD Spark Workshop interface.
import QtQuick

Rectangle {
    id: app
    signal openWorkshopRequested()
    signal previousStepRequested()
    signal nextStepRequested()
    signal finishRequested()
    signal cameraRequested()
    signal cameraCaptureRequested()
    signal cameraUseRequested()
    signal cameraRetakeRequested()
    signal cameraCancelRequested()
    signal askPhotoRequested()
    signal answerBackRequested()
    signal answerPreviousRequested()
    signal answerNextRequested()
    signal answerRetryRequested()
    width: 480; height: 480; color: "#191a1d"

    Item {
        x: 40; y: 28; width: 400; height: 424

        Rectangle { x: 0; y: 0; width: 400; height: 4; color: "#ed8952" }
        Image { x: 0; y: 11; width: 128; height: 48; source: "spark-mark.png" }

        Item {
            id: homePage; x: 0; y: 80; width: 400; height: 372
            Rectangle { x: 0; y: 0; width: 400; height: 2; color: "#393536" }
            Text { id: guideTitle; x: 0; y: 18; font.pixelSize: 25; color: "#f1ece5" }
            Text { id: homeStepCount; x: 0; y: 64; font.pixelSize: 15; color: "#eda072" }
            Rectangle { x: 0; y: 96; width: 400; height: 2; color: "#393536" }
            Text { x: 0; y: 118; text: "Photograph a detail for this step."; font.pixelSize: 15; color: "#c5c0b9" }
            Rectangle {
                x: 0; y: 258; width: 400; height: 46; color: "#a74f2d"
                Text { anchors.centerIn: parent; text: "OPEN WORKSHOP"; font.pixelSize: 15; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.openWorkshopRequested() }
            }
        }

        Item {
            id: lessonPage; x: 0; y: 80; width: 400; height: 372; visible: false
            Text { id: lessonCount; x: 300; y: 0; font.pixelSize: 12; color: "#aaa49d" }
            Rectangle { x: 0; y: 25; width: 400; height: 2; color: "#393536" }
            Text { id: stepTitle; x: 0; y: 42; font.pixelSize: 22; color: "#f1ece5" }
            Text { id: stepBody1; x: 0; y: 82; font.pixelSize: 16; color: "#c5c0b9" }
            Text { id: stepBody2; x: 0; y: 105; font.pixelSize: 16; color: "#c5c0b9" }
            Text { id: stepBody3; x: 0; y: 128; font.pixelSize: 16; color: "#c5c0b9" }

            Rectangle { x: 0; y: 167; width: 400; height: 2; color: "#393536" }
            Rectangle { x: 0; y: 182; width: 160; height: 120; color: "#252427" }
            Image { id: photoThumbnail; x: 0; y: 182; width: 160; height: 120; source: "photo-slot.png"; visible: false }
            Text { id: photoHint; x: 174; y: 194; font.pixelSize: 14; color: "#aaa49d" }
            Rectangle {
                x: 174; y: 225; width: 220; height: 40; color: "#633921"
                Text { anchors.centerIn: parent; text: "OPEN CAMERA"; font.pixelSize: 14; color: "#f7e7db" }
                MouseArea { anchors.fill: parent; onClicked: app.cameraRequested() }
            }
            Rectangle {
                id: askAction; x: 174; y: 272; width: 220; height: 36
                color: "#a74f2d"; visible: false
                Image { x: 62; y: 6; width: 96; height: 24; source: "ask-ru.png" }
                MouseArea { anchors.fill: parent; onClicked: app.askPhotoRequested() }
            }

            Rectangle {
                x: 0; y: 310; width: 105; height: 42; color: "#302d2e"
                Text { id: backLabel; anchors.centerIn: parent; text: "BACK"; font.pixelSize: 14; color: "#d1cbc4" }
                MouseArea { anchors.fill: parent; onClicked: app.previousStepRequested() }
            }
            Rectangle {
                id: nextAction; x: 295; y: 310; width: 105; height: 42; color: "#a74f2d"
                Text { anchors.centerIn: parent; text: "NEXT"; font.pixelSize: 14; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.nextStepRequested() }
            }
            Rectangle {
                id: doneAction; x: 295; y: 310; width: 105; height: 42; color: "#a74f2d"; visible: false
                Text { anchors.centerIn: parent; text: "DONE"; font.pixelSize: 14; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.finishRequested() }
            }
        }

        Item {
            id: answerPage; x: 0; y: 80; width: 400; height: 372; visible: false
            Text { x: 0; y: 0; text: "SPARK ANSWER"; font.pixelSize: 12; color: "#eda072" }
            Text { id: answerPageCount; x: 300; y: 0; font.pixelSize: 12; color: "#aaa49d" }
            Rectangle { x: 0; y: 25; width: 400; height: 2; color: "#393536" }
            Rectangle { x: 0; y: 38; width: 160; height: 120; color: "#252427" }
            Image { id: answerPhoto; x: 0; y: 38; width: 160; height: 120; source: "photo-slot.png"; visible: false }
            Text { id: answerLine1; x: 0; y: 166; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine2; x: 0; y: 184; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine3; x: 0; y: 202; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine4; x: 0; y: 220; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine5; x: 0; y: 238; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine6; x: 0; y: 256; font.pixelSize: 14; color: "#f1ece5" }
            Text { id: answerLine7; x: 0; y: 274; font.pixelSize: 14; color: "#f1ece5" }
            Rectangle {
                id: answerBackAction; x: 0; y: 310; width: 112; height: 42; color: "#302d2e"; visible: false
                Text { anchors.centerIn: parent; text: "BACK"; font.pixelSize: 13; color: "#d1cbc4" }
                MouseArea { anchors.fill: parent; onClicked: app.answerBackRequested() }
            }
            Rectangle {
                id: answerCancelAction; x: 0; y: 310; width: 112; height: 42; color: "#633921"; visible: false
                Text { anchors.centerIn: parent; text: "CANCEL"; font.pixelSize: 12; color: "#f7e7db" }
                MouseArea { anchors.fill: parent; onClicked: app.answerBackRequested() }
            }
            Rectangle {
                id: answerPreviousAction; x: 120; y: 310; width: 112; height: 42; color: "#633921"; visible: false
                Text { anchors.centerIn: parent; text: "PREVIOUS"; font.pixelSize: 12; color: "#f7e7db" }
                MouseArea { anchors.fill: parent; onClicked: app.answerPreviousRequested() }
            }
            Rectangle {
                id: answerNextAction; x: 240; y: 310; width: 112; height: 42; color: "#a74f2d"; visible: false
                Text { anchors.centerIn: parent; text: "NEXT"; font.pixelSize: 13; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.answerNextRequested() }
            }
            Rectangle {
                id: answerRetryAction; x: 240; y: 310; width: 160; height: 42; color: "#a74f2d"; visible: false
                Text { anchors.centerIn: parent; text: "RETRY"; font.pixelSize: 13; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.answerRetryRequested() }
            }
        }

        Item {
            id: cameraPage; x: 0; y: 80; width: 400; height: 372; visible: false
            Text { x: 0; y: 0; text: "CAMERA"; font.pixelSize: 12; color: "#eda072" }
            Text { id: cameraStep; x: 250; y: 0; font.pixelSize: 12; color: "#aaa49d" }
            Item {
                x: 330; y: 0; width: 70; height: 22
                Text { anchors.centerIn: parent; text: "BACK"; font.pixelSize: 12; color: "#d1cbc4" }
                MouseArea { anchors.fill: parent; onClicked: app.cameraCancelRequested() }
            }
            Rectangle { x: 0; y: 25; width: 400; height: 2; color: "#393536" }
            Rectangle { x: 0; y: 43; width: 400; height: 230; color: "#252427" }
            Image { id: cameraPreview; x: 120; y: 65; width: 160; height: 120; source: "photo-slot.png"; visible: false }
            Text { id: cameraStatus; x: 24; y: 198; font.pixelSize: 14; color: "#d1cbc4" }
            Rectangle {
                id: captureAction; x: 0; y: 286; width: 195; height: 44; color: "#a74f2d"
                Text { anchors.centerIn: parent; text: "TAKE PHOTO"; font.pixelSize: 14; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.cameraCaptureRequested() }
            }
            Rectangle {
                id: retakeAction; x: 0; y: 286; width: 195; height: 44; color: "#633921"; visible: false
                Text { anchors.centerIn: parent; text: "RETAKE"; font.pixelSize: 14; color: "#f7e7db" }
                MouseArea { anchors.fill: parent; onClicked: app.cameraRetakeRequested() }
            }
            Rectangle {
                id: useAction; x: 205; y: 286; width: 195; height: 44; color: "#a74f2d"; visible: false
                Text { anchors.centerIn: parent; text: "USE PHOTO"; font.pixelSize: 14; color: "#ffffff" }
                MouseArea { anchors.fill: parent; onClicked: app.cameraUseRequested() }
            }
        }
    }
}
