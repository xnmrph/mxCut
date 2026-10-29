import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import MxCut

ApplicationWindow {
    id: window

    property url initialSource
    property real inPoint: 0
    property real outPoint: 0
    property int trimMode: TrimJob.Fast
    // Folder mode adds the file sidebar; the export queue is always there.
    property bool folderMode: false
    // The parts of the video to keep, as {inPoint, outPoint}; there is always
    // at least one. inPoint and outPoint are the active one's, which the keys,
    // buttons and handles change.
    property var segments: [{ inPoint: 0, outPoint: 0 }]
    property int activeSegment: 0
    readonly property int maxSegments: Theme.segmentColors.length
    readonly property real keptDuration: {
        let total = 0
        for (const s of segments)
            total += Math.max(0, s.outPoint - s.inPoint)
        return total
    }
    // Segments set on each video this session, keyed by URL, so moving
    // between videos in a folder doesn't lose them.
    property var cuts: ({})
    readonly property bool busy: job.running || exportQueue.running
    readonly property real minimumLength: 0.04

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: player.loaded ? fileName(player.source) + " - mxCut" : "mxCut"
    color: Theme.background

    Material.theme: Material.Dark
    Material.accent: Theme.accent
    Material.primary: Theme.accent
    Material.background: Theme.surfaceRaised
    Material.foreground: Theme.text
    // Material shouts button labels in capitals; keep them as written.
    font.capitalization: Font.MixedCase

    function fileName(url) {
        const s = decodeURIComponent(url.toString())
        return s.substring(s.lastIndexOf("/") + 1)
    }

    function timecode(t) { return Theme.timecode(t, player.fps) }

    // Opens a file or a folder, from the command line or a drop.
    function openPath(url) {
        if (folderModel.isFolder(url))
            openFolder(url)
        else
            openVideo(url)
    }

    function openVideo(url) {
        if (job.running)
            return
        folderMode = false
        job.reset()
        player.source = url
    }

    function openFolder(url) {
        folderModel.folder = url
        folderMode = true
        player.source = folderModel.count > 0 ? folderModel.urlAt(0) : ""
    }

    function selectVideo(url) {
        if (String(url) !== String(player.source))
            player.source = url
    }

    function stepVideo(delta) {
        const next = folderModel.indexOf(player.source) + delta
        if (next >= 0 && next < folderModel.count)
            player.source = folderModel.urlAt(next)
    }

    // This video is about to be deleted after its trim, so close it if it is
    // showing; reopenFile shows the trim, or the original if it was kept.
    property url releasedSource
    function releaseFile(source) {
        if (String(player.source) !== String(source))
            return
        releasedSource = source
        player.source = ""
    }
    // Trims whose original was deleted, so the original's cut doesn't fit them.
    property var replacedFiles: ({})
    function reopenFile(source, current, deleted) {
        if (deleted) {
            delete cuts[String(source)]
            replacedFiles[String(current)] = true
        }
        if (String(releasedSource) !== String(source))
            return
        releasedSource = ""
        player.source = current
    }

    function rememberCut() {
        if (player.loaded && player.duration > 0)
            cuts[String(player.source)] = { segments: segments, active: activeSegment }
    }

    // Keep the active segment in step with inPoint and outPoint.
    property bool loadingSegment: false
    onInPointChanged: storeActiveSegment()
    onOutPointChanged: storeActiveSegment()
    function storeActiveSegment() {
        if (loadingSegment || !segments[activeSegment])
            return
        const s = segments.slice()
        s[activeSegment] = { inPoint: inPoint, outPoint: outPoint }
        segments = s
    }
    function loadSegments(list, active) {
        loadingSegment = true
        segments = list
        activeSegment = Math.max(0, Math.min(active, list.length - 1))
        inPoint = list[activeSegment].inPoint
        outPoint = list[activeSegment].outPoint
        loadingSegment = false
    }
    function selectSegment(i) {
        if (i >= 0 && i < segments.length && i !== activeSegment)
            loadSegments(segments, i)
        rememberCut()
    }
    // A new segment starts at the playhead and runs to the end, like the first.
    function addSegment() {
        if (!player.loaded || player.duration <= 0 || segments.length >= maxSegments)
            return
        const start = Math.min(player.position, player.duration - minimumLength)
        loadSegments(segments.concat([{ inPoint: start, outPoint: player.duration }]), segments.length)
        rememberCut()
    }
    function removeSegment(i) {
        if (segments.length <= 1)
            return
        const s = segments.slice()
        s.splice(i, 1)
        loadSegments(s, activeSegment > i ? activeSegment - 1 : Math.min(activeSegment, s.length - 1))
        rememberCut()
    }
    // What the trim keeps: nothing extra for one segment, all of them for more.
    function jobSegments() { return segments.length > 1 ? segments : [] }

    function setIn() { inPoint = Math.min(player.position, outPoint - minimumLength); rememberCut() }
    function setOut() { outPoint = Math.max(player.position, inPoint + minimumLength); rememberCut() }

    function nudge(delta) {
        player.seek(Math.max(0, Math.min(player.duration, player.position + delta)), true)
    }

    function addToQueue() {
        if (!player.loaded)
            return
        rememberCut()
        exportQueue.add(player.source, inPoint, outPoint, trimMode, jobSegments())
        if (folderMode && autoAdvance.checked)
            stepVideo(1)
    }

    function trimNow() {
        if (job.running) {
            job.cancel()
            return
        }
        job.source = player.source
        job.inPoint = inPoint
        job.outPoint = outPoint
        job.segments = jobSegments()
        job.mode = trimMode
        job.output = ""
        job.applySettings(appSettings)
        job.start()
    }

    Component.onCompleted: if (initialSource.toString() !== "") openPath(initialSource)

    Connections {
        target: player
        function onDurationChanged() {
            if (player.duration <= 0)
                return
            // Restore this video's cut: what was set here, else what's queued.
            const fresh = window.replacedFiles[String(player.source)] === true
            delete window.replacedFiles[String(player.source)]
            const saved = fresh ? undefined : window.cuts[String(player.source)]
            const queued = fresh ? null : exportQueue.jobFor(player.source)
            if (saved) {
                window.loadSegments(saved.segments, saved.active)
            } else if (queued) {
                const parts = queued.segments
                window.loadSegments(parts.length > 1 ? parts : [{ inPoint: queued.inPoint, outPoint: queued.outPoint }],
                                    parts.length - 1)
            } else {
                window.loadSegments([{ inPoint: 0, outPoint: player.duration }], 0)
            }
            if (saved || queued)
                player.seek(window.inPoint, true)
        }
    }

    FileDialog {
        id: openDialog
        title: "Open video"
        nameFilters: ["Videos (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts *.mts *.m2ts *.flv *.wmv *.mpg *.mpeg *.3gp *.ogv)", "All files (*)"]
        onAccepted: window.openVideo(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Open folder"
        onAccepted: window.openFolder(selectedFolder)
    }

    Settings {
        id: appSettings
        onFfmpegPathChanged: encoderProbe.probe()
    }

    EncoderProbe {
        id: encoderProbe
        Component.onCompleted: encoderProbe.probe()
    }

    TrimJob {
        id: job
        onAboutToDeleteOriginal: (source) => window.releaseFile(source)
        onDeleteOriginalFinished: (source, current, deleted) => window.reopenFile(source, current, deleted)
    }

    ExportQueue {
        id: exportQueue
        settings: appSettings
        maxConcurrent: appSettings.maxConcurrent
        onAboutToDeleteOriginal: (source) => window.releaseFile(source)
        onDeleteOriginalFinished: (source, current, deleted) => window.reopenFile(source, current, deleted)
    }

    MediaPreview {
        id: preview
        source: player.loaded ? player.source : ""
        duration: player.duration
    }

    SettingsDialog {
        id: settingsDialog
        settings: appSettings
        probe: encoderProbe
    }

    VideoFolderModel {
        id: folderModel
        queue: exportQueue
    }

    Shortcut { sequence: "Space"; onActivated: player.togglePause() }
    Shortcut { sequence: appSettings.inKey; onActivated: window.setIn() }
    Shortcut { sequence: appSettings.outKey; onActivated: window.setOut() }
    Shortcut { sequence: "Left"; onActivated: player.frameBackStep() }
    Shortcut { sequence: "Right"; onActivated: player.frameStep() }
    Shortcut { sequence: "Shift+Left"; onActivated: window.nudge(-5) }
    Shortcut { sequence: "Shift+Right"; onActivated: window.nudge(5) }
    Shortcut { sequence: "["; onActivated: player.seek(window.inPoint, true) }
    Shortcut { sequence: "]"; onActivated: player.seek(window.outPoint, true) }
    Shortcut { sequence: "Q"; onActivated: window.addToQueue() }
    Shortcut { sequence: "S"; onActivated: window.addSegment() }
    Shortcut { sequence: "Up"; enabled: window.folderMode; onActivated: window.stepVideo(-1) }
    Shortcut { sequence: "Down"; enabled: window.folderMode; onActivated: window.stepVideo(1) }
    Shortcut { sequence: StandardKey.Open; enabled: !window.busy; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: !window.busy; onActivated: folderDialog.open() }
    Shortcut { sequence: StandardKey.Preferences; onActivated: settingsDialog.open() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }

    header: Rectangle {
        implicitHeight: 48
        color: "#0b0b0d"

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            spacing: 6

            Image {
                source: "qrc:/packaging/branding/logo.png"
                sourceSize.height: 48
                Layout.preferredHeight: 24
                Layout.preferredWidth: 24 * implicitWidth / implicitHeight
                Layout.rightMargin: 18
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            IconButton {
                text: "Open video"
                iconPath: Icons.movie
                tip: "Open one video (Ctrl+O)"
                enabled: !window.busy
                onClicked: openDialog.open()
            }
            IconButton {
                text: "Open folder"
                iconPath: Icons.folder
                tip: "Trim a folder of videos as a batch (Ctrl+Shift+O)"
                enabled: !window.busy
                onClicked: folderDialog.open()
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: player.loaded ? window.fileName(player.source) : ""
                color: Theme.textDim
                font.pixelSize: 13
            }
            IconButton {
                iconPath: Icons.keyboard
                tip: "Space  play / pause\n" + appSettings.inKey + " / " + appSettings.outKey + "  set in / out\n"
                     + "← / →  previous / next frame\n"
                     + "Shift + ← / →  jump 5 seconds\n[ / ]  go to in / out\n"
                     + "S  add a segment\nQ  add to queue\n↑ / ↓  previous / next video (folder)\nCtrl + ,  settings"
            }
            IconButton {
                iconPath: Icons.settings
                tip: "Settings (Ctrl+,)"
                onClicked: settingsDialog.open()
            }
        }
    }

    // FFmpeg is needed for every trim, so say so up front when it's missing.
    Rectangle {
        id: ffmpegBanner
        z: 10
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? 40 : 0
        visible: !encoderProbe.probing && !encoderProbe.ffmpegFound
        color: "#3a1517"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            Text {
                Layout.fillWidth: true
                text: appSettings.ffmpegPath !== ""
                      ? "The FFmpeg program chosen in Settings doesn't work. Trimming won't work until it's fixed."
                      : "FFmpeg wasn't found, so trimming won't work. Install FFmpeg, or choose where it is in Settings."
                color: "#ffb4b4"
                elide: Text.ElideRight
                font.pixelSize: 13
            }
            IconButton {
                text: "Settings"
                onClicked: settingsDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: ffmpegBanner.height
        spacing: 0

        FileSidebar {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            visible: window.folderMode
            folderModel: folderModel
            currentUrl: player.source
            onVideoSelected: (url) => window.selectVideo(url)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Program monitor
            Item {
                id: monitor
                Layout.fillWidth: true
                Layout.fillHeight: true

                Item {
                    id: picture
                    anchors.fill: parent
                    anchors.margins: 18

                    MpvItem {
                        id: player
                        anchors.fill: parent
                    }

                    // Outline around the picture itself, so black bars inside
                    // the video stand out from the empty space around it.
                    Rectangle {
                        readonly property real fit: player.videoWidth > 0 && player.videoHeight > 0
                            ? Math.min(picture.width / player.videoWidth, picture.height / player.videoHeight) : 0
                        readonly property real w: player.videoWidth * fit
                        readonly property real h: player.videoHeight * fit
                        visible: player.loaded && fit > 0 && outlineButton.checked
                        x: Math.floor((picture.width - w) / 2) - 1
                        y: Math.floor((picture.height - h) / 2) - 1
                        width: Math.ceil(w) + 2
                        height: Math.ceil(h) + 2
                        color: "transparent"
                        border.color: "white"
                        border.width: 1
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: player.loaded
                    onClicked: player.togglePause()
                }

                Column {
                    anchors.centerIn: parent
                    visible: !player.loaded
                    spacing: 14

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        path: Icons.movie
                        size: 48
                        color: Theme.textFaint
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: player.errorString !== "" ? "Could not open this file: " + player.errorString
                            : window.folderMode && folderModel.count === 0 ? "This folder has no videos"
                            : "Open a video or a folder, or drop one here"
                        color: Theme.textDim
                        font.pixelSize: 16
                    }
                }

                DropArea {
                    anchors.fill: parent
                    enabled: !window.busy
                    onDropped: (drop) => { if (drop.hasUrls) window.openPath(drop.urls[0]) }
                }
            }

            // Transport bar, as under a program monitor.
            Item {
                Layout.fillWidth: true
                implicitHeight: 48

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.timecode(timeline.shownPosition)
                    color: Theme.accent
                    font.family: Theme.mono
                    font.pixelSize: 18
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    enabled: player.loaded

                    IconButton { iconPath: Icons.markIn; tip: "Set in point (" + appSettings.inKey + ")"; onClicked: window.setIn() }
                    IconButton { iconPath: Icons.markOut; tip: "Set out point (" + appSettings.outKey + ")"; onClicked: window.setOut() }
                    IconButton {
                        text: "+"
                        font.pixelSize: 18
                        implicitWidth: 34
                        enabled: window.segments.length < window.maxSegments
                        tip: window.segments.length < window.maxSegments
                             ? "Add a segment (S): keep another part of the video, joined after the others"
                             : "Up to " + window.maxSegments + " segments"
                        onClicked: window.addSegment()
                    }
                    Item { width: 12; height: 1 }
                    IconButton { iconPath: Icons.goToIn; tip: "Go to in point ([)"; onClicked: player.seek(window.inPoint, true) }
                    IconButton { iconPath: Icons.stepBack; tip: "Previous frame (←)"; onClicked: player.frameBackStep() }
                    IconButton {
                        iconPath: player.paused ? Icons.play : Icons.pause
                        iconSize: 24
                        implicitWidth: 44
                        tip: "Play / pause (Space)"
                        onClicked: player.togglePause()
                    }
                    IconButton { iconPath: Icons.stepForward; tip: "Next frame (→)"; onClicked: player.frameStep() }
                    IconButton { iconPath: Icons.goToOut; tip: "Go to out point (])"; onClicked: player.seek(window.outPoint, true) }
                    Item { width: 12; height: 1 }
                    IconButton {
                        id: outlineButton
                        iconPath: Icons.frame
                        checkable: true
                        checked: true
                        tip: "Outline the picture, to spot black bars"
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.timecode(player.duration)
                    color: Theme.textDim
                    font.family: Theme.mono
                    font.pixelSize: 14
                }
            }

            Timeline {
                id: timeline
                Layout.fillWidth: true
                duration: player.duration
                position: player.position
                segments: window.segments
                activeSegment: window.activeSegment
                fps: player.fps
                clipName: player.loaded ? window.fileName(player.source) : ""
                thumbnails: preview.thumbnails
                peaks: preview.peaks
                aspect: player.videoHeight > 0 ? player.videoWidth / player.videoHeight : 16 / 9
                minimumLength: window.minimumLength
                onSeekRequested: (time, exact) => player.seek(time, exact)
                onSegmentSelected: (index) => window.selectSegment(index)
                onSegmentMoved: (index, inPoint, outPoint) => {
                    window.selectSegment(index)
                    window.inPoint = inPoint
                    window.outPoint = outPoint
                    window.rememberCut()
                }
            }

            // Action bar
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 60
                color: Theme.surface

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.border
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 14
                    enabled: player.loaded

                    component Readout: Column {
                        property string label
                        property string value
                        property color labelColor: Theme.textFaint
                        property color valueColor: Theme.text
                        spacing: 1
                        Text { text: parent.label; color: parent.labelColor; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                        Text { text: parent.value; color: parent.valueColor; font.family: Theme.mono; font.pixelSize: 14 }
                    }

                    Readout {
                        label: "DURATION"
                        value: window.timecode(window.keptDuration)
                        valueColor: Theme.accent
                        ToolTip.visible: durationHover.hovered && window.segments.length > 1
                        ToolTip.text: "All segments together"
                        HoverHandler { id: durationHover }
                    }

                    // One in/out pair per segment; with several, each sits in a
                    // box in its colour, outlined when it is the active one.
                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: segmentRow.width
                        contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 4 }

                        Row {
                            id: segmentRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Repeater {
                                model: window.segments
                                delegate: AbstractButton {
                                    id: segmentBox
                                    required property var modelData
                                    required property int index
                                    readonly property bool several: window.segments.length > 1
                                    readonly property bool active: index === window.activeSegment
                                    readonly property color tint: several ? Theme.segmentColor(index) : Theme.textFaint
                                    readonly property string suffix: index > 0 ? " " + (index + 1) : ""

                                    implicitWidth: several ? compact.implicitWidth + 24 + removeButton.width
                                                           : pair.implicitWidth
                                    implicitHeight: 44
                                    hoverEnabled: several
                                    onClicked: window.selectSegment(index)

                                    background: Rectangle {
                                        visible: segmentBox.several
                                        radius: Theme.radius + 2
                                        color: segmentBox.hovered && !segmentBox.active ? Theme.hover : "transparent"
                                        border.width: segmentBox.active ? 2 : 1
                                        border.color: segmentBox.active ? segmentBox.tint : Theme.border
                                    }
                                    contentItem: Item {
                                        // Several segments: in and out stacked, to fit more in a row.
                                        Grid {
                                            id: compact
                                            visible: segmentBox.several
                                            x: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            columns: 2
                                            columnSpacing: 8
                                            rowSpacing: 1
                                            verticalItemAlignment: Grid.AlignVCenter
                                            Text { text: "IN" + segmentBox.suffix; color: segmentBox.tint; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                                            Text { text: window.timecode(segmentBox.modelData.inPoint); color: Theme.text; font.family: Theme.mono; font.pixelSize: 13 }
                                            Text { text: "OUT" + segmentBox.suffix; color: segmentBox.tint; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                                            Text { text: window.timecode(segmentBox.modelData.outPoint); color: Theme.text; font.family: Theme.mono; font.pixelSize: 13 }
                                        }
                                        Row {
                                            id: pair
                                            visible: !segmentBox.several
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 14
                                            Readout {
                                                label: "IN" + segmentBox.suffix
                                                labelColor: segmentBox.tint
                                                value: window.timecode(segmentBox.modelData.inPoint)
                                            }
                                            Readout {
                                                label: "OUT" + segmentBox.suffix
                                                labelColor: segmentBox.tint
                                                value: window.timecode(segmentBox.modelData.outPoint)
                                            }
                                        }
                                        IconButton {
                                            id: removeButton
                                            visible: segmentBox.several
                                            anchors.right: parent.right
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            iconPath: Icons.close
                                            iconSize: 12
                                            implicitWidth: 20
                                            implicitHeight: 20
                                            tint: Theme.textDim
                                            tip: "Remove this segment"
                                            onClicked: window.removeSegment(segmentBox.index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Segmented Fast / Accurate switch.
                    Rectangle {
                        implicitWidth: modes.implicitWidth + 6
                        implicitHeight: 34
                        radius: Theme.radius + 2
                        color: Theme.background
                        border.color: Theme.border

                        Row {
                            id: modes
                            anchors.centerIn: parent
                            spacing: 2
                            Repeater {
                                model: [
                                    { mode: TrimJob.Fast, text: "Fast",
                                      tip: "Copies the streams without re-encoding. Very quick and lossless, but the start snaps to the nearest earlier keyframe." },
                                    { mode: TrimJob.Accurate, text: "Accurate",
                                      tip: "Re-encodes so the cut lands on the exact frame. Slower. The encoder and quality are set in Settings." }
                                ]
                                delegate: IconButton {
                                    required property var modelData
                                    text: modelData.text
                                    tip: modelData.tip
                                    implicitHeight: 28
                                    implicitWidth: 80
                                    enabled: !job.running
                                    primary: window.trimMode === modelData.mode
                                    onClicked: window.trimMode = modelData.mode
                                }
                            }
                        }
                    }

                    // Single-video mode: trim right away, with the result shown here.
                    ColumnLayout {
                        visible: !window.folderMode && job.state !== TrimJob.Idle
                        Layout.maximumWidth: 260
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.pixelSize: 12
                            color: job.state === TrimJob.Failed ? Theme.danger
                                 : job.state === TrimJob.Finished ? Theme.success : Theme.textDim
                            text: {
                                switch (job.state) {
                                case TrimJob.Running: return "Trimming… " + Math.round(job.progress * 100) + "%"
                                case TrimJob.Finished: return "Saved " + job.output.substring(Math.max(job.output.lastIndexOf("/"), job.output.lastIndexOf("\\")) + 1)
                                case TrimJob.Failed: return "Trim failed: " + job.errorString
                                case TrimJob.Cancelled: return "Trim cancelled"
                                }
                                return ""
                            }
                            ToolTip.visible: statusHover.hovered && (job.state === TrimJob.Failed ? job.errorDetails !== "" : job.state === TrimJob.Finished)
                            ToolTip.text: job.state === TrimJob.Failed ? job.errorDetails : job.output
                            HoverHandler { id: statusHover }
                        }
                        Rectangle {
                            visible: job.running
                            Layout.fillWidth: true
                            implicitHeight: 3
                            radius: 2
                            color: Theme.hover
                            Rectangle {
                                width: parent.width * job.progress
                                height: parent.height
                                radius: 2
                                color: Theme.accent
                            }
                        }
                    }
                    IconButton {
                        text: "Show in folder"
                        iconPath: Icons.openFolder
                        visible: !window.folderMode && job.state === TrimJob.Finished
                        onClicked: Qt.openUrlExternally(job.outputFolderUrl())
                    }
                    IconButton {
                        visible: !window.folderMode
                        primary: !job.running
                        iconPath: job.running ? Icons.close : Icons.cut
                        text: job.running ? "Cancel" : "Trim"
                        implicitHeight: 36
                        implicitWidth: 110
                        onClicked: window.trimNow()
                    }

                    // Queue this video's cut and, in folder mode, move on.
                    CheckBox {
                        id: autoAdvance
                        visible: window.folderMode
                        checked: true
                        text: "Then next"
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: "Go to the next video after adding this one"
                    }
                    IconButton {
                        primary: window.folderMode
                        iconPath: Icons.queueAdd
                        implicitHeight: 36
                        tip: "Q"
                        text: {
                            const queued = exportQueue.jobFor(player.source)
                            return queued && queued.state === TrimJob.Idle ? "Update queue" : "Add to queue"
                        }
                        onClicked: window.addToQueue()
                    }
                }
            }
        }

        QueuePanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            queue: exportQueue
            settings: appSettings
            formatTime: window.timecode
            onJobActivated: (source) => window.selectVideo(source)
        }
    }
}
