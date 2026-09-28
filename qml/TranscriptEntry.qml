// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One transcript entry (AgentSession.transcript): user, assistant, tool,
// note or error.
Item {
    id: entry

    required property var modelData
    readonly property string kind: entry.modelData.kind
    property bool expanded: false

    implicitHeight: body.implicitHeight
    implicitWidth: body.implicitWidth

    Tk.Box {
        id: body
        width: entry.kind === "user" ? Math.min(entry.width * 0.85, userText.implicitWidth + 2 * Tk.Theme.space.sm + 2)
                                     : entry.width
        anchors.right: entry.kind === "user" ? parent.right : undefined
        padding: entry.kind === "tool" ? Tk.Theme.space.xs : Tk.Theme.space.sm
        radius: Tk.Theme.radius.md
        color: entry.kind === "user" ? Tk.Theme.color.accentSubtle
               : entry.kind === "assistant" ? Tk.Theme.color.panelAlt
               : entry.kind === "error" ? Tk.Theme.color.dangerSubtle
               : "transparent"
        borderColor: entry.kind === "tool" ? Tk.Theme.color.border : "transparent"
        borderWidth: entry.kind === "tool" ? 1 : 0
        interactive: entry.kind === "tool"
        tooltip: entry.kind === "tool" ? qsTr("Show the tool's arguments and result") : ""
        onClicked: entry.expanded = !entry.expanded

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs

            Tk.Label {
                id: userText
                visible: entry.kind === "user"
                text: entry.modelData.text || ""
                wrapMode: Text.Wrap
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
            Tk.Label {
                visible: entry.kind === "assistant"
                text: entry.modelData.text || ""
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                selectable: true
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                onLinkActivated: function (link) { Qt.openUrlExternally(link) }
            }
            Tk.Flex {
                visible: entry.kind === "tool"
                gap: Tk.Theme.space.xs
                align: Tk.Flex.Center
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                Tk.Icon {
                    name: entry.modelData.plan ? "list-plus" : "wrench"
                    size: Tk.Theme.size.iconSm
                    color: entry.modelData.ok === false ? Tk.Theme.color.danger
                           : entry.modelData.plan ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                }
                Tk.Mono {
                    text: entry.modelData.tool || ""
                    color: entry.modelData.plan ? Tk.Theme.color.accentText : Tk.Theme.color.text
                }
                Tk.Mono {
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    text: entry.modelData.args || ""
                    color: Tk.Theme.color.textMuted
                    elide: Text.ElideRight
                }
                Tk.Icon {
                    name: entry.expanded ? "chevron-up" : "chevron-down"
                    size: Tk.Theme.size.iconSm
                    color: Tk.Theme.color.textMuted
                }
            }
            Tk.Mono {
                visible: entry.kind === "tool" && entry.expanded
                text: entry.modelData.text || ""
                wrapMode: Text.WrapAnywhere
                color: Tk.Theme.color.textMuted
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
            Tk.Caption {
                visible: entry.kind === "note" || entry.kind === "error"
                text: entry.modelData.text || ""
                wrapMode: Text.Wrap
                color: entry.kind === "error" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
        }
    }
}
