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

    title: qsTrc("playback", "Recording Routing Matrix")

    contentWidth: 640
    contentHeight: 480

    margins: 16
    modal: true

    RecordingRoutingModel {
        id: routingModel
    }

    Component.onCompleted: {
        routingModel.load()
    }

    ColumnLayout {
        id: rootLayout
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            text: qsTrc("playback", "Each checkbox routes a device input channel into the corresponding track. An empty row means the track does not receive any input.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Flickable {
            id: matrixScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: matrixGrid.implicitWidth
            contentHeight: matrixGrid.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            GridLayout {
                id: matrixGrid
                rowSpacing: 4
                columnSpacing: 8
                columns: routingModel.displayChannelCount + 1

                Item { implicitWidth: 160; implicitHeight: 24 }
                Repeater {
                    model: routingModel.displayChannelCount
                    StyledTextLabel {
                        text: routingModel.isDeviceChannel(modelData)
                            ? (modelData + 1).toString()
                            : (modelData + 1).toString() + "*"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 28
                        opacity: routingModel.isDeviceChannel(modelData) ? 1.0 : 0.6
                    }
                }

                Repeater {
                    model: routingModel.trackCount
                    delegate: TrackRow {
                        trackIndex: index
                    }
                }
            }
        }

        StyledTextLabel {
            visible: routingModel.displayChannelCount > routingModel.channelCount
            text: qsTrc("playback", "Columns marked with * route from inputs beyond the current recording device (%1 channels).  Those bits are silent until you switch to a device with more inputs.").arg(routingModel.channelCount)
            wrapMode: Text.Wrap
            opacity: 0.7
            Layout.fillWidth: true
        }

        component TrackRow : Row {
            id: trackRow
            property int trackIndex
            spacing: matrixGrid.columnSpacing
            Layout.column: 0
            Layout.row: trackIndex + 1
            Layout.columnSpan: matrixGrid.columns

            StyledTextLabel {
                text: routingModel.trackName(trackRow.trackIndex)
                width: 160
                elide: Text.ElideRight
            }

            Repeater {
                model: routingModel.displayChannelCount
                delegate: CheckBox {
                    width: 28
                    checked: routingModel.isRouted(trackRow.trackIndex, modelData)
                    opacity: routingModel.isDeviceChannel(modelData) ? 1.0 : 0.6
                    onToggled: routingModel.setRouted(trackRow.trackIndex, modelData, checked)

                    Connections {
                        target: routingModel
                        function onCellChanged(t, c) {
                            if (t === trackRow.trackIndex && c === modelData) {
                                checked = routingModel.isRouted(t, c)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            FlatButton {
                text: qsTrc("playback", "Reset all")
                onClicked: routingModel.resetIdentity()
            }
            FlatButton {
                text: qsTrc("playback", "Clear all")
                onClicked: routingModel.clearAll()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Cancel")
                onClicked: root.reject()
            }
            FlatButton {
                text: qsTrc("global", "Apply")
                accentButton: true
                onClicked: {
                    routingModel.apply()
                    root.accept()
                }
            }
        }
    }
}
