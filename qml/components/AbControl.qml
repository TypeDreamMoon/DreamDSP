import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// A/B comparison, in the header where it is reachable from every page.
//
// Off, it is one button. On, there are two sides, each a whole copy of the
// application's settings: change something on B, then flip between A and B
// while the music plays. Hearing two settings a second apart says more than
// remembering what the first one sounded like a minute ago.
RowLayout {
    id: root

    readonly property int active: AppController.abActive

    spacing: 6

    HusIconButton {
        visible: root.active < 0
        text: 'A/B 对比'
        type: HusButton.Type_Text
        iconSource: HusIcon.SwapOutlined
        onClicked: AppController.startAb()

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '把现在的设置存成 A,并复制一份 B。改 B,再在 A 和 B 之间来回切换听差别'
        }
    }

    // The two sides.
    Row {
        visible: root.active >= 0
        spacing: 4

        Repeater {
            model: ['A', 'B']

            delegate: HusButton {
                required property int index
                required property string modelData
                width: 40
                text: modelData
                type: root.active === index ? HusButton.Type_Primary : HusButton.Type_Default
                onClicked: AppController.selectAb(index)

                HusToolTip {
                    parent: parent
                    visible: parent.hovered
                    text: root.active === index ? '正在听 ' + modelData
                                                 : '切换到 ' + modelData + '(这一侧的改动单独保留)'
                }
            }
        }
    }

    HusIconButton {
        visible: root.active >= 0
        type: HusButton.Type_Text
        iconSource: HusIcon.CopyOutlined
        onClicked: AppController.copyAb()

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '把这一侧复制到另一侧'
        }
    }

    HusIconButton {
        visible: root.active >= 0
        type: HusButton.Type_Text
        iconSource: HusIcon.CloseOutlined
        onClicked: AppController.stopAb()

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '结束对比 —— 保留正在听的这一侧'
        }
    }
}
