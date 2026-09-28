// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// "How the system is built": one stacked bar of @system, @world, their
// dependencies and orphans, with a legend. Each segment is clickable.
Item {
    id: bar

    property var stats: ({})
    signal segmentChosen(string role)

    readonly property var segments: {
        var s = bar.stats || {}
        var installed = s.installed || 0
        var deps = Math.max(0, installed - (s.system || 0) - (s.world || 0) - (s.orphans || 0))
        return [
            { role: "system", label: qsTr("@system"), count: s.system || 0, tone: Tk.Theme.color.info,
              detail: qsTr("the profile's base system") },
            { role: "world", label: qsTr("@world"), count: s.world || 0, tone: Tk.Theme.color.accent,
              detail: qsTr("what you asked for (world file %1 + profile set %2)").arg(s.worldFile || 0).arg(s.profileSet || 0) },
            { role: "dependency", label: qsTr("dependencies"), count: deps, tone: Tk.Theme.color.textMuted,
              detail: qsTr("pulled in by the above") },
            { role: "orphan", label: qsTr("orphans"), count: s.orphans || 0, tone: Tk.Theme.color.warning,
              detail: qsTr("nothing needs them; depclean would remove them") }
        ]
    }
    readonly property int total: Math.max(1, (bar.stats || {}).installed || 1)

    implicitHeight: column.implicitHeight

    Tk.Flex {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Item {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            implicitHeight: Tk.Theme.size.control
            Row {
                anchors.fill: parent
                spacing: 2
                Repeater {
                    model: bar.segments
                    delegate: Tk.Box {
                        required property var modelData
                        width: Math.max(modelData.count > 0 ? 4 : 0,
                                        (bar.width - 6) * modelData.count / bar.total)
                        height: parent.height
                        radius: Tk.Theme.radius.xs
                        color: Qt.alpha(modelData.tone, hovered ? 0.85 : 0.6)
                        interactive: true
                        tooltip: qsTr("%1: %2 packages — %3").arg(modelData.label).arg(modelData.count).arg(modelData.detail)
                        onClicked: bar.segmentChosen(modelData.role)
                    }
                }
            }
        }
        Tk.Flex {
            gap: Tk.Theme.space.lg
            wrap: Tk.Flex.Wrap
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Repeater {
                model: bar.segments
                delegate: Tk.Flex {
                    required property var modelData
                    gap: Tk.Theme.space.xs
                    align: Tk.Flex.Center
                    Rectangle {
                        implicitWidth: Tk.Theme.space.sm
                        implicitHeight: Tk.Theme.space.sm
                        radius: Tk.Theme.radius.xs
                        color: modelData.tone
                    }
                    Tk.Label { text: modelData.label }
                    Tk.Mono { text: String(modelData.count); color: Tk.Theme.color.textMuted }
                }
            }
        }
    }
}
