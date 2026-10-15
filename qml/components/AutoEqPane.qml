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

        // Where the current curve came from. The list shows 150 rows of
        // thousands, so the one in use is often not among them.
        RowLayout {
            Layout.fillWidth: true
            visible: AppController.autoEqSource !== ''
            spacing: 8

            HusText {
                text: '当前曲线来自'
                font.pixelSize: Ui.label
                opacity: 0.55
            }
            HusText {
                Layout.fillWidth: true
                text: AppController.autoEqSource
                      + (AppController.autoEqEntry >= 0
                         ? (AppController.autoEqFixed ? ' · 10 段' : ' · 参数均衡') : '')
                font.pixelSize: Ui.label
                font.weight: Font.DemiBold
                color: HusTheme.Primary.colorPrimary
                elide: Text.ElideRight
            }
            HusButton {
                visible: AppController.autoEqLoaded
                type: HusButton.Type_Link
                text: '在列表中找到它'
                onClicked: {
                    searchField.text = AppController.autoEqSource;
                    root.runSearch();
                }
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

            delegate: ListRow {
                id: row
                required property int index
                required property var modelData

                // The exact entry when it is known (an import made in this run),
                // otherwise any row with the same device name -- after a
                // restart that is all there is to go on.
                current: AppController.autoEqSource !== ''
                         && (AppController.autoEqEntry >= 0
                             ? row.modelData.index === AppController.autoEqEntry
                             : row.modelData.device === AppController.autoEqSource)
                readonly property bool usedParam: row.current && !AppController.autoEqFixed
                readonly property bool usedFixed: row.current && AppController.autoEqFixed
                readonly property var hover: row   // the buttons below read hover.hovered

                width: ListView.view.width
                height: 46

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

                    // Primary only on the row under the pointer: a filled
                    // button on every one of 150 rows is a wall of blue.
                    HusButton {
                        text: row.usedParam ? '已导入' : '导入'
                        type: (row.usedParam || hover.hovered) ? HusButton.Type_Primary
                                                               : HusButton.Type_Default
                        onClicked: AppController.importAutoEq(row.modelData.index, false)

                        HusToolTip {
                            parent: parent
                            visible: parent.hovered
                            text: '参数均衡(峰值 + 高低架滤波器)'
                        }
                    }

                    HusButton {
                        text: row.usedFixed ? '已用 10 段' : '10 段'
                        type: row.usedFixed ? HusButton.Type_Primary : HusButton.Type_Default
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
