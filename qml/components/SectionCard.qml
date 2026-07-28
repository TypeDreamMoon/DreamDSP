import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// A titled surface. Deliberately hand-rolled rather than HusCard: we want the
// content to fill the card and control its own padding, plus a slot for
// controls on the right of the title row.
Rectangle {
    id: root

    property string title: ''
    property string hint: ''
    property Component headerRight: null
    default property alias content: contentArea.data
    property alias contentItem: contentArea

    radius: 10
    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.025)
    border.width: 1
    border.color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.07)

    Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationMid } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.title !== '' || root.headerRight !== null

            HusText {
                text: root.title
                font.pixelSize: 13
                font.weight: Font.DemiBold
                visible: root.title !== ''
            }
            HusText {
                text: root.hint
                font.pixelSize: 11
                opacity: 0.55
                visible: root.hint !== ''
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            Loader {
                active: root.headerRight !== null
                sourceComponent: root.headerRight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
