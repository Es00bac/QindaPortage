// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Squarified treemap of installed size (layout from SystemModel.treemap):
// categories at the top level, a category's packages after a click.
Item {
    id: map

    property var system
    property string category: ""
    readonly property var tiles: map.width > 0 && map.height > 0 && map.system && map.system.revision >= 0
                                 ? map.system.treemap(map.category, map.width, map.height) : []

    signal categoryChosen(string category)
    signal packageChosen(string key)

    Repeater {
        model: map.tiles
        delegate: Tk.Box {
            id: tile
            required property var modelData
            x: modelData.x + 1
            y: modelData.y + 1
            width: Math.max(0, modelData.w - 2)
            height: Math.max(0, modelData.h - 2)
            radius: Tk.Theme.radius.xs
            // Larger tiles are lit more strongly; all shades derive from the accent.
            color: Qt.alpha(Tk.Theme.color.accent, 0.10 + 0.45 * Math.min(1, modelData.weight))
            borderColor: tile.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
            borderWidth: 1
            interactive: true
            clipContent: true
            tooltip: map.category.length > 0
                     ? qsTr("%1 — %2").arg(modelData.id).arg(modelData.size)
                     : qsTr("%1 — %2 in %3 packages").arg(modelData.label).arg(modelData.size).arg(modelData.count)
            onClicked: {
                if (map.category.length > 0) map.packageChosen(modelData.id)
                else map.categoryChosen(modelData.id)
            }

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                visible: tile.width > 56 && tile.height > 30
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.xs

                Tk.Label {
                    text: tile.modelData.label
                    font.pixelSize: Tk.Theme.font.small
                    color: Tk.Theme.color.text
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
                Tk.Caption {
                    text: tile.modelData.size
                    visible: tile.height > 44
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
            }
        }
    }
}
