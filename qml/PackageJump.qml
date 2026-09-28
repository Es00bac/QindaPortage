// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Toolbar search that jumps to an installed package.
Item {
    id: jump

    signal chosen(string key)

    implicitWidth: 280
    implicitHeight: field.implicitHeight

    property var matches: []

    Tk.SearchField {
        id: field
        anchors.fill: parent
        small: true
        placeholderText: qsTr("Jump to package…")
        tooltip: qsTr("Find an installed package by name")
        onSearched: function (text) {
            jump.matches = text.length < 2 ? [] : systemModel.packageRows(text, "all", "", "name", false).slice(0, 12)
            if (jump.matches.length > 0) popup.open()
            else popup.close()
        }
        onAccepted: {
            if (jump.matches.length > 0) {
                jump.chosen(jump.matches[0].key)
                popup.close()
            }
        }
    }

    Tk.Popover {
        id: popup
        anchorItem: field
        placement: "bottom"
        padding: Tk.Theme.space.xs

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            implicitWidth: 360

            Repeater {
                model: jump.matches
                delegate: Tk.ListRow {
                    required property var modelData
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    text: modelData.key
                    secondaryText: modelData.version + " · " + modelData.description
                    iconName: "package"
                    onClicked: {
                        jump.chosen(modelData.key)
                        popup.close()
                    }
                }
            }
        }
    }
}
