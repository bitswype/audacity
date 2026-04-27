/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Playback 1.0

StyledDialogView {
    id: root

    title: qsTrc("playback", "Channel Test Tone")

    contentWidth: 560
    contentHeight: 420

    margins: 16
    modal: true

    ChannelTestToneModel {
        id: toneModel
    }

    Component.onCompleted: {
        toneModel.load()
    }

    onClosed: {
        if (toneModel.active) {
            toneModel.stop()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            text: qsTrc("playback", "Generate a known signal on selected output channels.  Useful for verifying device routing and the routing engine.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Mode selection
        RowLayout {
            spacing: 12

            StyledTextLabel { text: qsTrc("playback", "Mode:") }

            RadioButton {
                text: qsTrc("playback", "Direct hardware")
                checked: toneModel.mode === 1
                onToggled: if (checked) toneModel.mode = 1
            }
            RadioButton {
                text: qsTrc("playback", "Through routing matrix")
                checked: toneModel.mode === 2
                onToggled: if (checked) toneModel.mode = 2
            }
        }

        // Tone type
        RowLayout {
            spacing: 12

            StyledTextLabel { text: qsTrc("playback", "Type:") }

            RadioButton {
                text: qsTrc("playback", "Sine")
                checked: toneModel.toneType === 0
                onToggled: if (checked) toneModel.toneType = 0
            }
            RadioButton {
                text: qsTrc("playback", "Pink")
                checked: toneModel.toneType === 1
                onToggled: if (checked) toneModel.toneType = 1
            }
            RadioButton {
                text: qsTrc("playback", "White")
                checked: toneModel.toneType === 2
                onToggled: if (checked) toneModel.toneType = 2
            }
        }

        // Frequency + level controls
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            StyledTextLabel { text: qsTrc("playback", "Frequency (Hz):") }
            TextField {
                Layout.preferredWidth: 100
                text: toneModel.frequencyHz.toFixed(1)
                validator: DoubleValidator { bottom: 1.0; top: 24000.0 }
                onEditingFinished: {
                    var v = parseFloat(text)
                    if (!isNaN(v)) {
                        toneModel.frequencyHz = v
                    }
                }
            }

            StyledTextLabel { text: qsTrc("playback", "Level (dBFS):") }
            RowLayout {
                Slider {
                    Layout.preferredWidth: 200
                    from: -90; to: 0; stepSize: 1
                    value: toneModel.levelDb
                    onMoved: toneModel.levelDb = value
                }
                StyledTextLabel {
                    text: toneModel.levelDb.toFixed(1) + " dB"
                }
            }
        }

        // Channel grid
        StyledTextLabel {
            text: qsTrc("playback", "Output channels:")
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            contentWidth: channelRow.implicitWidth
            clip: true
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Row {
                id: channelRow
                spacing: 4

                Repeater {
                    model: toneModel.channelCount
                    delegate: ColumnLayout {
                        spacing: 2
                        StyledTextLabel {
                            text: (modelData + 1).toString()
                            horizontalAlignment: Text.AlignHCenter
                            Layout.preferredWidth: 28
                        }
                        CheckBox {
                            checked: toneModel.isChannelOn(modelData)
                            onToggled: toneModel.setChannelOn(modelData, checked)

                            Connections {
                                target: toneModel
                                function onChannelStateChanged(ch) {
                                    if (ch === modelData) {
                                        checked = toneModel.isChannelOn(ch)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            FlatButton {
                text: qsTrc("playback", "Select all")
                onClicked: toneModel.selectAllChannels()
            }
            FlatButton {
                text: qsTrc("playback", "Clear")
                onClicked: toneModel.clearAllChannels()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: toneModel.active ? qsTrc("playback", "Stop")
                                       : qsTrc("playback", "Start")
                accentButton: true
                onClicked: {
                    if (toneModel.active) {
                        toneModel.stop()
                    } else {
                        toneModel.start()
                    }
                }
            }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: {
                    if (toneModel.active) {
                        toneModel.stop()
                    }
                    root.accept()
                }
            }
        }
    }
}
