import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MxCut

// The right panel: the export queue, what each queued video is saved as and
// where, and the controls that run it.
Rectangle {
    id: root

    required property ExportQueue queue
    required property Settings settings
    required property var formatTime

    readonly property bool replacing: settings.replaceOriginal

    function localPath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return s
        // file:///C:/x -> C:/x on Windows, file:///home/x -> /home/x elsewhere.
        const path = decodeURIComponent(s.substring(7))
        return /^\/[A-Za-z]:/.test(path) ? path.substring(1) : path
    }

    signal jobActivated(url source)

    color: Theme.surface

    FolderDialog {
        id: saveFolderDialog
        title: "Save trimmed videos to"
        currentFolder: root.settings.outputFolder
        onAccepted: {
            root.settings.outputFolder = selectedFolder
            root.settings.saveBesideSource = false
        }
    }

    component SectionLabel: Text {
        color: Theme.textFaint
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8
            Icon {
                path: Icons.queueAdd
                size: 18
                color: Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                text: "Export queue"
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                text: root.queue.count
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftMargin: 8
            rightMargin: 8
            clip: true
            spacing: 6
            model: root.queue
            ScrollBar.vertical: ScrollBar {}

            delegate: AbstractButton {
                id: row
                required property int index
                required property string name
                required property url source
                required property real inPoint
                required property real outPoint
                required property int segmentCount
                required property real keptDuration
                required property int mode
                required property int jobState
                required property real progress
                required property string errorString
                required property string errorDetails

                readonly property color stateColor: jobState === TrimJob.Finished ? Theme.success
                    : jobState === TrimJob.Failed ? Theme.danger
                    : jobState === TrimJob.Running ? Theme.accent
                    : jobState === TrimJob.Cancelled ? Theme.textFaint : Theme.border

                width: ListView.view.width - 16
                padding: 10
                leftPadding: 14
                implicitHeight: card.implicitHeight + topPadding + bottomPadding
                hoverEnabled: true
                onClicked: root.jobActivated(source)

                background: Rectangle {
                    radius: Theme.radius + 2
                    color: row.hovered ? Theme.hover : Theme.surfaceRaised
                    border.color: Theme.border
                    Rectangle {
                        width: 3
                        x: 0
                        y: 8
                        height: parent.height - 16
                        radius: 2
                        color: row.stateColor
                    }
                }

                contentItem: ColumnLayout {
                    id: card
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: row.name
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                        }
                        IconButton {
                            iconPath: Icons.close
                            iconSize: 14
                            implicitWidth: 22
                            implicitHeight: 22
                            tint: Theme.textDim
                            tip: row.jobState === TrimJob.Running ? "Cancel and remove" : "Remove"
                            onClicked: root.queue.remove(row.index)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.segmentCount > 1
                              ? row.segmentCount + " segments, " + root.formatTime(row.keptDuration) + " in all"
                              : root.formatTime(row.inPoint) + "  →  " + root.formatTime(row.outPoint)
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.family: Theme.mono
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: row.mode === TrimJob.Fast ? "FAST" : "ACCURATE"
                            color: Theme.textFaint
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.6
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            color: row.jobState === TrimJob.Idle ? Theme.textDim : row.stateColor
                            text: row.jobState === TrimJob.Running ? Math.round(row.progress * 100) + "%"
                                : row.jobState === TrimJob.Finished ? "Done"
                                : row.jobState === TrimJob.Failed ? "Failed"
                                : row.jobState === TrimJob.Cancelled ? "Cancelled" : "Waiting"
                        }
                    }
                    Rectangle {
                        visible: row.jobState === TrimJob.Running
                        Layout.fillWidth: true
                        implicitHeight: 3
                        radius: 2
                        color: Theme.hover
                        Rectangle {
                            width: parent.width * row.progress
                            height: parent.height
                            radius: 2
                            color: Theme.accent
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: row.jobState === TrimJob.Failed
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.danger
                        text: row.errorString
                        ToolTip.visible: failHover.hovered && row.errorDetails !== ""
                        ToolTip.text: row.errorDetails
                        HoverHandler { id: failHover }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "Set the in and out points on a video, then press Add to queue (Q)."
                color: Theme.textFaint
                font.pixelSize: 13
                lineHeight: 1.3
            }
        }


        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        // One name per queued video. Left empty, a field shows what the
        // filename pattern in Settings will call it.
        SectionLabel {
            Layout.leftMargin: 14
            Layout.topMargin: 12
            text: "FILE NAMES"
            opacity: root.replacing ? 0.5 : 1
        }

        ListView {
            id: names
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.preferredHeight: count === 0 ? 0 : Math.min(contentHeight, 4 * 52)
            clip: true
            spacing: 4
            interactive: contentHeight > height
            model: root.queue
            enabled: !root.replacing
            opacity: root.replacing ? 0.5 : 1
            ScrollBar.vertical: ScrollBar {}

            delegate: ColumnLayout {
                id: nameRow
                required property int index
                required property string name
                required property string outputName
                required property string defaultName
                required property string extension
                required property string output
                required property int jobState

                readonly property bool editable: jobState !== TrimJob.Running && jobState !== TrimJob.Finished

                // A finished trim shows the name it was saved as.
                function shownName() {
                    if (jobState === TrimJob.Finished && output !== "")
                        return output.substring(Math.max(output.lastIndexOf("/"), output.lastIndexOf("\\")) + 1)
                                     .replace(/\.[^.]*$/, "")
                    return outputName
                }
                function refresh() {
                    if (!nameField.activeFocus)
                        nameField.text = shownName()
                }
                function commit() {
                    const typed = nameField.text.trim()
                    root.queue.setOutputName(index, typed === defaultName ? "" : typed)
                    refresh()
                }

                x: 14
                width: ListView.view.width - 28
                spacing: 2

                Component.onCompleted: refresh()
                onOutputNameChanged: refresh()
                onOutputChanged: refresh()
                onJobStateChanged: refresh()

                Text {
                    Layout.fillWidth: true
                    text: nameRow.name
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: Theme.radius
                    color: Theme.background
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                    opacity: nameRow.editable ? 1 : 0.6

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 0

                        TextInput {
                            id: nameField
                            Layout.fillWidth: true
                            clip: true
                            color: Theme.text
                            selectionColor: Theme.accent
                            selectedTextColor: "white"
                            font.pixelSize: 12
                            readOnly: !nameRow.editable
                            selectByMouse: true
                            // Start from the default, so renaming is an edit, not a retype.
                            onActiveFocusChanged: {
                                if (activeFocus && text === "" && nameRow.editable) {
                                    text = nameRow.defaultName
                                    selectAll()
                                } else if (!activeFocus && nameRow.editable) {
                                    nameRow.commit()
                                }
                            }
                            onAccepted: focus = false
                            Keys.onEscapePressed: { text = nameRow.outputName; focus = false; nameRow.refresh() }

                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                visible: nameField.text === "" && !nameField.activeFocus
                                text: nameRow.defaultName
                                color: Theme.textDim
                                font: nameField.font
                                elide: Text.ElideMiddle
                            }
                        }
                        Text {
                            text: "." + nameRow.extension
                            color: Theme.textFaint
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 2
            visible: names.count === 0 || root.replacing
            wrapMode: Text.WordWrap
            text: root.replacing ? "Each trim replaces its original, keeping its name. Change this in Settings."
                                 : "Queued videos can be renamed here."
            color: Theme.textFaint
            font.pixelSize: 11
        }

        SectionLabel {
            Layout.leftMargin: 14
            Layout.topMargin: 12
            text: "SAVE TO"
            opacity: root.replacing ? 0.5 : 1
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 10
            Layout.topMargin: 4
            spacing: 6
            enabled: !root.replacing
            opacity: root.replacing ? 0.5 : 1

            Icon {
                path: Icons.folder
                size: 16
                color: Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: root.settings.saveBesideSource || root.settings.outputFolder.toString() === ""
                      ? "Next to each original"
                      : root.localPath(root.settings.outputFolder)
                color: Theme.text
                font.pixelSize: 12
                ToolTip.visible: saveToHover.hovered && !root.settings.saveBesideSource
                ToolTip.text: root.localPath(root.settings.outputFolder)
                HoverHandler { id: saveToHover }
            }
            IconButton {
                text: "Change…"
                implicitHeight: 28
                tip: "Choose a folder for every trimmed video"
                onClicked: saveFolderDialog.open()
            }
        }
        IconButton {
            Layout.leftMargin: 8
            implicitHeight: 26
            visible: !root.settings.saveBesideSource && !root.replacing
            text: "Save next to each original instead"
            tint: Theme.textDim
            onClicked: root.settings.saveBesideSource = true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 12
            implicitHeight: 1
            color: Theme.border
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 10
            visible: root.queue.finishedCount + root.queue.failedCount > 0
            text: root.queue.finishedCount + " done"
                  + (root.queue.failedCount > 0 ? "  ·  " + root.queue.failedCount + " failed" : "")
                  + (root.queue.pendingCount > 0 ? "  ·  " + root.queue.pendingCount + " waiting" : "")
            color: Theme.textDim
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 10
            Layout.topMargin: 6
            spacing: 6

            Text {
                text: "At once"
                color: Theme.textDim
                font.pixelSize: 12
            }
            IconButton {
                text: "−"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.queue.maxConcurrent > 1
                onClicked: root.queue.settings.maxConcurrent = root.queue.maxConcurrent - 1
            }
            Text {
                text: root.queue.maxConcurrent
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: 13
                ToolTip.visible: concurrencyHover.hovered
                ToolTip.text: "How many videos are trimmed at the same time"
                HoverHandler { id: concurrencyHover }
            }
            IconButton {
                text: "+"
                implicitWidth: 26
                implicitHeight: 26
                enabled: root.queue.maxConcurrent < 8
                onClicked: root.queue.settings.maxConcurrent = root.queue.maxConcurrent + 1
            }
            Item { Layout.fillWidth: true }
            IconButton {
                text: "Clear done"
                enabled: root.queue.finishedCount > 0
                onClicked: root.queue.clearFinished()
            }
        }

        IconButton {
            Layout.fillWidth: true
            Layout.margins: 12
            implicitHeight: 40
            primary: !root.queue.running
            iconPath: root.queue.running ? Icons.close : Icons.play
            enabled: root.queue.running || root.queue.count > root.queue.finishedCount
            text: root.queue.running ? "Cancel" : "Start trimming"
            onClicked: root.queue.running ? root.queue.cancel() : root.queue.start()
        }
    }
}
