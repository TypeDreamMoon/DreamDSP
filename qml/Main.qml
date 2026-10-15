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

    // The intro plays over the whole application, below the caption bar so the
    // window can still be dragged and closed while it runs. Skipped wherever a
    // person is not watching: hidden starts, and the diagnostic runs that grab
    // or poke the window and must see the real interface.
    readonly property bool skipIntro: {
        const a = Qt.application.arguments;
        return ['--no-splash', '--tray', '--grab', '--slidertest', '--applyir']
            .some(flag => a.includes(flag));
    }

    Loader {
        id: intro
        anchors.fill: parent
        anchors.topMargin: win.captionBar.height
        z: 1000
        active: !win.skipIntro
        sourceComponent: SplashScreen {
            onFinished: intro.active = false
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
                { label: '均衡器',   icon: HusIcon.SlidersOutlined,         section: '调音' },
                { label: '效果',     icon: HusIcon.ThunderboltOutlined,     section: '调音' },
                { label: '卷积',     icon: HusIcon.SoundOutlined,           section: '调音' },
                { label: '输出',     icon: HusIcon.DeploymentUnitOutlined,  section: '调音' },
                { label: '链路',     icon: HusIcon.NodeIndexOutlined,       section: '工具' },
                { label: 'AutoEQ',   icon: HusIcon.CustomerServiceOutlined, section: '工具' },
                { label: '系统音频', icon: HusIcon.DashboardOutlined,       section: '系统' },
                { label: '设置',     icon: HusIcon.SettingOutlined,         section: '系统' },
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

                    // Bypasses the whole rack. It used to be bound to the
                    // equalizer's enable bit while carrying this label, which
                    // left the equalizer as the one effect with no switch of
                    // its own -- and made it look like it could not be turned
                    // off, because the only control that did was disguised as
                    // the application's master switch.
                    HusSwitch {
                        checked: AppController.masterEnabled
                        onToggled: AppController.masterEnabled = checked
                    }

                    HusText {
                        text: '总开关'
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    // Which program is applying the curve. Two of them can, and
                    // "the slider does nothing" has a different cause for each,
                    // so the answer is on screen rather than inferable.
                    // What the whole rack is doing, in one phrase -- not the
                    // equalizer's state, which is what this used to report and
                    // which says nothing when the equalizer is the one effect
                    // switched off. Click through for the full diagnosis.
                    StatusPill {
                        id: overall
                        readonly property int state:
                            !AppController.apoInstalled ? 0
                            : (!AppController.apoAttached && !AppController.engaged) ? 1
                            : !AppController.masterEnabled ? 2
                            : AppController.engaged ? 3
                            : AppController.apoLive ? 4 : 5
                        text: ['音频组件未安装', '未接入当前设备', '已旁通',
                               '由 Equalizer APO 处理', '正在处理', '已就绪'][state]
                        dotColor: [HusTheme.Primary.colorError, HusTheme.Primary.colorWarning,
                                   HusTheme.Primary.colorTextQuaternary, HusTheme.Primary.colorWarning,
                                   HusTheme.Primary.colorSuccess, HusTheme.Primary.colorSuccess][state]

                        MouseArea {
                            id: overallHover
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onClicked: win.page = 6
                        }
                        HusToolTip {
                            parent: overall
                            visible: overallHover.containsMouse
                            text: '查看系统音频诊断'
                        }
                    }

                    Item { Layout.fillWidth: true }

                    MeterBar { Layout.alignment: Qt.AlignVCenter }

                    AbControl { }

                    FullPresetButton { }

                    // Shows the explanations under the controls. They are
                    // written for the first time through a page, so they are
                    // off until asked for.
                    HusIconButton {
                        text: '说明'
                        type: Ui.showNotes ? HusButton.Type_Primary : HusButton.Type_Text
                        iconSource: HusIcon.ReadOutlined
                        onClicked: Ui.showNotes = !Ui.showNotes
                        HusToolTip {
                            parent: parent
                            visible: parent.hovered
                            text: Ui.showNotes ? '隐藏每个控件下面的原理说明' : '显示每个控件下面的原理说明'
                        }
                    }

                    HusText {
                        text: '输出设备'
                        font.pixelSize: 12
                        opacity: 0.6
                    }

                    HusSelect {
                        // Gives way when the header is crowded -- A/B mode adds a
                        // control and the refresh button was pushed off the edge.
                        Layout.fillWidth: true
                        Layout.preferredWidth: 250
                        Layout.maximumWidth: 250
                        Layout.minimumWidth: 110
                        model: AppController.deviceNames.map((n, i) => ({ label: n, value: i }))
                        currentIndex: AppController.currentDevice
                        onActivated: (index) => AppController.currentDevice = index
                    }

                    HusIconButton {
                        iconSource: HusIcon.ReloadOutlined
                        onClicked: AppController.refreshDevices()
                    }

                    // The Equalizer APO hand-over that used to sit here lives
                    // on the 系统音频 page now: it is a fallback for a minority
                    // of machines, not something to keep in the header.
                }
            }

            // -------------------------------------------------- page header
            // The page's name and one line about its state. The pages used to
            // open straight onto their first card, so nothing on screen said
            // where you were or whether anything on the page was doing work.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Ui.pagePadding
                Layout.rightMargin: Ui.pagePadding
                Layout.topMargin: 16
                spacing: 12

                readonly property var titles: ['均衡器', '效果', '卷积', '输出', '链路',
                                               'AutoEQ', '系统音频', '设置']

                function stagesOn(page) {
                    const c = AppController.chain;
                    let on = 0, all = 0;
                    for (let i = 0; i < c.length; ++i) {
                        if (c[i].page !== page) continue;
                        ++all;
                        if (c[i].working) ++on;
                    }
                    return '已开启 ' + on + ' / ' + all;
                }

                HusText {
                    text: parent.titles[win.page]
                    font.pixelSize: Ui.pageTitle
                    font.weight: Font.DemiBold
                }
                HusText {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 3
                    elide: Text.ElideRight
                    font.pixelSize: Ui.label
                    opacity: 0.5
                    text: {
                        switch (win.page) {
                        case 0: return '参数均衡 ' + (AppController.eqEnabled ? '开' : '关')
                                       + ' · 图形均衡 ' + (AppController.graphicEnabled && AppController.graphicLoaded ? '开'
                                                       : AppController.graphicLoaded ? '关' : '未载入曲线');
                        case 1: return parent.stagesOn(1);
                        case 2: return AppController.convolutionEnabled ? '脉冲响应已启用' : '没有启用脉冲响应';
                        case 3: return parent.stagesOn(3);
                        case 4: {
                            const c = AppController.chain;
                            return '信号从上往下依次经过 · 已开启 '
                                   + c.filter(x => x.working).length + ' / ' + c.length;
                        }
                        case 5: return '导入耳机的频响校正曲线';
                        case 6: return 'DreamDSP 是否真的在处理你听到的声音';
                        default: return '';
                        }
                    }
                }
            }

            // ---------------------------------------------- bypass banner
            // The master switch has to be impossible to forget. Every effect
            // page below keeps showing its own switches as they were set --
            // that is the point of a bypass that remembers -- so without this
            // the pages would claim effects are on that are not being heard.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Ui.pagePadding
                Layout.rightMargin: Ui.pagePadding
                Layout.topMargin: 12
                visible: !AppController.masterEnabled && win.page <= 5
                implicitHeight: 40
                radius: 8
                color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04)
                border.width: 1
                border.color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.10)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 10
                    HusIconText {
                        iconSource: HusIcon.PoweroffOutlined
                        iconSize: 14
                        opacity: 0.6
                    }
                    HusText {
                        Layout.fillWidth: true
                        text: '总开关已关闭 —— 所有效果都被旁通,声音原样通过。下面各效果的开关和参数都保留着,打开总开关即恢复。'
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    HusButton {
                        text: '打开总开关'
                        type: HusButton.Type_Primary
                        onClicked: AppController.masterEnabled = true
                    }
                }
            }

            // -------------------------------------------------------- pages
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Ui.pagePadding
                Layout.rightMargin: Ui.pagePadding - 8
                Layout.topMargin: 12
                Layout.bottomMargin: 12
                currentIndex: win.page
                // Faded, not disabled: the settings stay editable while
                // bypassed, they are just not what is being heard.
                opacity: (!AppController.masterEnabled && win.page <= 5) ? 0.55 : 1.0
                Behavior on opacity { NumberAnimation { duration: 160 } }

                EqualizerPane { }
                EffectsPane { }
                ConvolutionPane { }
                OutputPane { }
                ChainPane { onGoToPage: (page) => win.page = page }
                AutoEqPane { }
                SystemPane { }
                SettingsPane { onGoToPage: (page) => win.page = page }
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

                        // Idle, it says nothing. It used to show Equalizer
                        // APO's config path, which stopped meaning anything
                        // once DreamDSP no longer needed Equalizer APO.
                        text: isError ? AppController.lastError
                             : isInfo ? AppController.lastMessage
                                      : ''
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
