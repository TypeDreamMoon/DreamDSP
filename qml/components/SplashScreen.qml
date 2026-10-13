pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import HuskarUI.Basic

// The intro: the D wipes in, the level bars rise, the star twinkles, the
// wordmark settles and the character floats in beside it, then the whole thing
// fades out onto the application.
//
// Always dark and independent of the theme. It is a logo, and it starts before
// Main.qml has applied the theme.
//
// Every frame is a pure function of `time` (seconds), not a pile of independent
// animations: that is what lets a screenshot be taken at any instant, and what
// keeps the pieces from drifting apart when the first frames are late because
// the main window was still being built under it.
Item {
    id: root

    // Emitted once, when the intro has faded out completely.
    signal finished()

    // Seconds into the intro. Driven by the animation at the bottom; a test may
    // set it directly (with `autoplay: false`).
    property real time: 0
    property bool autoplay: true

    readonly property real fadeStart: 2.1
    readonly property real fadeEnd: 2.5

    // Set by a click or key: fade out from wherever the intro had got to.
    property real skip: 0

    // The mark is authored on a 256 grid (assets/logo.svg). Everything inside
    // `mark` is that geometry, scaled as a whole.
    readonly property real markSide: Math.min(220, Math.min(width, height) * 0.4)

    readonly property color blue: '#3d7eff'

    // Logo and wordmark on the left, the character on the right.
    readonly property real markCx: width * 0.31
    readonly property real charH: Math.min(height * 0.94, 720)
    readonly property real charCx: width * 0.69

    function clamp01(x) { return Math.max(0, Math.min(1, x)); }
    // 0..1 progress of `time` through [from, to].
    function seg(from, to) { return clamp01((time - from) / (to - from)); }
    function outCubic(x) { return 1 - Math.pow(1 - x, 3); }
    function outBack(x) {
        const c1 = 1.70158, c3 = c1 + 1;
        return 1 + c3 * Math.pow(x - 1, 3) + c1 * Math.pow(x - 1, 2);
    }

    readonly property real fade: Math.max(seg(fadeStart, fadeEnd), skip)

    opacity: 1 - fade
    focus: true

    function leave() {
        if (skip > 0 || time >= fadeEnd) return;
        clock.stop();
        skipFade.from = 0;
        skipFade.start();
    }

    Keys.onPressed: (event) => { leave(); event.accepted = true; }

    MouseArea {
        anchors.fill: parent
        onClicked: root.leave()
    }

    // ------------------------------------------------------------ backdrop
    Rectangle {
        anchors.fill: parent
        color: '#080b17'
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: 'transparent'
            fillGradient: RadialGradient {
                centerX: root.width * 0.5
                centerY: root.height / 2
                centerRadius: Math.max(root.width, root.height) * 0.55
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0.0; color: Qt.rgba(0.24, 0.49, 1.0, 0.20 * root.outCubic(root.seg(0.0, 0.8))) }
                GradientStop { position: 1.0; color: Qt.rgba(0.24, 0.49, 1.0, 0.0) }
            }
            PathSvg { path: `M0 0 H${root.width} V${root.height} H0 Z` }
        }
    }

    // Sound rings spreading out from the mark, once the D is down.
    Repeater {
        model: 2
        Rectangle {
            id: ring
            required property int index
            readonly property real s: root.seg(0.95 + 0.25 * index, 2.0 + 0.25 * index)
            anchors.centerIn: mark
            width: root.markSide * (0.9 + 1.5 * root.outCubic(s))
            height: width
            radius: width / 2
            color: 'transparent'
            border.width: 2
            border.color: Qt.rgba(0.24, 0.49, 1.0, 0.35 * (1 - s))
            visible: s > 0 && s < 1
        }
    }

    // ---------------------------------------------------------------- mark
    Item {
        id: mark
        width: 256
        height: 256
        x: root.markCx - width / 2
        y: (root.height - height) / 2 - 40
        transformOrigin: Item.Center
        scale: (root.markSide / 256) * (0.82 + 0.18 * root.outBack(root.seg(0.0, 0.55)))
        opacity: root.outCubic(root.seg(0.0, 0.35))

        // The tile, its hairline, and the glow behind the D.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Qt.rgba(0.435, 0.608, 1.0, 0.28)
                strokeWidth: 2
                fillGradient: LinearGradient {
                    x1: 8; y1: 8; x2: 248; y2: 248
                    GradientStop { position: 0; color: '#1b2750' }
                    GradientStop { position: 1; color: '#080b17' }
                }
                PathSvg {
                    path: 'M66 8 H190 A58 58 0 0 1 248 66 V190 A58 58 0 0 1 190 248 H66 '
                        + 'A58 58 0 0 1 8 190 V66 A58 58 0 0 1 66 8 Z'
                }
            }
            ShapePath {
                strokeColor: 'transparent'
                fillGradient: RadialGradient {
                    centerX: 129; centerY: 128; centerRadius: 96
                    focalX: 129; focalY: 128
                    GradientStop { position: 0; color: Qt.rgba(0.24, 0.49, 1.0, 0.38) }
                    GradientStop { position: 1; color: Qt.rgba(0.24, 0.49, 1.0, 0.0) }
                }
                PathSvg { path: 'M33 128 A96 96 0 1 0 225 128 A96 96 0 1 0 33 128 Z' }
            }
        }

        // The D, as a filled outline (a stroke cannot take a gradient in
        // Qt Quick), uncovered left to right so the stem lands first.
        Item {
            width: 256 * root.outCubic(root.seg(0.30, 0.95))
            height: 256
            clip: true

            Shape {
                width: 256
                height: 256
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: 'transparent'
                    fillRule: ShapePath.OddEvenFill
                    fillGradient: LinearGradient {
                        x1: 71; y1: 59; x2: 187; y2: 197
                        GradientStop { position: 0; color: '#9cc0ff' }
                        GradientStop { position: 1; color: '#3d7eff' }
                    }
                    PathSvg {
                        path: 'M82 59 H118 A69 69 0 0 1 118 197 H82 A11 11 0 0 1 71 186 V70 A11 11 0 0 1 82 59 Z '
                            + 'M93 81 H118 A47 47 0 0 1 118 175 H93 Z'
                    }
                }
            }
        }

        // Three level bars, rising from their middles with a little overshoot,
        // then breathing for the rest of the intro.
        Repeater {
            model: [
                { x: 104, h: 40 },
                { x: 124, h: 80 },
                { x: 144, h: 48 },
            ]
            Rectangle {
                id: bar
                required property int index
                required property var modelData
                readonly property real rise: root.outBack(root.seg(0.75 + 0.09 * index, 1.25 + 0.09 * index))
                readonly property real breathe: 1 + 0.16 * Math.sin((root.time - 1.4) * 9 + index * 1.7)
                                                   * root.seg(1.3, 1.6)
                x: modelData.x
                width: 12
                height: Math.max(12, modelData.h * rise * breathe)
                y: 128 - height / 2
                radius: 6
                visible: root.time > 0.75
                gradient: Gradient {
                    GradientStop { position: 0; color: '#ffffff' }
                    GradientStop { position: 1; color: '#8db4ff' }
                }
            }
        }

        // The star: spins in, then twinkles.
        Item {
            x: 206
            y: 59
            readonly property real s: root.seg(0.95, 1.35)
            scale: root.outBack(s) * (1 + 0.14 * Math.sin((root.time - 1.35) * 10) * root.seg(1.35, 1.5))
            rotation: 90 * (1 - root.outCubic(s))
            visible: s > 0

            Shape {
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: 'transparent'
                    fillColor: '#cfe0ff'
                    PathSvg { path: 'M0 -21 L5 -5 L21 0 L5 5 L0 21 L-5 5 L-21 0 L-5 -5 Z' }
                }
            }
        }
    }

    // -------------------------------------------------------- character
    // The glow behind her, then her: slides in from the right, settles, and
    // bobs for the rest of the intro.
    Image {
        id: oc
        readonly property real s: root.outCubic(root.seg(0.35, 1.05))
        readonly property real bob: 6 * Math.sin(root.time * 2.4) * root.seg(1.05, 1.4)
        source: 'qrc:/DreamDSP/Images/character.png'
        height: root.charH
        width: height * implicitWidth / Math.max(1, implicitHeight)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        x: root.charCx - width / 2 + 70 * (1 - s)
        y: (root.height - height) / 2 + bob
        opacity: s
        visible: s > 0
    }

    // --------------------------------------------------------- wordmark
    Column {
        x: root.markCx - width / 2
        y: mark.y + mark.height / 2 + root.markSide / 2 + 28
        spacing: 8

        HusText {
            id: word
            readonly property real s: root.outCubic(root.seg(1.0, 1.55))
            anchors.horizontalCenter: parent.horizontalCenter
            textFormat: Text.StyledText
            text: '<font color="#e8efff">Dream</font><font color="#5b93ff">DSP</font>'
            font.pixelSize: 34
            font.weight: Font.DemiBold
            font.letterSpacing: 10 - 8 * s
            opacity: s
            transform: Translate { y: 14 * (1 - word.s) }
        }

        HusText {
            readonly property real s: root.outCubic(root.seg(1.25, 1.75))
            anchors.horizontalCenter: parent.horizontalCenter
            text: '系统级音频处理  ·  v' + Qt.application.version
            font.pixelSize: 12
            font.letterSpacing: 1
            color: '#9db4e6'
            opacity: 0.6 * s
        }
    }

    // --------------------------------------------------------------- clock
    NumberAnimation {
        id: clock
        target: root
        property: 'time'
        from: 0
        to: root.fadeEnd
        duration: root.fadeEnd * 1000
        running: root.autoplay
        onFinished: root.finished()
    }

    // A click or key mid-intro: finish the fade from where it stands, quickly.
    NumberAnimation {
        id: skipFade
        target: root
        property: 'skip'
        to: 1
        duration: 220
        onFinished: root.finished()
    }
}
