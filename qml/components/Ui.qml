pragma Singleton

import QtQuick
import QtCore

// The application's visual vocabulary, in one place.
//
// Before this, every pane chose its own sizes: titles at 13, hints at 11, rows
// at 12, page margins of 16 here and 12 there. Each choice was reasonable on
// its own and together they read as no hierarchy at all. These are the only
// numbers a pane should need.
QtObject {
    // ------------------------------------------------------------- type
    readonly property int pageTitle: 20
    readonly property int groupTitle: 13
    readonly property int cardTitle: 14
    readonly property int body: 13
    readonly property int label: 12
    readonly property int caption: 11

    // ---------------------------------------------------------- spacing
    readonly property int pagePadding: 20
    readonly property int gap: 12          // between cards
    readonly property int groupGap: 22     // between groups of cards
    readonly property int cardPadding: 16
    readonly property int rowGap: 10       // between rows inside a card

    readonly property int radius: 12
    readonly property int smallRadius: 8

    // The long explanations -- why a knee is the cheapest decibel, where the
    // Dolby curves come from -- are worth keeping and not worth reading every
    // time. Off by default; the header has the switch, and it is remembered.
    property alias showNotes: store.showNotes

    readonly property Settings store: Settings {
        id: store
        // An explicit file: the application deliberately sets no organisation
        // name (see main.cpp), and without one QSettings has nowhere to go.
        location: StandardPaths.writableLocation(StandardPaths.AppConfigLocation) + '/ui.ini'
        category: 'ui'
        property bool showNotes: false
    }
}
