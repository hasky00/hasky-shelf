import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: "Hasky Shelf"
    color: theme.background

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground

    readonly property bool rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
    // Bare keys belong to the shelf, so they step aside whenever a dialog,
    // and the text fields in it, has the keyboard.
    readonly property bool shelfHasKeys: !newBook.opened && !help.opened

    // One cell of the shelf: a cover standing on a lit shelf line.
    readonly property int cellWidth: 168
    readonly property int cellHeight: 262
    readonly property int coverWidth: 128
    readonly property int coverHeight: 184
    // Where the shelf line sits inside a cell, just under the cover's foot.
    readonly property int shelfLine: 218

    // Covers take their colors from the theme's palette, so the shelf changes
    // with the theme like the rest of the desktop.
    readonly property var palette: [
        theme.colors.blue, theme.colors.magenta, theme.colors.cyan,
        theme.colors.green, theme.colors.yellow, theme.colors.red
    ]

    function tintFor(index) {
        const picked = palette[index % palette.length]
        return picked ? picked : theme.accent
    }

    // Covers are panels of tinted glass: mostly the window's own background,
    // with enough of the book's color to tell them apart, so the title reads
    // in the theme's foreground on a light theme as well as a dark one.
    function glass(color, amount) {
        const base = theme.background
        return Qt.rgba(base.r + (color.r - base.r) * amount,
                       base.g + (color.g - base.g) * amount,
                       base.b + (color.b - base.b) * amount, 1)
    }

    function fade(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    Shortcut { sequence: "?"; enabled: !newBook.opened; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequence: "Q"; enabled: win.shelfHasKeys; onActivated: Qt.quit() }
    Shortcut { sequence: "N"; enabled: win.shelfHasKeys; onActivated: newBook.open() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: win.rightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            spacing: 16

            Label {
                objectName: "bookCount"
                text: qsTr("%Ln books", "", backend.books.length)
                font.pixelSize: 15
                font.bold: true
                font.letterSpacing: 1.5
                font.capitalization: Font.AllUppercase
                color: theme.accent
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { keys: "N", action: qsTr("New book") },
                    { keys: "?", action: qsTr("Shortcuts") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    spacing: 8

                    Label {
                        text: modelData.keys
                        color: theme.accent
                        font.bold: true
                    }
                    Label {
                        text: modelData.action
                        opacity: 0.7
                    }
                }
            }
        }

        Label {
            objectName: "error"
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            visible: backend.error !== ""
            text: backend.error
            color: theme.colors.red ? theme.colors.red : theme.accent
            wrapMode: Text.Wrap
        }

        Item {
            id: shelfArea
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 28
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            clip: true

            readonly property int columns: Math.max(1, Math.floor(width / win.cellWidth))
            readonly property int rows: Math.max(Math.ceil(backend.books.length / columns),
                                                 Math.ceil(height / win.cellHeight))

            // The shelves run the full width whether or not a book stands on
            // them, and travel with the books as the shelf scrolls.
            Repeater {
                model: shelfArea.rows
                delegate: Item {
                    required property int index
                    width: shelfArea.width
                    y: index * win.cellHeight + win.shelfLine - grid.contentY

                    // The light the shelf throws down the wall.
                    Rectangle {
                        width: parent.width
                        height: 34
                        gradient: Gradient {
                            GradientStop { position: 0; color: win.fade(theme.accent, 0.20) }
                            GradientStop { position: 1; color: win.fade(theme.accent, 0) }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 2
                        color: theme.accent
                    }
                    // Brighter at the middle, like a lit strip.
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width * 0.6
                        height: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: win.fade(theme.foreground, 0) }
                            GradientStop { position: 0.5; color: win.fade(theme.foreground, 0.75) }
                            GradientStop { position: 1; color: win.fade(theme.foreground, 0) }
                        }
                    }
                }
            }

            GridView {
                id: grid
                objectName: "shelf"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: shelfArea.columns * win.cellWidth
                cellWidth: win.cellWidth
                cellHeight: win.cellHeight
                model: backend.books
                focus: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                layoutDirection: win.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight

                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    width: win.cellWidth
                    height: win.cellHeight

                    readonly property bool current: GridView.isCurrentItem
                    readonly property color tint: win.tintFor(modelData.tint)

                    // The cover's reflection in the shelf under it.
                    Rectangle {
                        anchors.horizontalCenter: cover.horizontalCenter
                        y: win.shelfLine + 2
                        width: win.coverWidth
                        height: 30
                        gradient: Gradient {
                            GradientStop { position: 0; color: win.fade(cell.tint, cell.current ? 0.45 : 0.22) }
                            GradientStop { position: 1; color: win.fade(cell.tint, 0) }
                        }
                    }

                    Rectangle {
                        id: cover
                        objectName: "cover"
                        anchors.horizontalCenter: parent.horizontalCenter
                        // A picked book lifts off the shelf and lights up.
                        y: win.shelfLine - win.coverHeight - (cell.current ? 14 : 2)
                        width: win.coverWidth
                        height: win.coverHeight
                        radius: Math.min(4, theme.radius)
                        border.width: cell.current ? 2 : 1
                        border.color: cell.current ? cell.tint : win.fade(cell.tint, 0.55)
                        gradient: Gradient {
                            GradientStop { position: 0; color: win.glass(cell.tint, cell.current ? 0.30 : 0.14) }
                            GradientStop { position: 1; color: win.glass(cell.tint, cell.current ? 0.46 : 0.26) }
                        }

                        Behavior on y { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

                        // A lit strip along the top edge, in the book's color.
                        Rectangle {
                            x: 1
                            y: 1
                            width: parent.width - 2
                            height: 4
                            color: cell.tint
                        }

                        // The spine, as a hairline of the same light.
                        Rectangle {
                            x: 9
                            y: 5
                            width: 1
                            height: parent.height - 6
                            color: win.fade(cell.tint, 0.5)
                        }

                        Label {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 20
                            anchors.leftMargin: 20
                            anchors.rightMargin: 12
                            text: cell.modelData.title
                            color: theme.foreground
                            font.pixelSize: 15
                            font.bold: true
                            wrapMode: Text.Wrap
                            maximumLineCount: 5
                            elide: Text.ElideRight
                        }

                        // The book's place on the shelf, like a readout.
                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.bottom: author.top
                            anchors.bottomMargin: 6
                            text: String(cell.index + 1).padStart(2, "0")
                            color: cell.tint
                            font.family: "monospace"
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Label {
                            id: author
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottomMargin: 14
                            anchors.leftMargin: 20
                            anchors.rightMargin: 10
                            text: cell.modelData.author
                            color: theme.foreground
                            opacity: 0.75
                            font.pixelSize: 10
                            font.letterSpacing: 0.4
                            font.capitalization: Font.AllUppercase
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: cover
                        onClicked: grid.currentIndex = cell.index
                    }
                }
            }

            ColumnLayout {
                objectName: "empty"
                anchors.centerIn: parent
                visible: backend.books.length === 0
                spacing: 8

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Your shelf is empty")
                    font.pixelSize: 24
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Press N to write your first book")
                    opacity: 0.7
                }
            }
        }
    }

    Popup {
        id: newBook
        objectName: "newBook"
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: 24
        width: Math.min(420, win.width - 48)
        background: Rectangle {
            color: theme.background
            border.color: theme.accent
            radius: theme.radius
        }

        function create() {
            if (backend.createBook(titleField.text, authorField.text) !== "") {
                grid.currentIndex = backend.books.length - 1
                close()
            }
        }

        onOpened: {
            titleField.text = ""
            authorField.text = ""
            titleField.forceActiveFocus()
        }
        onClosed: grid.forceActiveFocus()

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 16
            LayoutMirroring.enabled: win.rightToLeft
            LayoutMirroring.childrenInherit: true

            Label {
                text: qsTr("New book")
                font.pixelSize: 20
                font.bold: true
            }

            TextField {
                id: titleField
                objectName: "titleField"
                Layout.fillWidth: true
                placeholderText: qsTr("Title")
                Material.roundedScale: theme.radius
                onAccepted: newBook.create()
            }

            TextField {
                id: authorField
                objectName: "authorField"
                Layout.fillWidth: true
                placeholderText: qsTr("Author")
                Material.roundedScale: theme.radius
                onAccepted: newBook.create()
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    Material.roundedScale: theme.radius
                    onClicked: newBook.close()
                }
                Button {
                    id: createButton
                    objectName: "createButton"
                    text: qsTr("Create")
                    highlighted: true
                    Material.roundedScale: theme.radius
                    onClicked: newBook.create()
                    // A highlighted button colors its caption by Material's
                    // own rule, so it can't read on every accent.
                    contentItem: Label {
                        text: createButton.text
                        font: createButton.font
                        color: theme.accentForeground
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    Popup {
        id: help
        objectName: "help"
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: 24
        background: Rectangle {
            color: theme.background
            border.color: theme.colors.selection
            radius: theme.radius
        }

        ColumnLayout {
            spacing: 8
            // A popup is not an Item and doesn't inherit the window's
            // mirroring, so its content opts in.
            LayoutMirroring.enabled: win.rightToLeft
            LayoutMirroring.childrenInherit: true

            Repeater {
                model: [
                    { keys: "N", action: qsTr("Write a new book") },
                    { keys: "← → ↑ ↓", action: qsTr("Move along the shelf") },
                    { keys: "?", action: qsTr("Show or hide the shortcuts") },
                    { keys: "Q", action: qsTr("Quit") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    spacing: 24

                    Label {
                        Layout.preferredWidth: 88
                        text: modelData.keys
                        color: theme.accent
                        font.bold: true
                    }
                    Label { text: modelData.action }
                }
            }
        }
    }
}
