import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import HuskarUI.Basic
import DreamDSP

// The equalizer's own presets: pick one to load, save over it, save it under
// a new name, delete it. Presets living in Equalizer APO's config directory
// (i.e. an existing Peace install's) are listed too, but read-only.
//
// The whole-application presets used to share this row and its name field,
// which made it impossible to tell which button saved what. They are in the
// header now (FullPresetButton): they are not an equalizer thing.
RowLayout {
    id: root

    spacing: 8

    // Rebuild the plain array HusSelect wants whenever the store changes.
    property var entries: []

    // Typing a new name replaces the selector until it is saved or abandoned.
    property bool naming: false

    readonly property var selected:
        presetSelect.currentIndex >= 0 && presetSelect.currentIndex < entries.length
        ? entries[presetSelect.currentIndex] : null

    function reload() {
        entries = AppController.presets.entries();
        // Assigning a model makes ComboBox write currentIndex itself, which
        // breaks any declarative binding on it -- so drive it imperatively,
        // after the model change has settled.
        Qt.callLater(syncSelection);
    }

    // Show whichever preset is actually loaded, or nothing if the current
    // curve did not come from one.
    function syncSelection() {
        const wanted = AppController.currentPreset;
        let found = -1;
        if (wanted.length > 0) {
            for (let i = 0; i < entries.length; ++i) {
                if (entries[i].name === wanted) { found = i; break; }
            }
        }
        presetSelect.currentIndex = found;
    }

    function startNaming() {
        nameField.text = AppController.currentPreset;
        naming = true;
        nameField.forceActiveFocus();
        nameField.selectAll();
    }

    function commitName() {
        const name = nameField.text.trim();
        if (name.length === 0)
            return;
        if (AppController.savePreset(name))
            naming = false;
    }

    Component.onCompleted: reload()

    Connections {
        target: AppController.presets
        function onCountChanged() { root.reload(); }
    }

    Connections {
        target: AppController
        function onCurrentPresetChanged() { root.syncSelection(); }
    }

    HusIconText {
        iconSource: HusIcon.SaveOutlined
        iconSize: 14
        opacity: 0.55
    }

    HusText {
        text: '曲线预设'
        font.pixelSize: Ui.label
        opacity: 0.6
    }

    HusSelect {
        id: presetSelect
        visible: !root.naming
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        Layout.minimumWidth: 160
        model: root.entries
        // Start unselected so the placeholder shows -- otherwise the first
        // preset in the list looks active when nothing has been loaded.
        currentIndex: -1
        placeholderText: root.entries.length > 0 ? '选择预设…' : '暂无预设 —— 调好后点「另存为」'
        onActivated: (index) => AppController.loadPreset(index)
    }

    HusInput {
        id: nameField
        visible: root.naming
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        Layout.minimumWidth: 160
        placeholderText: '新预设的名字'
        onAccepted: root.commitName()
        Keys.onEscapePressed: root.naming = false
    }

    // Not naming: save over the loaded preset, or start a new one.
    HusButton {
        visible: !root.naming
        text: '保存'
        type: AppController.dirty ? HusButton.Type_Primary : HusButton.Type_Default
        // Over the loaded preset only, and never over a Peace one: that file
        // belongs to another program.
        enabled: root.selected !== null && !root.selected.imported && AppController.dirty
        onClicked: AppController.savePreset(AppController.currentPreset)

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: root.selected ? '覆盖「' + root.selected.name + '」' : '先选择或另存一个预设'
        }
    }

    HusButton {
        visible: !root.naming
        text: '另存为…'
        onClicked: root.startNaming()
    }

    // A curve someone else made -- a PEQ .xml from a DSP client, an AutoEQ
    // ParametricEQ.txt, a GraphicEQ curve. loadEqFile sorts out which it is.
    HusButton {
        visible: !root.naming
        text: '导入…'
        onClicked: eqFileDialog.open()

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: 'PEQ 曲线文件 .xml · AutoEQ 的 ParametricEQ.txt / GraphicEQ.txt'
        }
    }

    // The reverse: the curve that is loaded, written back out as something a
    // a DSP client will open. It lands in the preset folder.
    HusButton {
        visible: !root.naming
        text: '导出 XML'
        enabled: AppController.bands.count > 0
        onClicked: AppController.exportFiiXml('')

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '导出成 DSP 客户端能读的 PEQ 曲线文件,放在预设目录里'
        }
    }

    // Naming: confirm or abandon.
    HusButton {
        visible: root.naming
        text: '保存'
        type: HusButton.Type_Primary
        enabled: nameField.text.trim().length > 0
        onClicked: root.commitName()
    }

    HusButton {
        visible: root.naming
        text: '取消'
        type: HusButton.Type_Text
        onClicked: root.naming = false
    }

    HusIconButton {
        visible: !root.naming
        type: HusButton.Type_Text
        iconSource: HusIcon.DeleteOutlined
        enabled: root.selected !== null && !root.selected.imported
        onClicked: {
            if (AppController.deletePreset(presetSelect.currentIndex))
                presetSelect.currentIndex = -1;
        }

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '删除选中的预设'
        }
    }

    Item { Layout.fillWidth: true }

    HusIconButton {
        type: HusButton.Type_Text
        iconSource: HusIcon.FolderOpenOutlined
        onClicked: AppController.openPresetFolder()

        HusToolTip {
            parent: parent
            visible: parent.hovered
            text: '打开预设文件夹'
        }
    }

    FileDialog {
        id: eqFileDialog
        title: '导入调音文件'
        fileMode: FileDialog.OpenFile
        nameFilters: ['曲线文件 (*.xml *.txt *.peace)', 'PEQ 曲线 (*.xml)', '所有文件 (*)']
        onAccepted: AppController.loadEqFile(selectedFile)
    }
}
