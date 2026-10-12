import QtQuick

// Cards in columns, each one dropped into whichever column is currently
// shortest.
//
// The pages used a GridLayout before, and a grid aligns its rows: every card in
// a row is given the height of the tallest one beside it, so a four-line card
// next to a twelve-line one leaves eight lines of nothing under it, and a card
// that spans two columns leaves the third empty for its whole height. With
// cards as uneven as these -- a single slider next to a transfer curve and six
// rows -- no column count and no ordering removes the holes, because the holes
// are what row alignment is. This has no rows, so it has no holes.
//
// Children are positioned by hand. A child may declare
//     property bool masonryFull: true
// to take the whole width, after everything placed so far; banners do.
Item {
    id: m

    property real minColumnWidth: 440
    property int maxColumns: 4
    property real spacing: 12

    readonly property int columns: Math.max(1, Math.min(m.maxColumns,
        Math.floor((m.width + m.spacing) / (m.minColumnWidth + m.spacing))))
    readonly property real columnWidth: (m.width - m.spacing * (m.columns - 1)) / m.columns

    implicitHeight: m._height

    property real _height: 0
    property var _hooked: []
    property bool _pending: false

    function _schedule() {
        if (m._pending)
            return;
        m._pending = true;
        Qt.callLater(m._relayout);
    }

    function _hook() {
        for (let i = 0; i < m.children.length; ++i) {
            const c = m.children[i];
            if (m._hooked.indexOf(c) >= 0)
                continue;
            m._hooked.push(c);
            c.implicitHeightChanged.connect(m._schedule);
            c.visibleChanged.connect(m._schedule);
        }
        m._schedule();
    }

    function _relayout() {
        m._pending = false;
        const n = m.columns;
        const heights = [];
        for (let k = 0; k < n; ++k)
            heights.push(0);

        for (let i = 0; i < m.children.length; ++i) {
            const c = m.children[i];
            if (!c.visible)
                continue;

            if (c.masonryFull === true) {
                const top = Math.max.apply(null, heights);
                c.x = 0;
                c.y = top;
                c.width = m.width;
                c.height = c.implicitHeight;
                for (let k = 0; k < n; ++k)
                    heights[k] = top + c.implicitHeight + m.spacing;
                continue;
            }

            let col = 0;
            for (let k = 1; k < n; ++k)
                if (heights[k] < heights[col] - 0.5)
                    col = k;
            c.x = col * (m.columnWidth + m.spacing);
            c.y = heights[col];
            c.width = m.columnWidth;
            c.height = c.implicitHeight;
            heights[col] += c.implicitHeight + m.spacing;
        }
        m._height = Math.max(0, Math.max.apply(null, heights) - m.spacing);
    }

    onChildrenChanged: m._hook()
    onWidthChanged: m._schedule()
    onColumnsChanged: m._schedule()
    Component.onCompleted: m._hook()
}
