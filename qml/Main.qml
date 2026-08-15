pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

import "components"

HusWindow {
    id: win

    width: 1120
    height: 740
    minimumWidth: 960
    minimumHeight: 620
    title: 'DreamDSP'
    followThemeSwitch: true

    // Which section the navigation rail is on.
    property int page: 0

    captionBar.visible: true
    captionBar.height: 34
    captionBar.showThemeButton: true
    captionBar.themeCallback: () => {
        HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark;
        win.setWindowMode(HusTheme.isDark);
    }

    // Launched by the run-at-login entry (--tray): come up hidden.
    visible: !AppController.startHidden

    Component.onCompleted: {
        HusTheme.darkMode = HusTheme.Dark;
        HusTheme.installThemePrimaryColorBase('#3d7eff');
        win.setWindowMode(true);
        AppController.attachWindow(win);
    }

    // Vetoing the close is also what quit() runs into: since Qt 6.6 it asks
    // every window to close and abandons the quit if one refuses. So a real
    // quit has to be let through, or the application can never exit.
    onClosing: (close) => {
        AppController.flushNow();
        if (!AppController.quitting && AppController.closeToTray && AppController.trayActive) {
            close.accepted = false;
            win.hide();
        }
    }

    function restoreFromTray() {
        win.show();
        win.raise();
        win.requestActivate();
    }

    function showTrayMenuAt(gx, gy) {
        trayMenu.popupAt(gx, gy);
    }

    TrayMenu {
        id: trayMenu
        onShowMainWindow: win.restoreFromTray()
    }

    Connections {
        target: AppController
        function onTrayActivated() {
            if (win.visible) win.hide(); else win.restoreFromTray();
        }
        function onTrayMenuRequested(gx, gy) { trayMenu.popupAt(gx, gy); }
        function onToggleWindowRequested() {
            if (win.visible) win.hide(); else win.restoreFromTray();
        }
    }

    // HusWindow draws no background of its own.
    Rectangle {
        anchors.fill: parent
        z: -1
        color: HusTheme.Primary.colorBgBase
        Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationMid } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: win.captionBar.height
        spacing: 0

        NavRail {
            Layout.fillHeight: true
            currentIndex: win.page
            onCurrentIndexChanged: win.page = currentIndex
            items: [
                { label: '均衡器', icon: HusIcon.SlidersOutlined },
                { label: '效果',   icon: HusIcon.ThunderboltOutlined },
                { label: '卷积',   icon: HusIcon.SoundOutlined },
                { label: '输出',   icon: HusIcon.DeploymentUnitOutlined },
                { label: '链路',   icon: HusIcon.NodeIndexOutlined },
                { label: 'AutoEQ', icon: HusIcon.CustomerServiceOutlined },
                { label: '设置',   icon: HusIcon.SettingOutlined },
            ]
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ------------------------------------------------------- top bar
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56
                color: 'transparent'

                Rectangle {
                    width: parent.width
                    height: 1
                    anchors.bottom: parent.bottom
                    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    HusSwitch {
                        checked: AppController.eqEnabled
                        onToggled: AppController.eqEnabled = checked
                    }

                    HusText {
                        text: '总开关'
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    // Which program is applying the curve. Two of them can, and
                    // "the slider does nothing" has a different cause for each,
                    // so the answer is on screen rather than inferable.
                    StatusPill {
                        text: '均衡 · ' + AppController.eqEngine
                        dotColor: !AppController.eqEnabled ? HusTheme.Primary.colorTextQuaternary
                                : AppController.eqNative   ? HusTheme.Primary.colorSuccess
                                : AppController.engaged    ? HusTheme.Primary.colorWarning
                                                           : HusTheme.Primary.colorError
                    }

                    Item { Layout.fillWidth: true }

                    MeterBar { Layout.alignment: Qt.AlignVCenter }

                    HusText {
                        text: '输出设备'
                        font.pixelSize: 12
                        opacity: 0.6
                    }

                    HusSelect {
                        Layout.preferredWidth: 250
                        model: AppController.deviceNames.map((n, i) => ({ label: n, value: i }))
                        currentIndex: AppController.currentDevice
                        onActivated: (index) => AppController.currentDevice = index
                    }

                    HusIconButton {
                        iconSource: HusIcon.ReloadOutlined
                        onClicked: AppController.refreshDevices()
                    }

                    // The fallback path, for a machine where DreamDSP's own
                    // processing object is not in the chain. Hidden entirely
                    // when Equalizer APO is not installed -- it is no longer
                    // something DreamDSP needs.
                    HusButton {
                        visible: AppController.apoFound
                        text: AppController.engaged ? '交回 DreamDSP' : '交给 Equalizer APO'
                        type: HusButton.Type_Default
                        enabled: AppController.apoFound && AppController.configWritable
                        onClicked: AppController.engaged = !AppController.engaged
                    }
                }
            }

            // -------------------------------------------------------- pages
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                currentIndex: win.page

                EqualizerPane { }
                EffectsPane { }
                ConvolutionPane { }
                OutputPane { }
                ChainPane { onGoToPage: (page) => win.page = page }
                AutoEqPane { }
                SettingsPane { }
            }

            // --------------------------------------------------- status bar
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34
                color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.02) : Qt.rgba(0, 0, 0, 0.02)

                Rectangle {
                    width: parent.width
                    height: 1
                    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    // DreamDSP's own audio component comes first, because it is
                    // the one that has to be working. Equalizer APO's absence
                    // used to be reported in red; it is not a fault any more.
                    StatusPill {
                        text: AppController.apoStatusText
                        dotColor: AppController.apoLive
                                  ? (AppController.apoInSync ? HusTheme.Primary.colorSuccess
                                                             : HusTheme.Primary.colorWarning)
                                  : (AppController.apoAttached ? HusTheme.Primary.colorWarning
                                                               : HusTheme.Primary.colorTextQuaternary)
                    }

                    StatusPill {
                        visible: AppController.apoFound
                        showDot: false
                        text: 'Equalizer APO ' + AppController.apoVersion
                    }

                    HusText {
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        elide: Text.ElideMiddle

                        readonly property bool isError: AppController.lastError !== ''
                        readonly property bool isInfo: !isError && AppController.lastMessage !== ''

                        text: isError ? AppController.lastError
                             : isInfo ? AppController.lastMessage
                                      : AppController.apoConfigPath
                        opacity: (isError || isInfo) ? 0.95 : 0.45
                        color: isError ? HusTheme.Primary.colorError
                             : isInfo ? HusTheme.Primary.colorSuccess
                                      : HusTheme.Primary.colorTextBase
                    }

                    HusButton {
                        text: '全部归零'
                        onClicked: AppController.resetAll()
                    }
                }
            }
        }
    }
}
