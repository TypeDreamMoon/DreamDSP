import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The processing chain: what runs, and in what order.
//
// The rack view. Every stage exists whether or not it is switched on -- adding
// and removing is the same switch its own page carries, because two notions of
// membership would eventually disagree with each other. What this page adds is
// the order, which nothing else can express.
Item {
    id: root

    signal goToPage(int page)

    // Every column is pinned. Left to itself the row layout distributes the
    // slack across whichever items have no width of their own, so the switch,
    // the settings link and the two arrows all landed at a different x on every
    // row -- the amount depending on how long that stage's description happened
    // to be. With twenty rows that reads as a broken table rather than a list.
    readonly property int wGrip: 22
    readonly property int wIndex: 24
    readonly property int wSwitch: 46
    readonly property int wSettings: 62
    readonly property int wArrow: 30

    readonly property int rowH: 42
    readonly property int rowGap: 4
    readonly property int rowPitch: rowH + rowGap

    // Drag state. Nothing is moved until the button comes up: reordering while
    // the pointer is still down would rebuild the model under the mouse grab
    // and drop the drag on the first row boundary crossed.
    property int dragFrom: -1
    property int dragTo: -1
    property real dragViewY: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: '处理顺序'
            hint: '信号从上往下依次经过 —— 拖左边的手柄可以直接换位置,灰色的表示没有启用'

            headerRight: RowLayout {
                spacing: 8
                HusText {
                    text: '已改动'
                    font.pixelSize: 11
                    opacity: 0.6
                    visible: !AppController.chainIsDefault
                }
                HusButton {
                    text: '恢复默认顺序'
                    enabled: !AppController.chainIsDefault
                    onClicked: AppController.resetChainOrder()
                }
            }

            Flickable {
                id: flick
                anchors.fill: parent
                contentWidth: width
                contentHeight: list.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: root.dragFrom < 0
                HusScrollBar.vertical: HusScrollBar { }

                // Dragging towards an edge scrolls, or a twenty-stage list
                // could only be reordered as far as one screen.
                Timer {
                    running: root.dragFrom >= 0
                    interval: 16
                    repeat: true
                    onTriggered: {
                        const margin = 36
                        const max = Math.max(0, flick.contentHeight - flick.height)
                        if (root.dragViewY < margin)
                            flick.contentY = Math.max(0, flick.contentY - 8)
                        else if (root.dragViewY > flick.height - margin)
                            flick.contentY = Math.min(max, flick.contentY + 8)
                        else
                            return

                        // Recompute here as well, or holding still at the edge
                        // would scroll the list out from under a target that
                        // only ever updates on mouse movement.
                        const n = AppController.chain.length
                        root.dragTo = Math.max(0, Math.min(n - 1,
                                          Math.floor((flick.contentY + root.dragViewY)
                                                     / root.rowPitch)))
                    }
                }

                ColumnLayout {
                    id: list
                    width: parent.width
                    spacing: root.rowGap

                    Repeater {
                        model: AppController.chain

                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property var modelData

                            readonly property bool dragging: root.dragFrom === row.index

                            Layout.fillWidth: true
                            implicitHeight: root.rowH
                            radius: 8
                            opacity: row.dragging ? 0.35 : 1.0
                            color: row.modelData.on
                                   ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.06)
                                                      : Qt.rgba(0, 0, 0, 0.04))
                                   : 'transparent'
                            border.width: 1
                            border.color: row.modelData.on
                                          ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.10)
                                                             : Qt.rgba(0, 0, 0, 0.08))
                                          : (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05)
                                                             : Qt.rgba(0, 0, 0, 0.04))

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 8
                                spacing: 8

                                // --- drag handle ---
                                Item {
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wGrip
                                    Layout.fillHeight: true

                                    HusIconText {
                                        anchors.centerIn: parent
                                        iconSource: HusIcon.HolderOutlined
                                        iconSize: 14
                                        colorIcon: HusTheme.Primary.colorTextBase
                                        opacity: grip.containsMouse || row.dragging ? 0.75 : 0.3
                                    }

                                    MouseArea {
                                        id: grip
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.SizeVerCursor
                                        preventStealing: true

                                        onPressed: (m) => {
                                            root.dragFrom = row.index
                                            root.dragTo = row.index
                                            root.dragViewY = mapToItem(flick, 0, m.y).y
                                        }
                                        onPositionChanged: (m) => {
                                            if (root.dragFrom < 0)
                                                return
                                            root.dragViewY = mapToItem(flick, 0, m.y).y
                                            const y = mapToItem(list, 0, m.y).y
                                            const n = AppController.chain.length
                                            root.dragTo = Math.max(0, Math.min(n - 1,
                                                              Math.floor(y / root.rowPitch)))
                                        }
                                        onReleased: {
                                            if (root.dragFrom >= 0 && root.dragTo >= 0
                                                && root.dragFrom !== root.dragTo)
                                                AppController.moveStage(root.dragFrom, root.dragTo)
                                            root.dragFrom = -1
                                            root.dragTo = -1
                                        }
                                        onCanceled: {
                                            root.dragFrom = -1
                                            root.dragTo = -1
                                        }
                                    }
                                }

                                HusText {
                                    text: String(row.index + 1)
                                    font.pixelSize: 11
                                    opacity: 0.4
                                    horizontalAlignment: Text.AlignRight
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wIndex
                                }

                                HusSwitch {
                                    checked: row.modelData.on
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wSwitch
                                    onToggled: AppController.setStageEnabled(row.modelData.id, checked)
                                }

                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    opacity: row.modelData.on ? 1.0 : 0.45

                                    HusText {
                                        text: row.modelData.name
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    HusText {
                                        text: row.modelData.hint
                                        font.pixelSize: 11
                                        opacity: 0.55
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                HusButton {
                                    text: '设置'
                                    type: HusButton.Type_Link
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wSettings
                                    onClicked: root.goToPage(row.modelData.page)
                                }

                                HusIconButton {
                                    iconSource: HusIcon.ArrowUpOutlined
                                    enabled: row.index > 0
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wArrow
                                    onClicked: AppController.moveStage(row.index, row.index - 1)
                                }
                                HusIconButton {
                                    iconSource: HusIcon.ArrowDownOutlined
                                    enabled: row.index < AppController.chain.length - 1
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: root.wArrow
                                    onClicked: AppController.moveStage(row.index, row.index + 1)
                                }
                            }
                        }
                    }
                }

                // Where the dragged stage will land.
                Rectangle {
                    visible: root.dragFrom >= 0 && root.dragTo >= 0
                    x: 0
                    y: root.dragTo * root.rowPitch
                    width: list.width
                    height: root.rowH
                    radius: 8
                    color: 'transparent'
                    border.width: 2
                    border.color: HusTheme.Primary.colorPrimary
                }
            }
        }
    }
}
