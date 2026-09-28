// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The dependency neighbourhood of the selected package, with controls for
// depth, direction and edge types.
Tk.Flex {
    id: view

    property var system
    property string selectedKey
    property int depth: 1
    property int mode: 1   // 0 deps, 1 both, 2 reverse
    property bool showBuild: true
    property bool showRun: true
    property bool showPost: true

    readonly property int edgeTypes: (view.showBuild ? 1 : 0) | (view.showRun ? 2 : 0) | (view.showPost ? 4 : 0) | 8
    readonly property var layout: view.system && view.system.revision >= 0 && view.selectedKey.length > 0
        ? view.system.neighbourhood(view.selectedKey, view.depth, view.mode !== 2, view.mode !== 0,
                                    view.edgeTypes, 160)
        : ({ nodes: [], edges: [] })

    signal packageChosen(string key)
    signal detailsRequested(string key)

    direction: Tk.Flex.Column
    gap: 0

    Tk.ToolBar {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        compact: true
        wrap: true

        Tk.Label {
            text: view.selectedKey.length > 0 ? view.selectedKey : qsTr("No package selected")
            font.weight: Font.DemiBold
        }
        Tk.Caption {
            text: view.layout.nodes ? qsTr("%1 packages, %2 edges%3").arg(view.layout.nodes.length)
                                      .arg(view.layout.edges.length).arg(view.layout.truncated ? qsTr(" (capped)") : "")
                                    : ""
        }
        Tk.Spacer {}
        Tk.Segmented {
            objectName: "graphDirection"
            small: true
            model: [
                { text: qsTr("Depends on"), iconName: "arrow-right", tooltip: qsTr("What this package needs") },
                { text: qsTr("Both"), iconName: "arrow-left-right" },
                { text: qsTr("Needed by"), iconName: "arrow-left", tooltip: qsTr("What needs this package") }
            ]
            currentIndex: view.mode
            onActivated: function (index) { view.mode = index }
        }
        Tk.Segmented {
            objectName: "graphDepth"
            small: true
            model: [{ text: "1", tooltip: qsTr("Direct only") }, { text: "2" }, { text: "3" }]
            currentIndex: view.depth - 1
            onActivated: function (index) { view.depth = index + 1 }
        }
        Tk.CheckBox { small: true; text: qsTr("run"); checked: view.showRun; onToggled: view.showRun = checked }
        Tk.CheckBox { small: true; text: qsTr("build"); checked: view.showBuild; onToggled: view.showBuild = checked }
        Tk.CheckBox { small: true; text: qsTr("post"); checked: view.showPost; onToggled: view.showPost = checked }
    }

    Item {
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch

        DependencyGraphView {
            anchors.fill: parent
            layout: view.layout
            onNodeChosen: function (key) { view.packageChosen(key) }
            onNodeOpened: function (key) { view.detailsRequested(key) }
        }
        Tk.EmptyState {
            anchors.fill: parent
            visible: view.selectedKey.length === 0
            iconName: "share-2"
            title: qsTr("Choose a package")
            text: qsTr("Use the search in the toolbar or pick one on the Packages page.")
        }
        // Legend.
        Tk.Island {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Tk.Theme.space.md
            Repeater {
                model: [
                    { label: qsTr("world"), tone: Tk.Theme.color.accent },
                    { label: qsTr("system"), tone: Tk.Theme.color.info },
                    { label: qsTr("dependency"), tone: Tk.Theme.color.textMuted },
                    { label: qsTr("orphan"), tone: Tk.Theme.color.warning }
                ]
                delegate: Tk.Flex {
                    required property var modelData
                    gap: Tk.Theme.space.xs
                    align: Tk.Flex.Center
                    Rectangle { implicitWidth: 3; implicitHeight: 12; radius: 1; color: modelData.tone }
                    Tk.Caption { text: modelData.label }
                }
            }
        }
    }
}
