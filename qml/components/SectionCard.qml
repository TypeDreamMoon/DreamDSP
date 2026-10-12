import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// A titled surface.
//
// Sizes itself to its content by default. Hard-coded card heights are a
// guess that goes stale the moment a control is added, and with clipping on
// the symptom is a silently truncated last row -- so the height is derived
// instead. A card that should stretch instead can still say
// Layout.fillHeight: true.
//
// An effect's card is `collapsible` and carries the effect's switch state as
// `active`. Switched off, it folds to its header: a page of eleven effects of
// which two are on should look like two effects and nine names, not eleven
// equally loud panels. Clicking the header opens a folded card anyway -- the
// parameters are still worth setting before switching on -- and that choice
// lasts until the switch moves again.
Rectangle {
    id: root

    property string title: ''
    property string hint: ''
    property Component headerRight: null
    default property alias content: contentArea.data
    property alias contentItem: contentArea

    property bool collapsible: false
    property bool active: true
    property int _override: -1      // -1 follow `active`, 0 folded, 1 open
    readonly property bool expanded: !root.collapsible
                                     || (root._override >= 0 ? root._override === 1 : root.active)
    onActiveChanged: root._override = -1

    // Content is conventionally a single layout anchored to fill contentArea;
    // its implicitHeight is what the card needs to show everything.
    readonly property real contentImplicitHeight:
        contentArea.children.length > 0 ? contentArea.children[0].implicitHeight : 0

    implicitHeight: column.anchors.margins * 2 + column.implicitHeight

    radius: Ui.radius
    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.025)
    border.width: 1
    border.color: (root.collapsible && root.active)
                  ? Qt.alpha(HusTheme.Primary.colorPrimary, 0.45)
                  : (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.07))

    Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationMid } }
    Behavior on border.color { ColorAnimation { duration: HusTheme.Primary.durationMid } }

    // Accent along the left edge of a card whose effect is running.
    Rectangle {
        visible: root.collapsible && root.active
        width: 3
        radius: 1.5
        anchors.left: parent.left
        anchors.leftMargin: 1
        anchors.top: parent.top
        anchors.topMargin: 14
        height: 22
        color: HusTheme.Primary.colorPrimary
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Ui.cardPadding
        spacing: root.expanded ? 12 : 0

        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: 8
            visible: root.title !== '' || root.headerRight !== null

            HusIconText {
                visible: root.collapsible
                iconSource: root.expanded ? HusIcon.DownOutlined : HusIcon.RightOutlined
                iconSize: 10
                opacity: 0.45
                Layout.alignment: Qt.AlignVCenter
            }

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
                columnSpacing: 10
                rowSpacing: 2

                HusText {
                    text: root.title
                    font.pixelSize: Ui.cardTitle
                    font.weight: Font.DemiBold
                    opacity: (root.collapsible && !root.active) ? 0.7 : 1.0
                    visible: root.title !== ''
                }
                HusText {
                    text: root.hint
                    font.pixelSize: Ui.caption
                    opacity: 0.5
                    visible: root.hint !== ''
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

            }

            Loader {
                id: rightSlot
                active: root.headerRight !== null
                sourceComponent: root.headerRight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Item {
            id: contentArea
            visible: root.expanded
            Layout.fillWidth: true
            Layout.fillHeight: root.expanded
            Layout.preferredHeight: root.expanded ? -1 : 0
            Layout.minimumHeight: root.expanded ? root.contentImplicitHeight : 0
            // Without this a card squeezed below its content's natural height
            // spills over whatever is underneath instead of being cut off.
            clip: true
        }
    }

    // The whole title area opens and folds the card -- everything in the
    // header except its right-hand side, where the switch lives. On the card
    // rather than inside the header, so no layout gets to place it.
    MouseArea {
        enabled: root.collapsible
        x: column.x
        y: column.y
        width: header.width - (rightSlot.item ? rightSlot.item.width + 8 : 0)
        height: header.height
        cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root._override = root.expanded ? 0 : 1
    }
}
