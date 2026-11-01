import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import MxCut

// Folder mode's left panel: a file browser for one folder, its subfolders and
// videos as thumbnail cards grouped by day, each marked with its state in the
// export queue.
Rectangle {
    id: root

    required property VideoFolderModel folderModel
    property url currentUrl

    signal videoSelected(url url)
    signal folderOpened(url url)
    signal chooseFolder()
    signal closeRequested()
    // File actions; the window does them, as it may have to let go of the
    // video first.
    signal renameRequested(url url, string newName)
    signal deleteRequested(var urls, bool permanently)

    // Selected cards, as a set of URL strings. A click selects one card,
    // Ctrl+click adds or removes one, Shift+click selects a run of them.
    property var selection: ({})
    readonly property int selectionCount: Object.keys(selection).length
    property string anchorUrl
    // The card whose name is being edited.
    property string renamingUrl

    color: Theme.surface

    // Dragged down to its narrowest, the header and filters shrink to fit.
    readonly property bool narrow: width > 0 && width < 260
    readonly property int gap: 10
    readonly property int sidePadding: 12
    readonly property real gridWidth: width - 2 * sidePadding
    readonly property int columns: Math.max(1, Math.floor((gridWidth + gap) / (Theme.thumbnailWidth + gap)))
    readonly property real cardWidth: Math.floor((gridWidth - gap * (columns - 1)) / columns)
    readonly property bool byDate: folderModel.sort === VideoFolderModel.Newest
                                   || folderModel.sort === VideoFolderModel.Oldest

    // The rows of cards: each group cut into rows of `columns`, the first row
    // of a group carrying its title.
    readonly property var cardRows: {
        const rows = []
        for (const group of folderModel.groups) {
            for (let i = 0; i < group.count; i += columns)
                rows.push({ title: i === 0 ? group.title : "", total: group.count,
                            start: group.start + i, count: Math.min(columns, group.count - i) })
        }
        return rows
    }

    function badge(state) {
        switch (state) {
        case TrimJob.Idle: return { text: "Queued", color: Theme.accent, ink: "white" }
        case TrimJob.Running: return { text: "Trimming", color: Theme.warning, ink: "black" }
        case TrimJob.Finished: return { text: "Trimmed", color: Theme.success, ink: "#04130a" }
        case TrimJob.Failed: return { text: "Failed", color: Theme.danger, ink: "white" }
        }
        return null
    }

    function formatSize(bytes) {
        if (bytes >= 1e9)
            return (bytes / 1e9).toFixed(1) + " GB"
        if (bytes >= 1e6)
            return Math.round(bytes / 1e6) + " MB"
        return Math.max(1, Math.round(bytes / 1e3)) + " KB"
    }

    function formatDuration(seconds) {
        if (!(seconds >= 0))
            return ""
        const s = Math.round(seconds)
        const pad = n => String(n).padStart(2, "0")
        return s >= 3600 ? Math.floor(s / 3600) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
                         : Math.floor(s / 60) + ":" + pad(s % 60)
    }

    readonly property var sortNames: ["Newest", "Oldest", "Name", "Largest"]
    readonly property string trashName: Qt.platform.os === "windows" ? "Recycle Bin" : "Trash"

    // The selected items still in the folder, in the order shown.
    function selectedUrls() {
        const urls = []
        for (let row = 0; row < folderModel.count; ++row) {
            const url = folderModel.urlAt(row)
            if (selection[String(url)] === true)
                urls.push(url)
        }
        return urls
    }

    function select(urls) {
        const set = {}
        for (const url of urls)
            set[String(url)] = true
        selection = set
    }

    function cardClicked(info, button, modifiers) {
        const key = String(info.url)
        if (button === Qt.RightButton) {
            if (selection[key] !== true) {
                select([info.url])
                anchorUrl = key
            }
            cardMenu.targets = selectedUrls()
            cardMenu.popup()
            return
        }
        if (modifiers & Qt.ShiftModifier && anchorUrl !== "") {
            const from = folderModel.indexOf(anchorUrl), to = folderModel.indexOf(info.url)
            if (from >= 0 && to >= 0) {
                const urls = []
                for (let row = Math.min(from, to); row <= Math.max(from, to); ++row)
                    urls.push(folderModel.urlAt(row))
                select(urls)
                return
            }
        }
        if (modifiers & Qt.ControlModifier) {
            const set = Object.assign({}, selection)
            if (set[key] === true)
                delete set[key]
            else
                set[key] = true
            selection = set
            anchorUrl = key
            return
        }
        select([info.url])
        anchorUrl = key
        if (info.isFolder)
            folderOpened(info.url)
        else
            videoSelected(info.url)
    }

    function startRename(url) {
        if (folderModel.queue && folderModel.queue.busyWith(url)) {
            showMessage("That's being trimmed right now, so it can't be renamed yet.")
            return
        }
        renamingUrl = String(url)
        grid.showRow(folderModel.indexOf(url))
    }

    function confirmDelete(urls) {
        if (urls.length === 0)
            return
        deleteDialog.urls = urls
        deleteDialog.permanently = false
        deleteDialog.open()
    }

    // The trash wasn't possible; ask before deleting for good.
    function offerPermanentDelete(urls, error) {
        deleteDialog.urls = urls
        deleteDialog.permanently = true
        deleteDialog.reason = error
        deleteDialog.open()
    }

    function copyPaths(urls) {
        clipboardHelper.text = urls.map(u => folderModel.nativePath(u)).join("\n")
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        showMessage(urls.length === 1 ? "Path copied" : urls.length + " paths copied", false)
    }

    function showMessage(text, isError) {
        message.text = text
        message.isError = isError !== false
        messageTimer.restart()
    }

    function describe(urls) {
        if (urls.length === 1)
            return "\u201c" + decodeURIComponent(String(urls[0]).substring(String(urls[0]).lastIndexOf("/") + 1)) + "\u201d"
        return urls.length + " items"
    }

    Connections {
        target: root.folderModel
        function onFolderChanged() {
            root.selection = {}
            root.anchorUrl = ""
            root.renamingUrl = ""
        }
    }

    // Copying to the clipboard goes through a text field, which QML can do
    // without help.
    TextEdit {
        id: clipboardHelper
        visible: false
    }

    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: root.visible && root.selectionCount > 0 && root.renamingUrl === "" && !root.typing()
        onActivated: root.confirmDelete(root.selectedUrls())
    }
    Shortcut {
        sequence: "F2"
        enabled: root.visible && root.selectionCount === 1 && root.renamingUrl === "" && !root.typing()
        onActivated: { const urls = root.selectedUrls(); if (urls.length === 1) root.startRename(urls[0]) }
    }
    Shortcut {
        sequence: StandardKey.SelectAll
        enabled: root.visible && !root.typing()
        onActivated: {
            const urls = []
            for (let row = 0; row < root.folderModel.count; ++row)
                urls.push(root.folderModel.urlAt(row))
            root.select(urls)
        }
    }
    function typing() {
        const item = Window.activeFocusItem
        return item !== null && typeof item.cursorPosition === "number"
    }

    Menu {
        id: cardMenu
        property var targets: []
        readonly property bool busy: {
            const queue = root.folderModel.queue
            return queue !== null && targets.some(u => queue.busyWith(u))
        }

        MenuItem {
            text: "Rename"
            enabled: cardMenu.targets.length === 1 && !cardMenu.busy
            onTriggered: root.startRename(cardMenu.targets[0])
        }
        MenuItem {
            text: cardMenu.targets.length > 1 ? "Copy paths" : "Copy path"
            onTriggered: root.copyPaths(cardMenu.targets)
        }
        MenuSeparator {}
        MenuItem {
            text: cardMenu.targets.length > 1 ? "Delete " + cardMenu.targets.length + " items" : "Delete"
            enabled: !cardMenu.busy
            onTriggered: root.confirmDelete(cardMenu.targets)
        }
    }

    Dialog {
        id: deleteDialog
        property var urls: []
        property bool permanently: false
        property string reason
        readonly property bool hasFolder: urls.some(u => root.folderModel.isFolder(u))

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 40 : 420)
        modal: true
        padding: 20
        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radius + 4
            border.color: Theme.border
        }
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }
        title: permanently ? "Delete for good?" : "Delete " + root.describe(urls) + "?"

        contentItem: Text {
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
            text: deleteDialog.permanently
                  ? deleteDialog.reason + " Delete " + root.describe(deleteDialog.urls)
                    + " permanently instead? This can't be undone."
                  : (deleteDialog.urls.length === 1 ? "It" : "They") + " will be moved to the " + root.trashName
                    + (deleteDialog.hasFolder ? ", folders with everything in them" : "")
                    + ". You can put " + (deleteDialog.urls.length === 1 ? "it" : "them") + " back from there."
        }

        footer: DialogButtonBox {
            Button {
                text: "Cancel"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: deleteDialog.permanently ? "Delete permanently" : "Move to " + root.trashName
                highlighted: true
                Material.accent: Theme.danger
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: {
            root.select([])
            root.deleteRequested(urls, permanently)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Folder, with up, choose another and close.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Layout.bottomMargin: 8
            spacing: root.narrow ? 2 : 8

            Icon {
                visible: !root.narrow
                path: Icons.folder
                size: 22
                color: Theme.accent
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: root.folderModel.folderName
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: root.folderModel.folderPath + "  ·  " + root.folderModel.videoCount
                          + (root.folderModel.videoCount === 1 ? " video" : " videos")
                          + (root.folderModel.totalSize > 0 ? "  ·  " + root.formatSize(root.folderModel.totalSize) : "")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }
            IconButton {
                iconPath: Icons.arrowUp
                iconSize: 17
                implicitWidth: 28
                implicitHeight: 28
                tip: "Up one folder"
                enabled: root.folderModel.parentFolder.toString() !== ""
                onClicked: root.folderOpened(root.folderModel.parentFolder)
            }
            IconButton {
                iconPath: Icons.openFolder
                iconSize: 17
                implicitWidth: 28
                implicitHeight: 28
                tip: "Open another folder (Ctrl+Shift+O)"
                onClicked: root.chooseFolder()
            }
            IconButton {
                iconPath: Icons.close
                iconSize: 17
                implicitWidth: 28
                implicitHeight: 28
                tip: "Close this folder. mxCut stops reopening it at launch."
                onClicked: root.closeRequested()
            }
        }

        // Search and sort.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.sidePadding
            Layout.rightMargin: root.sidePadding
            Layout.bottomMargin: 8
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 30
                radius: Theme.radius
                color: Theme.surfaceRaised
                border.color: searchField.activeFocus ? Theme.accent : Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    spacing: 6
                    Icon {
                        path: Icons.search
                        size: 15
                        color: Theme.textFaint
                    }
                    TextInput {
                        id: searchField
                        Layout.fillWidth: true
                        clip: true
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: "white"
                        font.pixelSize: 12
                        selectByMouse: true
                        onTextChanged: root.folderModel.search = text
                        Keys.onEscapePressed: { text = ""; focus = false }
                        onAccepted: focus = false

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: searchField.text === ""
                            text: "Search"
                            color: Theme.textFaint
                            font: searchField.font
                        }
                    }
                    IconButton {
                        visible: searchField.text !== ""
                        iconPath: Icons.close
                        iconSize: 13
                        implicitWidth: 22
                        implicitHeight: 22
                        onClicked: searchField.text = ""
                    }
                }
            }

            IconButton {
                id: sortButton
                implicitHeight: 30
                iconPath: Icons.sort
                iconSize: 15
                text: root.narrow ? "" : root.sortNames[root.folderModel.sort]
                // Sized from the label itself: the layout doesn't follow the
                // button's own width when the label comes and goes.
                Layout.preferredWidth: root.narrow ? 32 : sortMetrics.advanceWidth + iconSize + 7 + 24
                TextMetrics {
                    id: sortMetrics
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    text: root.sortNames[root.folderModel.sort]
                }
                tint: Theme.textDim
                tip: "Sort: " + root.sortNames[root.folderModel.sort]
                background: Rectangle {
                    radius: Theme.radius
                    color: sortButton.hovered ? Theme.hover : Theme.surfaceRaised
                    border.color: Theme.border
                }
                onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)

                Menu {
                    id: sortMenu
                    Repeater {
                        model: root.sortNames
                        MenuItem {
                            required property string modelData
                            required property int index
                            text: modelData
                            checkable: true
                            checked: root.folderModel.sort === index
                            onTriggered: root.folderModel.sort = index
                        }
                    }
                }
            }
        }

        // Filters, each with how many videos it shows.
        Item {
            Layout.fillWidth: true
            implicitHeight: 30

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            Row {
                x: 14
                height: parent.height
                spacing: 16

                Repeater {
                    model: [
                        { label: "All", filter: VideoFolderModel.All, count: root.folderModel.videoCount },
                        { label: "Queued", filter: VideoFolderModel.Queued, count: root.folderModel.queuedCount },
                        { label: "Trimmed", filter: VideoFolderModel.Trimmed, count: root.folderModel.trimmedCount },
                    ]
                    AbstractButton {
                        id: tab
                        required property var modelData
                        readonly property bool active: root.folderModel.filter === modelData.filter
                        height: parent.height
                        hoverEnabled: true
                        onClicked: root.folderModel.filter = modelData.filter
                        contentItem: Row {
                            spacing: 6
                            topPadding: 4
                            Text {
                                text: tab.modelData.label
                                color: tab.active || tab.hovered ? Theme.text : Theme.textFaint
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                visible: !root.narrow
                                width: countText.implicitWidth + 10
                                height: 16
                                radius: 8
                                color: tab.modelData.filter === VideoFolderModel.Queued && tab.modelData.count > 0
                                       ? Qt.rgba(0.18, 0.55, 1, 0.2) : Theme.surfaceRaised
                                Text {
                                    id: countText
                                    anchors.centerIn: parent
                                    text: tab.modelData.count
                                    color: tab.modelData.filter === VideoFolderModel.Queued && tab.modelData.count > 0
                                           ? Theme.accent : Theme.textDim
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                        background: Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: tab.active ? Theme.accent : "transparent"
                        }
                    }
                }
            }
        }

        ListView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.cardRows
            bottomMargin: 12

            // The rows are rebuilt whenever the folder's contents or queue
            // states change; stay where the user had scrolled to, except
            // in a different folder.
            property real keptY: 0
            onMovementEnded: keptY = contentY
            onModelChanged: Qt.callLater(() => { contentY = Math.max(0, Math.min(keptY, contentHeight - height)) })
            Connections {
                target: root.folderModel
                function onFolderChanged() { grid.keptY = 0 }
            }

            // Keep the video being edited in view as it changes, from the
            // keyboard or after adding one to the queue.
            function showCurrent() {
                showRow(root.folderModel.indexOf(root.currentUrl))
            }
            function showRow(row) {
                if (row < 0)
                    return
                const rows = root.cardRows
                for (let i = 0; i < rows.length; ++i) {
                    if (row >= rows[i].start && row < rows[i].start + rows[i].count) {
                        const item = itemAtIndex(i)
                        if (item && item.y >= contentY && item.y + item.height <= contentY + height)
                            return
                        positionViewAtIndex(i, ListView.Contain)
                        keptY = contentY
                        return
                    }
                }
            }
            // Once the rows are laid out, not straight away.
            Timer {
                id: showCurrentLater
                interval: 50
                onTriggered: grid.showCurrent()
            }
            Connections {
                target: root
                function onCurrentUrlChanged() { showCurrentLater.restart() }
            }
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Column {
                id: cardRow
                required property var modelData
                x: root.sidePadding
                width: root.gridWidth
                spacing: 0

                // Group title, on the group's first row.
                Item {
                    visible: cardRow.modelData.title !== ""
                    width: parent.width
                    height: visible ? 34 : 0
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 2
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        text: cardRow.modelData.title
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        text: cardRow.modelData.total
                        color: Theme.textFaint
                        font.pixelSize: 11
                    }
                }

                Row {
                    spacing: root.gap
                    bottomPadding: root.gap

                    Repeater {
                        model: cardRow.modelData.count

                        Item {
                            id: card
                            required property int index
                            readonly property int row: cardRow.modelData.start + index
                            readonly property var info: {
                                root.folderModel.revision
                                return root.folderModel.get(row)
                            }
                            readonly property bool current: !info.isFolder && String(info.url) === String(root.currentUrl)
                            readonly property var status: info.isFolder ? null : root.badge(info.queueState)
                            readonly property bool queued: info.queueState === TrimJob.Idle
                                                           || info.queueState === TrimJob.Running
                            readonly property bool hovered: cardMouse.containsMouse
                            readonly property bool selected: root.selection[String(info.url)] === true
                            // A lone selection on the video being edited needs no extra mark.
                            readonly property bool showSelected: selected && (root.selectionCount > 1 || !current)
                            readonly property bool renaming: root.renamingUrl !== "" && root.renamingUrl === String(info.url)

                            width: root.cardWidth
                            // Folders are a compact row; videos a thumbnail card.
                            height: info.isFolder ? 46 : thumb.height + 44

                            ToolTip.visible: hovered && nameText.truncated && !renaming
                            ToolTip.delay: 600
                            ToolTip.text: info.name

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radius
                                color: card.showSelected ? Qt.rgba(0.18, 0.55, 1, card.hovered ? 0.3 : 0.22)
                                     : card.hovered ? Theme.hover : Theme.surfaceRaised
                            }

                            Item {
                                anchors.fill: parent
                                Rectangle {
                                    id: thumb
                                    visible: !card.info.isFolder
                                    width: parent.width
                                    height: visible ? Math.round(width * 9 / 16) : 0
                                    color: "#0b0b0d"
                                    radius: Theme.radius

                                    Image {
                                        anchors.fill: parent
                                        visible: !card.info.isFolder && status === Image.Ready
                                        source: card.info.isFolder ? "" : card.info.thumbnail
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        sourceSize.width: 320
                                        smooth: true
                                    }
                                    Icon {
                                        anchors.centerIn: parent
                                        visible: card.info.thumbnail === ""
                                        path: Icons.movie
                                        size: 26
                                        color: "#2c2c33"
                                    }
                                    // A finished trim is done with, so it steps back.
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: card.info.queueState === TrimJob.Finished
                                        color: Qt.rgba(0, 0, 0, 0.45)
                                    }

                                    // State tag.
                                    Rectangle {
                                        x: 6
                                        y: 6
                                        visible: card.status !== null || card.current
                                        width: tagRow.implicitWidth + 12
                                        height: 18
                                        radius: 4
                                        color: card.status ? card.status.color : Qt.rgba(0, 0, 0, 0.75)
                                        Row {
                                            id: tagRow
                                            anchors.centerIn: parent
                                            spacing: 4
                                            Icon {
                                                anchors.verticalCenter: parent.verticalCenter
                                                path: !card.status ? Icons.play
                                                      : card.info.queueState === TrimJob.Finished ? Icons.check : Icons.queueAdd
                                                size: 11
                                                color: card.status ? card.status.ink : Theme.text
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: !card.status ? "Editing"
                                                      : card.status.text + (card.info.cuts.length > 1 ? " · " + card.info.cuts.length + " cuts" : "")
                                                color: card.status ? card.status.ink : Theme.text
                                                font.pixelSize: 10
                                                font.weight: Font.Bold
                                            }
                                        }
                                    }

                                    // Duration.
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 6
                                        visible: !card.info.isFolder && card.info.duration >= 0
                                        width: durationText.implicitWidth + 8
                                        height: 16
                                        radius: 3
                                        color: Qt.rgba(0, 0, 0, 0.75)
                                        Text {
                                            id: durationText
                                            anchors.centerIn: parent
                                            text: root.formatDuration(card.info.duration)
                                            color: Theme.text
                                            font.family: Theme.mono
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    // Where the queued cut is in the video.
                                    Item {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 4
                                        visible: card.queued && card.info.cuts.length > 0
                                        Rectangle {
                                            anchors.fill: parent
                                            color: Qt.rgba(0, 0, 0, 0.55)
                                        }
                                        Repeater {
                                            model: card.info.cuts
                                            Rectangle {
                                                required property var modelData
                                                x: modelData.start * parent.width
                                                width: Math.max(2, (modelData.end - modelData.start) * parent.width)
                                                height: parent.height
                                                color: Theme.accent
                                            }
                                        }
                                    }
                                }

                                Icon {
                                    visible: card.info.isFolder
                                    x: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    path: Icons.folder
                                    size: 20
                                    color: Theme.textDim
                                }
                                Text {
                                    id: nameText
                                    anchors.top: thumb.bottom
                                    anchors.topMargin: card.info.isFolder ? 8 : 7
                                    x: card.info.isFolder ? 38 : 9
                                    width: parent.width - x - 9
                                    visible: !card.renaming
                                    text: card.info.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    anchors.top: nameText.bottom
                                    anchors.topMargin: 3
                                    x: nameText.x
                                    text: card.info.isFolder
                                          ? card.info.itemCount + (card.info.itemCount === 1 ? " video" : " videos")
                                          : root.byDate ? Qt.formatTime(card.info.modified, "h:mm AP")
                                                        : Qt.formatDate(card.info.modified, "MMM d, yyyy")
                                    color: Theme.textFaint
                                    font.pixelSize: 11
                                }
                                Text {
                                    anchors.top: nameText.bottom
                                    anchors.topMargin: 3
                                    anchors.right: parent.right
                                    anchors.rightMargin: 9
                                    visible: !card.info.isFolder
                                    text: root.formatSize(card.info.size)
                                    color: Theme.textFaint
                                    font.pixelSize: 11
                                }
                            }

                            // Selected cards get a tick in the corner.
                            Rectangle {
                                visible: card.showSelected
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 6
                                width: 18
                                height: 18
                                radius: 9
                                color: Theme.accent
                                border.color: "white"
                                border.width: 1.5
                                z: 2
                                Icon {
                                    anchors.centerIn: parent
                                    path: Icons.check
                                    size: 12
                                    color: "white"
                                }
                            }

                            // Queued and current cards are outlined; drawn on top
                            // so the outline covers the thumbnail's corners.
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radius
                                color: "transparent"
                                border.width: card.current || card.queued || card.showSelected ? 2 : 1
                                border.color: card.current ? Theme.text : card.queued || card.showSelected ? Theme.accent : Theme.border
                            }

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => root.cardClicked(card.info, mouse.button, mouse.modifiers)
                            }

                            // Renaming happens in place, over the name. A video's
                            // extension stays as it is.
                            Rectangle {
                                visible: card.renaming
                                x: nameText.x - 4
                                y: nameText.y - 4
                                width: parent.width - x - 5
                                height: 22
                                radius: 4
                                color: Theme.background
                                border.color: Theme.accent
                                z: 3

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 4
                                    spacing: 0
                                    TextInput {
                                        id: renameField
                                        Layout.fillWidth: true
                                        clip: true
                                        color: Theme.text
                                        selectionColor: Theme.accent
                                        selectedTextColor: "white"
                                        font.pixelSize: 12
                                        selectByMouse: true
                                        property bool done: false
                                        readonly property string fullName: text + card.extension

                                        function finish(commit) {
                                            if (done)
                                                return
                                            done = true
                                            if (commit && fullName !== card.info.name)
                                                root.renameRequested(card.info.url, fullName)
                                            root.renamingUrl = ""
                                        }
                                        onAccepted: {
                                            const error = root.folderModel.renameError(card.info.url, fullName)
                                            if (error !== "")
                                                root.showMessage(error)
                                            else
                                                finish(true)
                                        }
                                        Keys.onEscapePressed: finish(false)
                                        onActiveFocusChanged: if (!activeFocus && card.renaming) finish(root.folderModel.renameError(card.info.url, fullName) === "")
                                    }
                                    Text {
                                        text: card.extension
                                        color: Theme.textFaint
                                        font.pixelSize: 12
                                    }
                                }
                            }
                            readonly property string extension: {
                                if (info.isFolder)
                                    return ""
                                const dot = info.name.lastIndexOf(".")
                                return dot > 0 ? info.name.substring(dot) : ""
                            }
                            onRenamingChanged: {
                                if (!renaming)
                                    return
                                renameField.done = false
                                renameField.text = info.name.substring(0, info.name.length - extension.length)
                                renameField.selectAll()
                                renameField.forceActiveFocus()
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: grid.count === 0
                text: root.folderModel.search !== "" ? "Nothing here matches “" + root.folderModel.search + "”"
                      : root.folderModel.filter === VideoFolderModel.Queued ? "No videos from this folder are queued"
                      : root.folderModel.filter === VideoFolderModel.Trimmed ? "No videos from this folder are trimmed yet"
                      : "No videos in this folder"
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 7
            Icon {
                visible: !messageTimer.running
                path: Icons.pin
                size: 13
                color: Theme.textFaint
            }
            ToolTip.visible: footerHover.hovered
            ToolTip.text: messageTimer.running ? message.text : "This folder opens when mxCut starts, until you close it with ×."
            HoverHandler { id: footerHover }
            Text {
                Layout.fillWidth: true
                visible: !messageTimer.running
                text: root.selectionCount > 1 ? root.selectionCount + " selected  ·  right-click for actions"
                                              : "Reopens at next launch"
                color: Theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            // Results and problems from the file actions, for a few seconds.
            Text {
                id: message
                property bool isError: true
                Layout.fillWidth: true
                visible: messageTimer.running
                color: isError ? Theme.danger : Theme.success
                font.pixelSize: 11
                elide: Text.ElideRight
                Timer {
                    id: messageTimer
                    interval: 6000
                }
            }
        }
    }
}
