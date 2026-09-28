// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// How Portage is configured here: the resolved make.conf variables, the
// profile, repositories and categories.
Tk.Scroll {
    id: view

    property var system

    signal askRequested(string text)

    padding: Tk.Theme.space.lg

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.lg
        width: view.width - 2 * Tk.Theme.space.lg

        Tk.Panel {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            title: qsTr("Resolved settings (portageq envvar)")
            iconName: "sliders-horizontal"
            collapsible: false
            actions: Tk.Button {
                small: true
                variant: "ghost"
                iconName: "sparkles"
                text: qsTr("Review")
                tooltip: qsTr("Ask the assistant to review these settings for this machine")
                onClicked: view.askRequested(qsTr("Review my make.conf settings (MAKEOPTS, EMERGE_DEFAULT_OPTS, FEATURES, CFLAGS, USE) for this laptop and suggest improvements, without changing anything."))
            }

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                width: parent.width
                Repeater {
                    model: view.system ? view.system.environment : []
                    delegate: Tk.Flex {
                        required property var modelData
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        gap: Tk.Theme.space.md
                        align: Tk.Flex.Start
                        Tk.Mono {
                            text: modelData.name
                            color: Tk.Theme.color.accentText
                            Tk.Flex.basis: 190
                            Tk.Flex.shrink: 0
                        }
                        Tk.Mono {
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            text: modelData.value.length > 0 ? modelData.value : "—"
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: modelData.name === "USE" ? 6 : 3
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Tk.Panel {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            title: qsTr("Installed packages by category")
            iconName: "list-tree"
            collapsible: false
            Tk.Flex {
                width: parent.width
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                Repeater {
                    model: view.system ? view.system.categories : []
                    delegate: Tk.Chip {
                        required property var modelData
                        small: true
                        text: qsTr("%1 %2").arg(modelData.name).arg(modelData.count)
                        tooltip: view.system.formatBytes(modelData.bytes)
                    }
                }
            }
        }
    }
}
