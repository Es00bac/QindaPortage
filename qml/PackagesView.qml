// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "Format.js" as Format

// Every installed package: a filterable, sortable table and the inspector
// for the selected one.
Item {
    id: view

    property var system
    property string selectedKey
    property string filterText: ""
    property string role: "all"
    property string repo: ""
    property string sortKey: "name"
    property bool descending: false

    readonly property var rows: view.system && view.system.revision >= 0
        ? view.system.packageRows(view.filterText, view.role, view.repo, view.sortKey, view.descending) : []

    signal packageChosen(string key)
    signal graphRequested(string key)
    signal askRequested(string text)

    T.SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 1
            color: T.SplitHandle.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
        }

        Tk.Flex {
            T.SplitView.fillWidth: true
            T.SplitView.minimumWidth: 360
            direction: Tk.Flex.Column
            gap: 0

            Tk.ToolBar {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                compact: true
                wrap: true

                Tk.SearchField {
                    objectName: "packageFilter"
                    small: true
                    Tk.Flex.basis: 220
                    Tk.Flex.shrink: 1
                    placeholderText: qsTr("Filter name or description")
                    tooltip: qsTr("Filter the installed packages")
                    onSearched: function (text) { view.filterText = text }
                }
                Tk.Segmented {
                    objectName: "roleFilter"
                    small: true
                    model: [
                        { text: qsTr("All"), value: "all" },
                        { text: qsTr("World"), value: "world" },
                        { text: qsTr("System"), value: "system" },
                        { text: qsTr("Deps"), value: "dependency" },
                        { text: qsTr("Orphans"), value: "orphan" }
                    ]
                    currentIndex: ["all", "world", "system", "dependency", "orphan"].indexOf(view.role)
                    onActivated: function (index) { view.role = model[index].value }
                }
                Tk.ComboBox {
                    objectName: "repoFilter"
                    small: true
                    implicitWidth: 150
                    tooltip: qsTr("Only packages from this repository")
                    model: [qsTr("All repositories")].concat((view.system ? view.system.repositories : []).map(function (r) { return r.name }))
                    onActivated: function (index) { view.repo = index === 0 ? "" : model[index] }
                }
                Tk.Spacer {}
                Tk.Caption { text: qsTr("%1 shown").arg(view.rows.length) }
            }

            Tk.DataTable {
                id: table
                objectName: "packageTable"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                tooltip: qsTr("Installed packages")
                model: view.rows
                sortKey: view.sortKey
                sortOrder: view.descending ? Qt.DescendingOrder : Qt.AscendingOrder
                emptyText: view.system && view.system.loading ? qsTr("Loading…") : qsTr("No package matches")
                currentIndex: {
                    for (var i = 0; i < view.rows.length; ++i) if (view.rows[i].key === view.selectedKey) return i
                    return -1
                }
                onSortRequested: function (key, order) {
                    view.sortKey = key
                    view.descending = order === Qt.DescendingOrder
                }
                onActivated: function (index) { view.packageChosen(view.rows[index].key) }
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < view.rows.length && view.rows[currentIndex].key !== view.selectedKey)
                        view.packageChosen(view.rows[currentIndex].key)
                }

                Tk.TableColumn { key: "name"; title: qsTr("Package"); flex: 2; minWidth: 160
                    formatter: function (value, row) { return row.key } }
                Tk.TableColumn { key: "version"; title: qsTr("Version"); width: 120; mono: true }
                Tk.TableColumn { key: "repo"; title: qsTr("Repo"); width: 90; muted: true }
                Tk.TableColumn { key: "role"; title: qsTr("Role"); width: 84; sortable: false
                    formatter: function (value) { return Format.roleLabel(value) } }
                Tk.TableColumn { key: "size"; title: qsTr("Size"); width: 80; align: Qt.AlignRight; mono: true; descendingFirst: true }
                Tk.TableColumn { key: "rdeps"; title: qsTr("Needed by"); width: 76; align: Qt.AlignRight; mono: true; descendingFirst: true }
                Tk.TableColumn { key: "installed"; title: qsTr("Installed"); width: 92; mono: true; muted: true; descendingFirst: true }
                Tk.TableColumn { key: "description"; title: qsTr("Description"); flex: 3; muted: true; sortable: false }
            }
        }

        PackageInspector {
            T.SplitView.preferredWidth: 420
            T.SplitView.minimumWidth: 320
            system: view.system
            packageKey: view.selectedKey
            onPackageChosen: function (key) { view.packageChosen(key) }
            onGraphRequested: function (key) { view.graphRequested(key) }
            onAskRequested: function (text) { view.askRequested(text) }
        }
    }
}
