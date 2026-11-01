import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import MxCut

// A newer mxCut: what's in it, and downloading and running its installer.
Dialog {
    id: root

    required property Updater updater
    // Trimming would be cut short by the installer closing mxCut.
    property bool busy: false

    title: "mxCut " + updater.latestVersion + " is available"
    modal: true
    width: Math.min(520, parent ? parent.width - 40 : 520)
    anchors.centerIn: parent
    padding: 20

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radius + 4
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            text: "You have " + root.updater.currentVersion + ". The update downloads the installer, closes mxCut, "
                  + "and installs over this copy; your settings stay as they are."
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(notes.implicitHeight + 16, 220)
            visible: root.updater.notes !== ""
            clip: true
            background: Rectangle {
                color: Theme.background
                radius: Theme.radius
                border.color: Theme.border
            }
            Label {
                id: notes
                width: root.availableWidth - 16
                x: 8
                y: 8
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                color: Theme.text
                linkColor: Theme.accent
                text: root.updater.notes
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.updater.state === Updater.Downloading
            spacing: 4
            ProgressBar {
                Layout.fillWidth: true
                value: root.updater.progress
            }
            Label {
                text: "Downloading… " + Math.round(root.updater.progress * 100) + "%"
                color: Theme.textDim
                font.pixelSize: 12
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.updater.state === Updater.Failed
            wrapMode: Text.WordWrap
            color: Theme.danger
            text: root.updater.errorString
        }
        Label {
            Layout.fillWidth: true
            visible: root.busy && root.updater.state === Updater.ReadyToInstall
            wrapMode: Text.WordWrap
            color: Theme.warning
            text: "Videos are being trimmed. Install once they're done, or the trims will stop."
        }
    }

    footer: DialogButtonBox {
        Button {
            text: "Release page"
            flat: true
            onClicked: Qt.openUrlExternally(root.updater.releasePage)
            DialogButtonBox.buttonRole: DialogButtonBox.HelpRole
        }
        Button {
            text: root.updater.state === Updater.Downloading ? "Cancel" : "Later"
            flat: true
            onClicked: {
                if (root.updater.state === Updater.Downloading)
                    root.updater.cancel()
                else
                    root.close()
            }
        }
        Button {
            highlighted: true
            enabled: root.updater.state !== Updater.Downloading
            text: root.updater.state === Updater.ReadyToInstall ? "Close mxCut and install"
                : root.updater.state === Updater.Failed ? "Try again"
                : "Download and install"
            onClicked: {
                if (root.updater.state === Updater.ReadyToInstall)
                    root.updater.install()
                else
                    root.updater.download()
            }
        }
    }
}
