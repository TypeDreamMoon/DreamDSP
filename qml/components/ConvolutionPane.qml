import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Impulse-response browser. APO does the convolution itself, so this only has
// to find files and write one line.
Item {
    id: root

    property var results: []

    function runSearch() {
        results = AppController.searchImpulses(searchField.text, 250);
    }

    function ensureLoaded() {
        if (AppController.impulseCount === 0 && AppController.loadImpulses())
            runSearch();
        else if (AppController.impulseCount > 0 && results.length === 0)
            runSearch();
    }

    onVisibleChanged: if (visible) ensureLoaded()

    Timer {
        id: debounce
        interval: 180
        onTriggered: root.runSearch()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ------------------------------------------------------ current state
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: current.implicitHeight + 20
            radius: 8
            visible: AppController.convolutionFile !== ''
            color: AppController.rateMismatch
                   ? Qt.rgba(1, 0.4, 0.2, 0.10)
                   : (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
            border.width: 1
            border.color: AppController.rateMismatch
                          ? HusTheme.Primary.colorError
                          : 'transparent'

            RowLayout {
                id: current
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                HusSwitch {
                    checked: AppController.convolutionEnabled
                    onToggled: AppController.convolutionEnabled = checked
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    HusText {
                        text: AppController.convolutionName
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    HusText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        opacity: 0.6
                        color: AppController.rateMismatch ? HusTheme.Primary.colorError
                                                          : HusTheme.Primary.colorTextBase
                        text: AppController.rateMismatch
                              ? '⚠ 采样率不匹配:脉冲响应 ' + AppController.convolutionRate
                                + ' Hz,设备 ' + AppController.deviceRate
                                + ' Hz。APO 要求两者一致,否则卷积不会正确工作。'
                              : AppController.convolutionRate + ' Hz · 与设备一致'
                    }
                }

                HusButton {
                    text: '移除'
                    onClicked: AppController.clearConvolution()
                }
            }
        }

        // ------------------------------------------------------------ search
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            HusInput {
                id: searchField
                Layout.fillWidth: true
                placeholderText: AppController.impulseCount > 0
                                 ? '搜索脉冲响应…'
                                 : '尚未扫描'
                enabled: AppController.impulseCount > 0
                onTextChanged: debounce.restart()
            }

            HusButton {
                text: AppController.impulseCount > 0
                      ? AppController.impulseCount + ' 个'
                      : '扫描'
                type: AppController.impulseCount > 0 ? HusButton.Type_Default
                                                     : HusButton.Type_Primary
                enabled: AppController.impulseCount === 0
                onClicked: root.ensureLoaded()
            }
        }

        HusDivider { Layout.fillWidth: true }

        // ------------------------------------------------------------- list
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.results

            HusScrollBar.vertical: HusScrollBar { }

            delegate: Rectangle {
                id: row
                required property var modelData

                width: ListView.view.width
                height: 44
                radius: 6
                color: hover.hovered
                       ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
                       : 'transparent'

                HoverHandler { id: hover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        HusText {
                            text: row.modelData.name
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        HusText {
                            text: row.modelData.category
                            font.pixelSize: 10
                            opacity: 0.45
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    HusText {
                        text: row.modelData.ok
                              ? (row.modelData.rate / 1000).toFixed(1) + ' kHz · '
                                + row.modelData.ms + ' ms'
                              : '未知格式'
                        font.pixelSize: 10
                        opacity: 0.5
                    }

                    // Wrong sample rate for the current device: APO would load
                    // it but the result is not what the file describes.
                    HusTag {
                        text: '采样率不符'
                        visible: row.modelData.mismatch
                        colorText: HusTheme.Primary.colorError
                    }

                    HusButton {
                        text: '应用'
                        type: HusButton.Type_Primary
                        onClicked: AppController.selectImpulse(row.modelData.index)
                    }
                }
            }
        }

        HusText {
            Layout.fillWidth: true
            font.pixelSize: 11
            opacity: 0.4
            text: '来源:DreamDSP 的 impulses 目录、APO 配置目录,以及 ViPER4Windows 自带的 IR 库'
        }
    }
}
