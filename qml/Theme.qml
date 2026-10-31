pragma Singleton
import QtQuick

// Colours, sizes and formatting shared by every view.
QtObject {
    readonly property color background: "#000000"
    readonly property color surface: "#111113"
    readonly property color surfaceRaised: "#1b1b1f"
    readonly property color hover: "#26262b"
    readonly property color border: "#2a2a30"
    readonly property color text: "#ececef"
    readonly property color textDim: "#a0a0a8"
    readonly property color textFaint: "#66666e"
    readonly property color accent: "#2d8cff"
    readonly property color success: "#3fb950"
    readonly property color warning: "#e3a52f"
    readonly property color danger: "#ff5c5c"
    readonly property color videoClip: "#24467e"
    readonly property color audioClip: "#0f3a24"
    readonly property color waveform: "#3ddc84"
    // One colour per segment of a cut, in the order they are added.
    readonly property var segmentColors: ["#2d8cff", "#2fbf71", "#ff8c32", "#f2c94c", "#a970ff"]
    function segmentColor(i) { return segmentColors[i % segmentColors.length] }
    // Text that reads on top of a segment colour.
    function onColor(c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b > 0.6 ? "#000000" : "#ffffff" }
    readonly property int radius: 6
    // The narrowest a thumbnail card in the file browser gets. The browser
    // is at least one card wide, and the export queue at least as wide.
    readonly property int thumbnailWidth: 160
    readonly property int panelMinWidth: thumbnailWidth + 24
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas"
                                  : Qt.platform.os === "osx" ? "Menlo" : "monospace"

    // Seconds as HH:MM:SS:FF when the frame rate is known, else HH:MM:SS.mmm.
    function timecode(t, fps) {
        if (!(t >= 0))
            t = 0
        const pad = (n, w) => String(n).padStart(w || 2, "0")
        const whole = Math.floor(t + 1e-6)
        const hms = pad(Math.floor(whole / 3600)) + ":" + pad(Math.floor(whole / 60) % 60) + ":" + pad(whole % 60)
        if (fps > 0) {
            const frame = Math.min(Math.round(fps) - 1, Math.floor((t - whole) * fps + 1e-6))
            return hms + ":" + pad(Math.max(0, frame))
        }
        return hms + "." + pad(Math.floor((t - whole) * 1000 + 1e-6), 3)
    }

    // Short form for ruler labels; steps under a second need the frame.
    function rulerLabel(t, step, fps) {
        if (step < 1)
            return timecode(t, fps)
        const pad = n => String(n).padStart(2, "0")
        const s = Math.floor(t + 1e-6)
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
    }
}
