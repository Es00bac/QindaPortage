// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "Format.js" as Format

// The preview as a list: problem cards with candidate fixes first, then
// what triggers rebuilds, then the merge table.
Tk.Flex {
    id: list

    property var problems: []
    property var merges: []
    property var rebuildCauses: []
    property bool previewed: false

    signal askRequested(string text)
    signal packageChosen(string key)

    direction: Tk.Flex.Column
    gap: 0

    Tk.Scroll {
        id: problemScroll
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: list.problems.length > 0 || list.rebuildCauses.length > 0
        implicitHeight: Math.min(problemColumn.implicitHeight + 2 * Tk.Theme.space.sm, list.height * 0.5)
        padding: Tk.Theme.space.sm

        Tk.Flex {
            id: problemColumn
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            width: problemScroll.width - 2 * Tk.Theme.space.sm

            Repeater {
                model: list.problems
                delegate: Tk.Notice {
                    id: problem
                    required property var modelData
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    variant: "danger"
                    title: modelData.kind
                    text: modelData.title + (modelData.fixes.length > 0
                        ? "\n" + modelData.fixes.slice(0, 4).map(function (f) {
                              return "• " + f.kind + " " + f.atom + " " + f.values + (f.reason.length > 0 ? " (" + f.reason + ")" : "")
                          }).join("\n") : "")
                    actions: Tk.Button {
                        small: true
                        iconName: "sparkles"
                        text: qsTr("Fix it")
                        onClicked: list.askRequested(qsTr("The preview reports a %1 problem: %2. Find a fix, verify it with pretend, and update the plan.")
                                                     .arg(problem.modelData.kind).arg(problem.modelData.title))
                    }
                }
            }
            Repeater {
                model: list.rebuildCauses
                delegate: Tk.Notice {
                    required property string modelData
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    variant: "info"
                    text: modelData
                }
            }
        }
    }

    Tk.DataTable {
        objectName: "impactTable"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        tooltip: qsTr("Packages the plan would merge")
        model: list.merges
        emptyText: list.previewed ? qsTr("Nothing would be merged.") : qsTr("Run Preview to see the impact.")
        onActivated: function (index) { list.packageChosen(list.merges[index].key) }

        columns: [
            Tk.TableColumn { key: "action"; title: qsTr("Action"); width: 86; sortable: false },
            Tk.TableColumn { key: "key"; title: qsTr("Package"); flex: 2; minWidth: 160; sortable: false },
            Tk.TableColumn { key: "version"; title: qsTr("Version"); width: 120; mono: true; sortable: false },
            Tk.TableColumn { key: "from"; title: qsTr("From"); width: 110; mono: true; muted: true; sortable: false },
            Tk.TableColumn { key: "binary"; title: qsTr("Build"); width: 64; sortable: false
                formatter: function (value) { return value ? qsTr("binary") : qsTr("source") } },
            Tk.TableColumn { key: "useChanges"; title: qsTr("USE changes"); flex: 1; mono: true; sortable: false },
            Tk.TableColumn { key: "repo"; title: qsTr("Repo"); width: 90; muted: true; sortable: false },
            Tk.TableColumn { key: "download"; title: qsTr("Download"); width: 90; align: Qt.AlignRight; mono: true; sortable: false }
        ]
    }
}
