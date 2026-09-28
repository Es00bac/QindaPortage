// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// In-window fallback menu bar rendering the ApplicationCoordinator's menu
// model — the fail-closed twin of the exported global menu (main.cpp keeps
// it visible exactly while the export is not live). Shortcut strings are
// display-only; key handling lives in Main.qml's Shortcut items.
Tk.MenuBar {
    id: bar

    // ApplicationCoordinator.menus: [{id, label, actions: [{id, label,
    // shortcut, enabled, checkable, checked, destructive, ...}]}]
    property var menusModel: []

    signal activated(string actionId)

    Instantiator {
        model: bar.menusModel

        delegate: Tk.Menu {
            id: menuDelegate
            required property var modelData
            title: modelData.label

            Instantiator {
                model: menuDelegate.modelData.actions

                delegate: Tk.MenuItem {
                    required property var modelData
                    text: modelData.label
                    shortcut: modelData.shortcut
                    checkable: modelData.checkable
                    checked: modelData.checked
                    enabled: modelData.enabled
                    danger: modelData.destructive
                    onTriggered: bar.activated(modelData.id)
                }

                onObjectAdded: function (index, object) { menuDelegate.insertItem(index, object) }
                onObjectRemoved: function (index, object) { menuDelegate.removeItem(object) }
            }
        }

        onObjectAdded: function (index, object) { bar.insertMenu(index, object) }
        onObjectRemoved: function (index, object) { bar.removeMenu(object) }
    }
}
