import QtQuick

// Head-fixed coordinates: positive rotation is clockwise from the screen.
// The right pane projects the same speakers onto the front/back-height plane.
Item {
    id: root
    property real spread: 60
    property real azimuth: 0
    property real elevation: 0
    property color accent: '#3d7eff'
    property color foreground: '#dddddd'
    property color guide: '#555555'
    implicitHeight: 204

    Row {
        anchors.fill: parent
        spacing: 12
        Repeater {
            model: 2
            Canvas {
                id: view
                required property int index
                width: (root.width - 12) / 2
                height: root.height
                readonly property real spread: root.spread
                readonly property real azimuth: root.azimuth
                readonly property real elevation: root.elevation
                readonly property color accent: root.accent
                readonly property color foreground: root.foreground
                readonly property color guide: root.guide
                onSpreadChanged: requestPaint()
                onAzimuthChanged: requestPaint()
                onElevationChanged: requestPaint()
                onAccentChanged: requestPaint()
                onForegroundChanged: requestPaint()
                onGuideChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onVisibleChanged: if (visible) requestPaint()

                onPaint: {
                    const c = getContext('2d')
                    c.reset()
                    const cx = width / 2, cy = 102
                    const radius = Math.min(57, width / 2 - 19)
                    const rad = Math.PI / 180
                    const elevationRad = elevation * rad
                    c.font = '11px sans-serif'; c.textAlign = 'center'
                    c.fillStyle = foreground
                    c.fillText(index === 0 ? '俯视 · 环绕方位' : '侧视 · 上下高度', cx, 12)
                    c.strokeStyle = guide; c.lineWidth = 1
                    c.beginPath(); c.arc(cx, cy, radius, 0, Math.PI * 2); c.stroke()
                    c.beginPath(); c.moveTo(cx - radius - 5, cy)
                    c.lineTo(cx + radius + 5, cy); c.stroke()

                    // Screen stays in front of the listener in both views.
                    if (index === 0) {
                        c.strokeRect(cx - 18, 25, 36, 4)
                    } else {
                        c.strokeRect(width - 9, cy - 16, 4, 32)
                        c.fillStyle = guide
                        c.fillText('后', 8, cy + 21)
                        c.fillText('前', width - 9, cy + 31)
                    }

                    const points = []
                    for (let source = 0; source < 2; ++source) {
                        const angle = (azimuth + (source === 0 ? -spread / 2 : spread / 2)) * rad
                        const x = index === 0
                            ? cx + radius * Math.sin(angle)
                            : cx + radius * Math.cos(elevationRad) * Math.cos(angle)
                        const y = index === 0
                            ? cy - radius * Math.cos(angle)
                            : cy - radius * Math.sin(elevationRad)
                        points.push({ x: x, y: y })
                        c.strokeStyle = accent; c.globalAlpha = 0.55
                        c.beginPath(); c.moveTo(cx, cy); c.lineTo(x, y); c.stroke()
                        if (index === 1) {
                            c.strokeStyle = guide
                            c.beginPath(); c.moveTo(x, y); c.lineTo(x, cy); c.stroke()
                        }
                        c.globalAlpha = 1
                    }

                    // A fixed nose and ears make the listening direction clear.
                    c.fillStyle = foreground; c.strokeStyle = foreground; c.lineWidth = 1.4
                    c.beginPath(); c.arc(cx, cy, 12, 0, Math.PI * 2); c.stroke()
                    c.beginPath()
                    if (index === 0) {
                        c.moveTo(cx - 4, cy - 11); c.lineTo(cx, cy - 18); c.lineTo(cx + 4, cy - 11)
                        c.moveTo(cx - 14, cy - 5); c.lineTo(cx - 14, cy + 5)
                        c.moveTo(cx + 14, cy - 5); c.lineTo(cx + 14, cy + 5)
                    } else {
                        c.moveTo(cx + 11, cy - 4); c.lineTo(cx + 18, cy); c.lineTo(cx + 11, cy + 4)
                    }
                    c.stroke()

                    const overlap = Math.abs(points[0].x - points[1].x) < 15
                                    && Math.abs(points[0].y - points[1].y) < 15
                    for (let source = 0; source < (overlap ? 1 : 2); ++source) {
                        const p = points[source]
                        c.fillStyle = accent; c.fillRect(p.x - 5, p.y - 6, 10, 12)
                        c.fillStyle = foreground
                        c.fillText(overlap ? 'L / R' : (source === 0 ? 'L' : 'R'), p.x, p.y - 12)
                    }
                    c.fillStyle = foreground
                    c.fillText(index === 0 ? '头朝屏幕 · 音箱绕头转' : '0° 与耳同高 · 正值向上', cx, 188)
                }
            }
        }
    }
}
