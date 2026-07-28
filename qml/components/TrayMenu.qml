import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The notification-area context menu.
//
// A separate frameless Window rather than a native Win32 popup: the native menu
// does not follow the dark theme and would look pasted on. Qt.Popup gives the
// dismiss-on-outside-click behaviour for free.
Window {
    id: menu

    flags: Qt.Popup | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint
    color: 'transparent'
    width: 216
    height: body.implicitHeight + 12

    // Anchor the menu's bottom-right at the cursor, the way Windows tray menus
    // sit when the notification area is in its usual corner -- then clamp so it
    // can never land off-screen.
    function popupAt(gx, gy) {
        const sx = menu.screen ? menu.screen.virtualX : 0;
        const sy = menu.screen ? menu.screen.virtualY : 0;
        const sw = menu.screen ? menu.screen.width : 1920;
        const sh = menu.screen ? menu.screen.height : 1080;

        menu.x = Math.max(sx + 4, Math.min(gx - menu.width, sx + sw - menu.width - 4));
        menu.y = Math.max(sy + 4, Math.min(gy - menu.height, sy + sh - menu.height - 4));
        menu.show();
        menu.requestActivate();
    }

    component MenuRow: Rectangle {
        id: rowRoot
        property string label: ''
        property string detail: ''
        property bool danger: false
        signal triggered()

        Layout.fillWidth: true
        implicitHeight: 32
        radius: 6
        color: hover.hovered
               ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.06))
               : 'transparent'

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            HusText {
                text: rowRoot.label
                font.pixelSize: 12
                color: rowRoot.danger ? HusTheme.Primary.colorError
                                      : HusTheme.Primary.colorTextBase
            }
            Item { Layout.fillWidth: true }
            HusText {
                text: rowRoot.detail
                font.pixelSize: 11
                opacity: 0.45
                visible: rowRoot.detail !== ''
            }
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: {
                menu.close();
                rowRoot.triggered();
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 10
        color: HusTheme.isDark ? '#242424' : '#ffffff'
        border.width: 1
        border.color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.10)

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            MenuRow {
                label: AppController.eqEnabled ? '关闭均衡器' : '启用均衡器'
                detail: AppController.eqEnabled ? '开' : '关'
                onTriggered: AppController.eqEnabled = !AppController.eqEnabled
            }

            MenuRow {
                label: AppController.engaged ? '停止接管 config.txt' : '接管 config.txt'
                detail: AppController.engaged ? '已接管' : ''
                enabled: AppController.apoFound && AppController.configWritable
                opacity: enabled ? 1 : 0.4
                onTriggered: AppController.engaged = !AppController.engaged
            }

            HusDivider { Layout.fillWidth: true }

            MenuRow {
                label: '全部归零'
                onTriggered: AppController.resetAll()
            }

            MenuRow {
                label: '显示主窗口'
                onTriggered: menu.showMainWindow()
            }

            HusDivider { Layout.fillWidth: true }

            MenuRow {
                label: '退出 DreamDSP'
                danger: true
                onTriggered: AppController.quitApplication()
            }
        }
    }

    signal showMainWindow()
}
