import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Preset row: pick one to load, name-and-save the current curve, delete.
// Presets living in Equalizer APO's config directory (i.e. an existing Peace
// install's) are listed too, but read-only.
RowLayout {
    id: root

    spacing: 8

    // Rebuild the plain array HusSelect wants whenever the store changes.
    property var entries: []

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

    Component.onCompleted: reload()

    Connections {
        target: AppController.presets
        function onCountChanged() { root.reload(); }
    }

    Connections {
        target: AppController
        function onCurrentPresetChanged() { root.syncSelection(); }
    }

    HusText {
        text: '预设'
        font.pixelSize: 12
        opacity: 0.6
    }

    HusSelect {
        id: presetSelect
        Layout.preferredWidth: 240
        model: root.entries
        // Start unselected so the placeholder shows -- otherwise the first
        // preset in the list looks active when nothing has been loaded.
        currentIndex: -1
        placeholderText: root.entries.length > 0 ? '选择预设…' : '暂无预设'
        onActivated: (index) => AppController.loadPreset(index)
    }

    HusIconButton {
        iconSource: HusIcon.DeleteOutlined
        enabled: presetSelect.currentIndex >= 0
                 && presetSelect.currentIndex < root.entries.length
                 && !root.entries[presetSelect.currentIndex].imported
        onClicked: {
            if (AppController.deletePreset(presetSelect.currentIndex))
                presetSelect.currentIndex = -1;
        }
    }

    HusDivider {
        Layout.preferredHeight: 22
        orientation: Qt.Vertical
    }

    HusInput {
        id: nameField
        Layout.preferredWidth: 160
        placeholderText: '预设名'
        text: AppController.currentPreset
        onAccepted: saveButton.doSave()
    }

    HusButton {
        id: saveButton
        text: '保存'
        type: AppController.dirty ? HusButton.Type_Primary : HusButton.Type_Default
        enabled: nameField.text.trim().length > 0

        function doSave() {
            if (nameField.text.trim().length > 0)
                AppController.savePreset(nameField.text);
        }
        onClicked: doSave()
    }

    HusIconButton {
        iconSource: HusIcon.FolderOpenOutlined
        onClicked: AppController.openPresetFolder()
    }
}
