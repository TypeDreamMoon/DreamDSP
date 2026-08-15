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

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: '处理顺序'
            hint: '信号从上往下依次经过 —— 灰色的表示没有启用'

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
                anchors.fill: parent
                contentWidth: width
                contentHeight: list.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                HusScrollBar.vertical: HusScrollBar { }

                ColumnLayout {
                    id: list
                    width: parent.width
                    spacing: 4

                    Repeater {
                        model: AppController.chain

                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property var modelData

                            Layout.fillWidth: true
                            implicitHeight: 46
                            radius: 8
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
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 10

                                HusText {
                                    text: String(row.index + 1)
                                    font.pixelSize: 11
                                    opacity: 0.4
                                    Layout.preferredWidth: 18
                                }

                                HusSwitch {
                                    checked: row.modelData.on
                                    onToggled: AppController.setStageEnabled(row.modelData.id, checked)
                                }

                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    opacity: row.modelData.on ? 1.0 : 0.45

                                    HusText {
                                        text: row.modelData.name
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                    HusText {
                                        text: row.modelData.hint
                                        font.pixelSize: 11
                                        opacity: 0.55
                                    }
                                }

                                HusButton {
                                    text: '设置'
                                    type: HusButton.Type_Link
                                    onClicked: root.goToPage(row.modelData.page)
                                }

                                HusIconButton {
                                    iconSource: HusIcon.ArrowUpOutlined
                                    enabled: row.index > 0
                                    onClicked: AppController.moveStage(row.index, row.index - 1)
                                }
                                HusIconButton {
                                    iconSource: HusIcon.ArrowDownOutlined
                                    enabled: row.index < AppController.chain.length - 1
                                    onClicked: AppController.moveStage(row.index, row.index + 1)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
