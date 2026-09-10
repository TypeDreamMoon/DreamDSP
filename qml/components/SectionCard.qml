import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// A titled surface.
//
// Sizes itself to its content by default. Hard-coded card heights are a
// guess that goes stale the moment a control is added, and with clipping on
// the symptom is a silently truncated last row -- so the height is derived
// instead. A card that should stretch instead can still say
// Layout.fillHeight: true.
Rectangle {
    id: root

    property string title: ''
    property string hint: ''
    property Component headerRight: null
    default property alias content: contentArea.data
    property alias contentItem: contentArea

    // Content is conventionally a single layout anchored to fill contentArea;
    // its implicitHeight is what the card needs to show everything.
    readonly property real contentImplicitHeight:
        contentArea.children.length > 0 ? contentArea.children[0].implicitHeight : 0

    implicitHeight: column.anchors.margins * 2 + column.implicitHeight

    radius: 10
    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.025)
    border.width: 1
    border.color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.07)

    Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationMid } }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: 8
            visible: root.title !== '' || root.headerRight !== null

            // Title and description side by side while there is room, stacked
            // when there is not.
            //
            // The description used to be a bare Text with elide set and no
            // fillWidth, which means it never elided -- a layout gives an item
            // with no width policy its implicit width, and the elide only
            // engages once something narrows it. On a full-width card that was
            // invisible; put two cards side by side and the description ran out
            // of its own card and across the title of the next one.
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 620 ? 2 : 1
                columnSpacing: 8
                rowSpacing: 0

                HusText {
                    text: root.title
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    visible: root.title !== ''
                }
                HusText {
                    text: root.hint
                    font.pixelSize: 11
                    opacity: 0.55
                    visible: root.hint !== ''
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Loader {
                active: root.headerRight !== null
                sourceComponent: root.headerRight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.contentImplicitHeight
            // Without this a card squeezed below its content's natural height
            // spills over whatever is underneath instead of being cut off.
            clip: true
        }
    }
}
