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

    title: qsTrc("playback", "Playback Routing Matrix")

    contentWidth: 640
    contentHeight: 480

    margins: 16
    modal: true

    PlaybackRoutingModel {
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
            text: qsTrc("playback", "Each checkbox routes a track's audio to the corresponding device output channel. An empty row means the track is silenced (no playback).")
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
                columns: routingModel.channelCount + 1

                // Header row: blank + numbered columns
                Item { implicitWidth: 160; implicitHeight: 24 }
                Repeater {
                    model: routingModel.channelCount
                    StyledTextLabel {
                        text: (modelData + 1).toString()
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 28
                    }
                }

                // One row per track: track name + checkboxes
                Repeater {
                    model: routingModel.trackCount
                    delegate: TrackRow {
                        trackIndex: index
                    }
                }
            }
        }

        component TrackRow : Row {
            id: trackRow
            property int trackIndex
            spacing: matrixGrid.columnSpacing
            // Make each TrackRow span the whole grid row
            Layout.column: 0
            Layout.row: trackIndex + 1
            Layout.columnSpan: matrixGrid.columns

            StyledTextLabel {
                text: routingModel.trackName(trackRow.trackIndex)
                width: 160
                elide: Text.ElideRight
            }

            Repeater {
                model: routingModel.channelCount
                delegate: CheckBox {
                    width: 28
                    checked: routingModel.isRouted(trackRow.trackIndex, modelData)
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
