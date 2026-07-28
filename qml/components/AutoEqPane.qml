import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Browse and import the AutoEQ headphone-correction databases that ship in
// Equalizer APO's config directory.
Item {
    id: root

    property var results: []

    function runSearch() {
        results = AppController.searchAutoEq(searchField.text, 150);
    }

    // The databases are ~20 MB of text; loading is deferred until this pane is
    // actually looked at, rather than paid for at every startup.
    function ensureLoaded() {
        if (!AppController.autoEqLoaded && AppController.loadAutoEq())
            runSearch();
        else if (AppController.autoEqLoaded && results.length === 0)
            runSearch();
    }

    onVisibleChanged: if (visible) ensureLoaded()

    Timer {
        id: searchDebounce
        interval: 180
        onTriggered: root.runSearch()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            HusInput {
                id: searchField
                Layout.fillWidth: true
                placeholderText: AppController.autoEqLoaded
                                 ? '搜索耳机型号、测量者或目标曲线…'
                                 : '先载入数据库'
                enabled: AppController.autoEqLoaded
                onTextChanged: searchDebounce.restart()
            }

            HusButton {
                text: AppController.autoEqLoaded
                      ? AppController.autoEqCount + ' 条'
                      : '载入数据库'
                type: AppController.autoEqLoaded ? HusButton.Type_Default : HusButton.Type_Primary
                enabled: !AppController.autoEqLoaded
                onClicked: root.ensureLoaded()
            }
        }

        HusDivider { Layout.fillWidth: true }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.results

            HusScrollBar.vertical: HusScrollBar { }

            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData

                width: ListView.view.width
                height: 46
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
                            text: row.modelData.device
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        HusText {
                            text: [row.modelData.measurer, row.modelData.target]
                                  .filter(s => s && s.length > 0).join('  ·  ')
                            font.pixelSize: 11
                            opacity: 0.45
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    HusTag { text: row.modelData.source }

                    HusButton {
                        text: '导入'
                        type: HusButton.Type_Primary
                        onClicked: AppController.importAutoEq(row.modelData.index, false)

                        HusToolTip {
                            parent: parent
                            visible: parent.hovered
                            text: '参数均衡(峰值 + 高低架滤波器)'
                        }
                    }

                    HusButton {
                        text: '10 段'
                        enabled: row.modelData.hasFixed
                        opacity: enabled ? 1 : 0.35
                        onClicked: AppController.importAutoEq(row.modelData.index, true)

                        HusToolTip {
                            parent: parent
                            visible: parent.hovered
                            text: '固定频段版本(31/62/…/16k,Q 1.41)'
                        }
                    }
                }
            }
        }

        HusText {
            Layout.fillWidth: true
            text: AppController.autoEqLoaded
                  ? '显示前 ' + root.results.length + ' 条结果 · 导入会替换当前曲线'
                  : '数据库来自 Equalizer APO 配置目录(Harman / IEF / IEF Bass / OPRA)'
            font.pixelSize: 11
            opacity: 0.4
        }
    }
}
