// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "Format.js" as Format

// One installed package: identity, why it is installed, USE flags with the
// place each value comes from, dependencies both ways, and its files.
Tk.Box {
    id: inspector

    property var system
    property string packageKey
    property int tab: 0

    readonly property var detail: inspector.system && inspector.system.revision >= 0 && inspector.packageKey.length > 0
                                  ? inspector.system.packageDetail(inspector.packageKey) : ({})
    readonly property bool valid: inspector.detail.key !== undefined
    readonly property var flags: inspector.valid ? inspector.system.useFlags(inspector.packageKey) : []
    readonly property var why: inspector.valid ? inspector.system.whyInstalled(inspector.packageKey) : []

    signal packageChosen(string key)
    signal graphRequested(string key)
    signal askRequested(string text)

    color: Tk.Theme.color.panel
    borderLeft: 1
    borderColor: Tk.Theme.color.border

    Tk.EmptyState {
        anchors.fill: parent
        visible: !inspector.valid
        iconName: "package"
        title: qsTr("No package selected")
        text: qsTr("Pick a package in the table, the graph or the toolbar search.")
    }

    Tk.Scroll {
        anchors.fill: parent
        visible: inspector.valid
        padding: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            width: inspector.width - 2 * Tk.Theme.space.md

            // Identity.
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex.alignSelf: Tk.Flex.Stretch

                Tk.Heading {
                    objectName: "inspectorTitle"
                    level: 2
                    text: inspector.detail.key || ""
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    elide: Text.ElideMiddle
                }
                Tk.Flex {
                    gap: Tk.Theme.space.xs
                    wrap: Tk.Flex.Wrap
                    Tk.Badge { text: inspector.detail.version || ""; variant: "accent" }
                    Tk.Badge { text: qsTr("slot %1").arg(inspector.detail.slot || "") }
                    Tk.Badge { text: "::" + (inspector.detail.repo || "") }
                    Tk.Badge {
                        text: Format.roleLabel(inspector.detail.role || "")
                        variant: Format.roleVariant(inspector.detail.role || "")
                    }
                    Tk.Badge { text: qsTr("EAPI %1").arg(inspector.detail.eapi || "?") }
                }
                Tk.Label {
                    text: inspector.detail.description || ""
                    wrapMode: Text.WordWrap
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
                Tk.Caption {
                    text: qsTr("%1 · installed %2 · %3").arg(inspector.detail.size || "").arg(inspector.detail.installed || "")
                          .arg(inspector.detail.license || "")
                    wrapMode: Text.WordWrap
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
                Tk.Flex {
                    gap: Tk.Theme.space.xs
                    wrap: Tk.Flex.Wrap
                    Tk.Button {
                        small: true
                        iconName: "share-2"
                        text: qsTr("Graph")
                        tooltip: qsTr("Show its dependency neighbourhood")
                        onClicked: inspector.graphRequested(inspector.packageKey)
                    }
                    Tk.Button {
                        small: true
                        iconName: "sparkles"
                        text: qsTr("Explain")
                        tooltip: qsTr("Ask the assistant what this package does here and what depends on it")
                        onClicked: inspector.askRequested(qsTr("Explain %1 on this system: why it is installed, what depends on it, and whether its USE flags make sense.").arg(inspector.packageKey))
                    }
                    Tk.Button {
                        small: true
                        variant: "ghost"
                        iconName: "trash-2"
                        text: qsTr("Remove…")
                        tooltip: qsTr("Plan its removal; the assistant checks what would break first")
                        onClicked: inspector.askRequested(qsTr("Plan removing %1 safely. Check reverse dependencies first and tell me what would break.").arg(inspector.packageKey))
                    }
                    Tk.Button {
                        small: true
                        variant: "ghost"
                        iconName: "external-link"
                        text: qsTr("Homepage")
                        visible: (inspector.detail.homepage || "").length > 0
                        tooltip: inspector.detail.homepage || ""
                        onClicked: Qt.openUrlExternally(String(inspector.detail.homepage).split(" ")[0])
                    }
                }
            }

            // Why is it installed?
            Tk.SectionHeader { title: qsTr("Why it is installed"); Tk.Flex.alignSelf: Tk.Flex.Stretch }
            Tk.Flex {
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                Tk.Flex.alignSelf: Tk.Flex.Stretch

                Tk.Caption {
                    visible: inspector.why.length === 0
                    text: qsTr("Nothing needs it: an orphan that depclean would remove.")
                }
                Repeater {
                    model: inspector.why
                    delegate: Tk.Flex {
                        id: step
                        required property var modelData
                        required property int index
                        gap: Tk.Theme.space.xs
                        align: Tk.Flex.Center
                        Tk.Icon {
                            visible: step.index > 0
                            name: "chevron-right"
                            size: Tk.Theme.size.iconSm
                            color: Tk.Theme.color.textMuted
                        }
                        Tk.Chip {
                            small: true
                            text: step.modelData.key
                            active: step.modelData.key === inspector.packageKey
                            tooltip: step.index === 0 ? qsTr("in @%1").arg(step.modelData.role) : qsTr("needed by the previous package")
                            onClicked: inspector.packageChosen(step.modelData.key)
                        }
                    }
                }
            }

            // USE flags.
            Tk.SectionHeader {
                title: qsTr("USE flags")
                count: String(inspector.flags.length)
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
            Tk.Flex {
                objectName: "useFlags"
                gap: Tk.Theme.space.xs
                wrap: Tk.Flex.Wrap
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                Tk.Caption { visible: inspector.flags.length === 0; text: qsTr("No USE flags.") }
                Repeater {
                    model: inspector.flags
                    delegate: Tk.Chip {
                        required property var modelData
                        small: true
                        active: modelData.enabled
                        emphasis: modelData.origin === "package.use" ? "tinted" : "plain"
                        iconName: modelData.locked ? "lock" : ""
                        text: (modelData.enabled ? "+" : "−") + modelData.flag
                        tooltip: qsTr("%1 — set by %2\n%3\nClick to ask about changing it.")
                                 .arg(modelData.enabled ? qsTr("enabled") : qsTr("disabled"))
                                 .arg(Format.originLabel(modelData.origin)).arg(modelData.where)
                        onClicked: inspector.askRequested(qsTr("What would change if I %1 the USE flag %2 on %3? Preview it.")
                                                          .arg(modelData.enabled ? qsTr("disable") : qsTr("enable"))
                                                          .arg(modelData.flag).arg(inspector.packageKey))
                    }
                }
            }
            Tk.Caption {
                text: qsTr("Lit = enabled. Tinted = set in your package.use. Lock = forced or masked by the profile.")
                wrapMode: Text.WordWrap
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }

            Tk.TabStrip {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                model: [qsTr("Depends on (%1)").arg(inspector.detail.deps || 0),
                        qsTr("Needed by (%1)").arg(inspector.detail.rdeps || 0),
                        qsTr("Files")]
                currentIndex: inspector.tab
                onTabActivated: function (index) { inspector.tab = index }
            }

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                visible: inspector.tab < 2
                Repeater {
                    model: inspector.valid && inspector.tab < 2 ? inspector.system.dependencies(inspector.packageKey, inspector.tab === 1) : []
                    delegate: Tk.ListRow {
                        required property var modelData
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        small: true
                        text: modelData.key
                        secondaryText: modelData.types + (modelData.conditions.length > 0 ? qsTr(" · if %1").arg(modelData.conditions) : "")
                                       + (modelData.anyOf ? qsTr(" · one of several") : "")
                        tooltip: modelData.atom
                        iconName: modelData.role === "world" ? "list-checks" : "package"
                        onClicked: inspector.packageChosen(modelData.key)
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                visible: inspector.tab === 2
                Repeater {
                    model: inspector.valid && inspector.tab === 2 ? inspector.system.files(inspector.packageKey, 400) : []
                    delegate: Tk.Mono {
                        required property var modelData
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        text: modelData.link.length > 0 ? modelData.path + " → " + modelData.link : modelData.path
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
