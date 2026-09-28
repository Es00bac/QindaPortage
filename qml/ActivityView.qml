// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Every transaction the helper ran (journal), what it merged and removed,
// and Revert for its configuration changes.
T.SplitView {
    id: view

    property var plan
    property int selected: 0
    readonly property var entries: view.plan ? view.plan.journal : []
    readonly property var entry: view.selected >= 0 && view.selected < view.entries.length ? view.entries[view.selected] : null

    orientation: Qt.Horizontal
    handle: Rectangle {
        implicitWidth: 1
        color: T.SplitHandle.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
    }

    Tk.Flex {
        T.SplitView.preferredWidth: 420
        T.SplitView.minimumWidth: 300
        direction: Tk.Flex.Column
        gap: 0

        Tk.ToolBar {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            compact: true
            Tk.Label { text: qsTr("Transactions"); font.weight: Font.DemiBold }
            Tk.Caption { text: String(view.entries.length) }
            Tk.Spacer {}
            Tk.IconButton {
                small: true
                iconName: "refresh-cw"
                tooltip: qsTr("Reload the journal")
                onClicked: view.plan.refreshJournal()
            }
        }
        Tk.DataTable {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            tooltip: qsTr("Transaction journal")
            model: view.entries
            currentIndex: view.selected
            emptyText: qsTr("No transactions yet. Applied plans appear here.")
            onActivated: function (index) { view.selected = index }
            onCurrentIndexChanged: if (currentIndex >= 0) view.selected = currentIndex
            columns: [
                Tk.TableColumn { key: "started"; title: qsTr("When"); width: 150; mono: true; sortable: false
                    formatter: function (value) { return String(value || "").replace("T", " ").replace(/\..*$/, "").replace("Z", "") } },
                Tk.TableColumn { key: "kind"; title: qsTr("Kind"); width: 60; sortable: false },
                Tk.TableColumn { key: "ok"; title: qsTr("Result"); width: 64; sortable: false
                    formatter: function (value, row) { return row.reverted_by ? qsTr("reverted") : value ? qsTr("ok") : qsTr("failed") } },
                Tk.TableColumn { key: "title"; title: qsTr("What"); flex: 1; sortable: false }
            ]
        }
    }

    Tk.Scroll {
        id: detail
        T.SplitView.fillWidth: true
        padding: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            width: detail.width - 2 * Tk.Theme.space.md

            Tk.EmptyState {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                visible: view.entry === null
                iconName: "history"
                title: qsTr("Nothing selected")
            }
            Tk.Heading {
                visible: view.entry !== null
                level: 2
                text: view.entry ? view.entry.title || view.entry.id : ""
                wrapMode: Text.Wrap
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
            Tk.Flex {
                visible: view.entry !== null
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                Tk.Badge { text: view.entry ? view.entry.id : ""; variant: "accent" }
                Tk.Badge {
                    text: view.entry && view.entry.ok ? qsTr("succeeded") : qsTr("failed")
                    variant: view.entry && view.entry.ok ? "success" : "danger"
                }
                Tk.Badge { visible: view.entry && !!view.entry.reverted_by; text: qsTr("reverted"); variant: "warning" }
            }
            Tk.Notice {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                visible: view.entry !== null && (view.entry.error || "").length > 0
                variant: "danger"
                text: view.entry ? view.entry.error || "" : ""
            }
            Tk.KeyValue { key: qsTr("Goal"); value: view.entry ? view.entry.goal || "—" : ""; visible: view.entry !== null; Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Tk.KeyValue { key: qsTr("Merged"); value: view.entry ? (view.entry.merged || []).join(", ") || "—" : ""; visible: view.entry !== null; Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Tk.KeyValue { key: qsTr("Removed"); value: view.entry ? (view.entry.removed || []).join(", ") || "—" : ""; visible: view.entry !== null; Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Tk.SectionHeader { title: qsTr("Files changed"); visible: view.entry !== null; Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Repeater {
                model: view.entry ? view.entry.touched || [] : []
                delegate: Tk.Mono {
                    required property var modelData
                    text: String(modelData)
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    elide: Text.ElideMiddle
                }
            }
            Tk.SectionHeader { title: qsTr("Commands run"); visible: view.entry !== null; Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Repeater {
                model: view.entry ? view.entry.commands || [] : []
                delegate: Tk.Mono {
                    required property var modelData
                    text: (modelData.join ? modelData.join(" ") : String(modelData))
                    wrapMode: Text.WrapAnywhere
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
            }
            Tk.Flex {
                visible: view.entry !== null && view.entry.kind === "apply" && !view.entry.reverted_by
                gap: Tk.Theme.space.sm
                Tk.Button {
                    iconName: "undo-2"
                    text: qsTr("Revert configuration…")
                    tooltip: qsTr("Restore the files this transaction changed, if nobody edited them since")
                    busy: view.plan && view.plan.applying
                    onClicked: revertDialog.open()
                }
            }
            Tk.Notice {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                visible: view.plan && view.plan.undoCommands.length > 0
                variant: "info"
                title: qsTr("To undo the packages too")
                text: view.plan ? view.plan.undoCommands.join("\n") : ""
            }
        }
    }

    Tk.MessageDialog {
        id: revertDialog
        title: qsTr("Revert this transaction's configuration?")
        variant: "warning"
        text: qsTr("Files the transaction changed are restored from its snapshot; files edited since are left alone and reported. "
                   + "Packages are not rebuilt: the commands to do that are shown afterwards.")
        primaryText: qsTr("Revert")
        secondaryText: qsTr("Cancel")
        onAccepted: view.plan.revert(view.entry.id)
    }
}
