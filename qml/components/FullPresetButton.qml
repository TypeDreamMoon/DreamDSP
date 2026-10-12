import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import HuskarUI.Basic
import DreamDSP

// Whole-application presets: the equalizer, every effect, the convolution and
// the AutoEQ source, saved and restored together. They live in the header
// because they are about everything on every page, not about the equalizer
// page where they used to sit.
HusIconButton {
    id: root

    property var entries: []

    text: '方案'
    type: popup.opened ? HusButton.Type_Primary : HusButton.Type_Text
    iconSource: HusIcon.AppstoreOutlined
    onClicked: popup.opened ? popup.close() : popup.open()

    function reload() { entries = AppController.fullPresets(); }

    Component.onCompleted: reload()

    Connections {
        target: AppController
        function onFullPresetsChanged() { root.reload(); }
    }

    HusToolTip {
        parent: root
        visible: root.hovered && !popup.opened
        text: '保存或载入全部设置(均衡、效果、卷积、输出)'
    }

    HusPopup {
        id: popup
        y: root.height + 6
        x: Math.min(0, root.width - width)
        width: 340
        padding: 14
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

        onOpened: nameField.text = ''

        contentItem: ColumnLayout {
            spacing: 10

            HusText {
                text: '配置方案'
                font.pixelSize: Ui.cardTitle
                font.weight: Font.DemiBold
            }
            HusText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Ui.caption
                opacity: 0.55
                text: '一个方案保存所有页面的全部设置。只想存均衡曲线,用均衡器页的「曲线预设」。'
            }

            // The saved ones: click to load.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.entries.length > 0

                Repeater {
                    model: root.entries

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 34
                        radius: Ui.smallRadius
                        color: rowHover.hovered ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.06)
                                                                : Qt.rgba(0, 0, 0, 0.04))
                                                : 'transparent'

                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                AppController.loadFullPreset(row.modelData.path);
                                popup.close();
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 2
                            spacing: 6

                            HusIconText {
                                iconSource: HusIcon.FileTextOutlined
                                iconSize: 13
                                opacity: 0.5
                            }
                            HusText {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                elide: Text.ElideRight
                                font.pixelSize: Ui.body
                            }
                            HusIconButton {
                                visible: rowHover.hovered
                                type: HusButton.Type_Text
                                iconSource: HusIcon.DeleteOutlined
                                onClicked: AppController.deleteFullPreset(row.modelData.path)
                            }
                        }
                    }
                }
            }

            HusText {
                visible: root.entries.length === 0
                text: '还没有保存过方案'
                font.pixelSize: Ui.label
                opacity: 0.45
            }

            HusDivider { Layout.fillWidth: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                HusInput {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: '把当前设置存为…'
                    onAccepted: saveButton.clicked()
                }
                HusButton {
                    id: saveButton
                    text: '保存'
                    type: HusButton.Type_Primary
                    enabled: nameField.text.trim().length > 0
                    onClicked: {
                        if (nameField.text.trim().length > 0
                                && AppController.saveFullPreset(nameField.text))
                            nameField.text = '';
                    }
                }
            }

            HusButton {
                Layout.alignment: Qt.AlignRight
                type: HusButton.Type_Link
                text: '打开方案文件夹'
                onClicked: AppController.openPresetFolder()
            }
        }
    }
}
