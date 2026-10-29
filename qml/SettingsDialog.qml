import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MxCut

Dialog {
    id: root

    required property Settings settings
    required property EncoderProbe probe

    title: "Settings"
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(720, parent ? parent.height - 40 : 720)
    anchors.centerIn: parent
    padding: 20

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radius + 4
        border.color: Theme.border
    }

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }

    function localPath(url) {
        const s = url.toString()
        if (!s.startsWith("file://"))
            return s
        // file:///C:/x -> C:/x on Windows, file:///home/x -> /home/x elsewhere.
        const path = decodeURIComponent(s.substring(7))
        return /^\/[A-Za-z]:/.test(path) ? path.substring(1) : path
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        color: Theme.textDim
    }

    // Shows a shortcut key; click it, then press the new key.
    component KeyField: Button {
        id: keyField
        property string key
        property string otherKey
        property bool listening: false
        signal picked(string key)

        // Keys the app already uses for something else.
        readonly property var reserved: ["SPACE", "Q", "S", "[", "]"]

        function keyName(event) {
            if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12)
                return "F" + (event.key - Qt.Key_F1 + 1)
            if ((event.key >= Qt.Key_A && event.key <= Qt.Key_Z) || (event.key >= Qt.Key_0 && event.key <= Qt.Key_9))
                return String.fromCharCode(event.key)
            const t = event.text.trim()
            return t.length === 1 ? t.toUpperCase() : ""
        }

        Layout.preferredWidth: 130
        highlighted: listening
        text: listening ? "Press a key…" : key
        font.family: Theme.mono
        onClicked: { listening = true; forceActiveFocus() }
        onActiveFocusChanged: if (!activeFocus) listening = false

        Keys.onPressed: event => {
            if (!listening)
                return
            event.accepted = true
            if (event.key === Qt.Key_Escape) {
                listening = false
                return
            }
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return
            const name = keyName(event)
            if (name === "" || reserved.indexOf(name) >= 0 || name === otherKey.toUpperCase())
                return
            listening = false
            picked(name)
        }
    }

    FolderDialog {
        id: outputFolderDialog
        title: "Save trimmed videos to"
        onAccepted: {
            root.settings.outputFolder = selectedFolder
            root.settings.saveBesideSource = false
        }
    }

    FileDialog {
        id: ffmpegDialog
        title: "Choose the FFmpeg program"
        onAccepted: root.settings.ffmpegPath = root.localPath(selectedFile)
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14

            GroupBox {
                title: "Output"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    CheckBox {
                        text: "Delete the original video"
                        checked: root.settings.deleteOriginal
                        onToggled: root.settings.deleteOriginal = checked
                    }
                    Hint {
                        visible: root.settings.deleteOriginal
                        text: "Once the trimmed video is saved and checked, the original goes to the Trash (Recycle "
                              + "Bin on Windows), wherever the trim is saved. If the trim fails, the original is kept. "
                              + "Use {name} as the file name to give the trim the original's name."
                    }

                    RadioButton {
                        text: "Save next to the original video"
                        checked: root.settings.saveBesideSource
                        onToggled: root.settings.saveBesideSource = true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: "Save in:"
                            checked: !root.settings.saveBesideSource
                            enabled: root.settings.outputFolder.toString() !== ""
                            onToggled: root.settings.saveBesideSource = false
                        }
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            placeholderText: "Choose a folder"
                            text: root.localPath(root.settings.outputFolder)
                        }
                        Button {
                            text: "Browse…"
                            onClicked: outputFolderDialog.open()
                        }
                    }

                    Label { text: "File name"; Layout.topMargin: 6 }
                    TextField {
                        Layout.fillWidth: true
                        text: root.settings.filenamePattern
                        placeholderText: "{name}_trim"
                        onTextEdited: root.settings.filenamePattern = text
                    }
                    Hint {
                        text: "{name} original name · {in} and {out} cut times · {mode} fast or accurate · "
                              + "{date} and {time} when trimmed. The extension is added for you."
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: {
                            root.settings.filenamePattern
                            return "Example: holiday.mov → " + root.settings.previewFileName("holiday.mov", TrimJob.Fast)
                        }
                    }
                    CheckBox {
                        text: "Overwrite files that already exist"
                        checked: root.settings.overwrite
                        onToggled: root.settings.overwrite = checked
                    }
                    Hint {
                        visible: !root.settings.overwrite
                        text: "Otherwise a number is added, like holiday_trim_2.mov."
                    }
                }
            }

            GroupBox {
                title: "Accurate mode encoding"
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Label { text: "Video encoder" }
                    ComboBox {
                        id: encoderBox
                        Layout.fillWidth: true
                        model: root.probe.encoders
                        textRole: "label"
                        valueRole: "id"
                        currentIndex: indexOfValue(root.settings.videoEncoder)
                        onActivated: root.settings.videoEncoder = currentValue
                        displayText: {
                            const item = model[currentIndex]
                            if (!item)
                                return ""
                            return item.label + (!root.probe.probing && !item.available ? " (not working here)" : "")
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            enabled: modelData.available
                            highlighted: encoderBox.highlightedIndex === index
                            text: modelData.label + (modelData.available ? ""
                                  : root.probe.probing ? " (checking…)" : " (not available)")
                        }
                    }

                    Item { implicitWidth: 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        BusyIndicator {
                            running: root.probe.probing
                            visible: running
                            implicitWidth: 22
                            implicitHeight: 22
                        }
                        Hint {
                            text: root.probe.probing ? "Checking which encoders work on this computer…"
                                : !root.probe.ffmpegFound ? "FFmpeg wasn't found, so no encoders can be checked."
                                : "GPU encoders are listed only if a test encode worked on this computer."
                        }
                        Button {
                            text: "Check again"
                            enabled: !root.probe.probing
                            onClicked: root.probe.probe()
                        }
                    }

                    Label { text: "Quality" }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 51
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: root.settings.quality
                            onMoved: root.settings.quality = value
                        }
                        Label {
                            text: root.settings.quality
                            font.family: Theme.mono
                            Layout.preferredWidth: 24
                        }
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: "Lower numbers mean better quality and bigger files. 18 looks the same as the original; 23 to 28 is smaller." }

                    Label { text: "Speed" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["Fast", "Balanced", "Best compression (slow)"]
                        currentIndex: root.settings.speed
                        onActivated: root.settings.speed = currentIndex
                    }

                    Label { text: "Audio" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            RadioButton {
                                text: "Re-encode to AAC at"
                                checked: !root.settings.copyAudio
                                onToggled: root.settings.copyAudio = false
                            }
                            SpinBox {
                                from: 64
                                to: 320
                                stepSize: 32
                                editable: true
                                value: root.settings.audioBitrate
                                enabled: !root.settings.copyAudio
                                onValueModified: root.settings.audioBitrate = value
                            }
                            Label { text: "kbit/s" }
                        }
                        RadioButton {
                            text: "Copy the original audio"
                            checked: root.settings.copyAudio
                            onToggled: root.settings.copyAudio = true
                        }
                    }

                    Item { implicitWidth: 1 }
                    Button {
                        text: "Reset encoding to defaults"
                        onClicked: root.settings.resetEncoding()
                    }
                }
            }

            GroupBox {
                title: "Keyboard shortcuts"
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Label { text: "Set in point" }
                    KeyField {
                        key: root.settings.inKey
                        otherKey: root.settings.outKey
                        onPicked: key => root.settings.inKey = key
                    }
                    Label { text: "Set out point" }
                    KeyField {
                        key: root.settings.outKey
                        otherKey: root.settings.inKey
                        onPicked: key => root.settings.outKey = key
                    }
                    Item { implicitWidth: 1 }
                    Hint { text: "Click a key, then press the one you want. Esc keeps the old key. Space, Q, S, [ and ] are already used." }
                    Item { implicitWidth: 1 }
                    Button {
                        text: "Reset to A and D"
                        onClicked: root.settings.resetShortcuts()
                    }
                }
            }

            GroupBox {
                title: "FFmpeg"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        color: root.probe.ffmpegFound || root.probe.probing ? Theme.text : Theme.danger
                        text: root.probe.probing && root.probe.ffmpegPath === "" ? "Looking for FFmpeg…"
                            : root.probe.ffmpegFound
                              ? "Using FFmpeg " + root.probe.ffmpegVersion + " at " + root.probe.ffmpegPath
                              : "FFmpeg wasn't found. Install it, or choose the program below."
                    }
                    RadioButton {
                        text: "Find FFmpeg automatically"
                        checked: root.settings.ffmpegPath === ""
                        onToggled: root.settings.ffmpegPath = ""
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: "Use:"
                            checked: root.settings.ffmpegPath !== ""
                            enabled: root.settings.ffmpegPath !== ""
                        }
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            placeholderText: "Choose the ffmpeg program"
                            text: root.settings.ffmpegPath
                        }
                        Button {
                            text: "Browse…"
                            onClicked: ffmpegDialog.open()
                        }
                    }
                }
            }
        }
    }
}
