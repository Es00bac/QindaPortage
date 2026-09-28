// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "Format.js" as Format

// The draft plan and its impact: the steps with their rationale, the exact
// config lines that will be written, the virtual emerge --pretend result
// (merges, problems with candidate fixes, the plan drawn on the graph),
// and Apply.
T.SplitView {
    id: view

    property var plan
    property var system
    property var runtime
    property int previewTab: 0

    signal askRequested(string text)
    signal packageChosen(string key)

    readonly property var preview: view.plan ? view.plan.preview : ({})
    readonly property var merges: view.preview.merges || []
    readonly property var problems: view.preview.problems || []
    readonly property string focusKey: {
        var steps = view.plan ? view.plan.steps : []
        for (var i = 0; i < steps.length; ++i) {
            if (steps[i].kind === "emerge" && steps[i].values.length > 0) return String(steps[i].values[0]).replace(/^[<>=~]+/, "").replace(/-[0-9].*$/, "").replace(/:.*$/, "")
            if (steps[i].atom.length > 0) return String(steps[i].atom).replace(/^[<>=~]+/, "").replace(/-[0-9].*$/, "").replace(/:.*$/, "")
        }
        return ""
    }

    orientation: Qt.Horizontal
    handle: Rectangle {
        implicitWidth: 1
        color: T.SplitHandle.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
    }

    // Left: the plan.
    Tk.Box {
        T.SplitView.preferredWidth: 440
        T.SplitView.minimumWidth: 340
        color: Tk.Theme.color.panel
        borderRight: 1
        borderColor: Tk.Theme.color.border

        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: 0

            Tk.ToolBar {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                compact: true
                Tk.Icon { name: "list-checks"; color: Tk.Theme.color.accent; size: Tk.Theme.size.iconSm }
                Tk.Label { text: qsTr("Plan"); font.weight: Font.DemiBold }
                Tk.Caption { text: Format.plural(view.plan ? view.plan.stepCount : 0, qsTr("step"), qsTr("steps")) }
                Tk.Spacer {}
                Tk.Button {
                    small: true
                    variant: "ghost"
                    iconName: "trash-2"
                    text: qsTr("Discard")
                    enabled: view.plan && view.plan.stepCount > 0 && !view.plan.applying
                    onClicked: view.plan.clear()
                }
            }

            Tk.Scroll {
                id: stepScroll
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                padding: Tk.Theme.space.sm

                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    width: stepScroll.width - 2 * Tk.Theme.space.sm

                    Tk.EmptyState {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        visible: !view.plan || view.plan.stepCount === 0
                        iconName: "list-checks"
                        title: qsTr("No plan yet")
                        text: qsTr("Ask the assistant for a change. Its steps land here for you to review, preview and apply.")
                    }
                    Tk.Label {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        visible: view.plan && view.plan.goal.length > 0
                        text: view.plan ? "“" + view.plan.goal + "”" : ""
                        wrapMode: Text.Wrap
                        font.italic: true
                    }
                    Repeater {
                        model: view.plan ? view.plan.steps : []
                        delegate: Tk.Card {
                            id: stepCard
                            required property var modelData
                            required property int index
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            padding: Tk.Theme.space.sm
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: Tk.Theme.space.xs
                                Tk.Flex {
                                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                                    gap: Tk.Theme.space.xs
                                    align: Tk.Flex.Center
                                    Tk.Badge {
                                        text: String(stepCard.index + 1)
                                        variant: stepCard.modelData.build ? "accent" : "info"
                                    }
                                    Tk.Label {
                                        Tk.Flex.grow: 1
                                        Tk.Flex.basis: 0
                                        text: stepCard.modelData.text
                                        wrapMode: Text.Wrap
                                        font.weight: Font.DemiBold
                                    }
                                    Tk.IconButton {
                                        small: true
                                        iconName: "x"
                                        tooltip: qsTr("Remove this step")
                                        enabled: !view.plan.applying
                                        onClicked: view.plan.removeStep(stepCard.index)
                                    }
                                }
                                Tk.Caption {
                                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                                    text: stepCard.modelData.rationale
                                    wrapMode: Text.Wrap
                                }
                                Tk.Button {
                                    small: true
                                    variant: "ghost"
                                    iconName: "file-text"
                                    text: qsTr("Show %1").arg(stepCard.modelData.kind === "patch" ? qsTr("patch") : qsTr("file"))
                                    visible: stepCard.modelData.hasContent
                                    onClicked: {
                                        contentDialog.title = stepCard.modelData.path
                                        contentDialog.body = view.plan.stepContent(stepCard.index)
                                        contentDialog.open()
                                    }
                                }
                            }
                        }
                    }
                    Tk.SectionHeader {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        visible: view.plan && view.plan.configPreview.length > 0
                        title: qsTr("Written to /etc/portage")
                    }
                    Repeater {
                        model: view.plan ? view.plan.configPreview : []
                        delegate: Tk.Box {
                            required property string modelData
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            padding: Tk.Theme.space.sm
                            radius: Tk.Theme.radius.sm
                            color: Tk.Theme.color.canvas
                            Tk.Mono { text: parent.modelData; wrapMode: Text.WrapAnywhere; width: parent.width }
                        }
                    }
                }
            }

            // Actions.
            Tk.Box {
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                padding: Tk.Theme.space.sm
                borderTop: 1
                borderColor: Tk.Theme.color.border
                color: Tk.Theme.color.surface
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Caption {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        wrapMode: Text.Wrap
                        text: view.plan && view.plan.applying ? qsTr("Applying: %1").arg(view.plan.applyStep)
                              : view.plan && view.plan.previewCurrent
                                ? (view.preview.resolved ? qsTr("The preview resolves cleanly. Applying unloads the model first so the build gets the memory.")
                                                         : qsTr("The preview has problems; apply is still possible but will likely fail."))
                              : qsTr("Preview the plan before applying it.")
                    }
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        Tk.Button {
                            objectName: "previewButton"
                            iconName: "search"
                            text: qsTr("Preview")
                            busy: view.plan && view.plan.previewing
                            available: view.plan && view.plan.stepCount > 0 && !view.plan.applying
                            onClicked: view.plan.runPreview()
                        }
                        Tk.Button {
                            objectName: "applyButton"
                            variant: "accent"
                            iconName: "play"
                            text: qsTr("Apply…")
                            busy: view.plan && view.plan.applying
                            available: view.plan && view.plan.stepCount > 0 && view.plan.previewCurrent
                            onClicked: applyDialog.open()
                        }
                    }
                }
            }
        }
    }

    // Right: the impact.
    Tk.Flex {
        T.SplitView.fillWidth: true
        direction: Tk.Flex.Column
        gap: 0

        Tk.ToolBar {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            compact: true
            wrap: true
            Tk.Label { text: qsTr("Impact"); font.weight: Font.DemiBold }
            Tk.Spinner { size: 12; visible: view.plan && view.plan.previewing }
            Tk.Caption { visible: view.plan && view.plan.previewing; text: view.plan ? view.plan.previewStage : "" }
            Repeater {
                model: view.preview.total !== undefined ? [
                    { text: qsTr("%1 merges").arg(view.preview.total), variant: "default" },
                    { text: qsTr("%1 new").arg(view.preview["new"]), variant: "success" },
                    { text: qsTr("%1 upgrades").arg(view.preview.upgrades), variant: "accent" },
                    { text: qsTr("%1 rebuilds").arg(view.preview.rebuilds), variant: "warning" },
                    { text: qsTr("%1 downgrades").arg(view.preview.downgrades), variant: "danger" },
                    { text: qsTr("%1 binary").arg(view.preview.binaries), variant: "info" }
                ] : []
                delegate: Tk.Badge {
                    required property var modelData
                    text: modelData.text
                    variant: modelData.variant
                }
            }
            Tk.Spacer {}
            Tk.Segmented {
                small: true
                model: [qsTr("List"), qsTr("Graph"), qsTr("Log")]
                currentIndex: view.previewTab
                onActivated: function (index) { view.previewTab = index }
            }
        }

        Tk.Notice {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: view.preview.error !== undefined
            variant: "danger"
            text: view.preview.error || ""
        }

        Item {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch

            ImpactList {
                anchors.fill: parent
                visible: view.previewTab === 0
                problems: view.problems
                merges: view.merges
                rebuildCauses: view.preview.rebuildCauses || []
                previewed: view.preview.total !== undefined
                onAskRequested: function (text) { view.askRequested(text) }
                onPackageChosen: function (key) { view.packageChosen(key) }
            }
            DependencyGraphView {
                anchors.fill: parent
                visible: view.previewTab === 1
                layout: view.system && view.focusKey.length > 0 && view.system.revision >= 0
                        ? view.system.neighbourhood(view.focusKey, 2, true, false, 3, 120) : ({ nodes: [], edges: [] })
                highlights: view.plan ? view.plan.highlights : ({})
                onNodeChosen: function (key) { view.packageChosen(key) }
            }
            Tk.Scroll {
                id: logScroll
                anchors.fill: parent
                visible: view.previewTab === 2
                padding: Tk.Theme.space.sm
                Tk.Mono {
                    width: logScroll.width - 2 * Tk.Theme.space.sm
                    text: view.plan && view.plan.applyLog.length > 0 ? view.plan.applyLog.join("\n") : qsTr("No build output yet.")
                    wrapMode: Text.WrapAnywhere
                    onTextChanged: Qt.callLater(logScroll.scrollToBottom)
                }
            }
        }
    }

    Tk.MessageDialog {
        id: applyDialog
        title: qsTr("Apply the plan?")
        variant: "question"
        text: qsTr("%1 will be applied as root through the QindaPortage helper. It snapshots /etc/portage, the world file and "
                   + "the local overlay first, so the configuration can be reverted from Activity. The local model is unloaded "
                   + "before building.").arg(Format.plural(view.plan ? view.plan.stepCount : 0, qsTr("step"), qsTr("steps")))
        primaryText: qsTr("Apply")
        secondaryText: qsTr("Cancel")
        onAccepted: {
            view.previewTab = 2
            view.plan.apply()
        }
    }

    Tk.Dialog {
        id: contentDialog
        property string body
        dialogWidth: 760
        primaryText: qsTr("Close")
        Tk.Scroll {
            implicitHeight: 460
            width: parent.width
            Tk.Mono { text: contentDialog.body; width: parent.width; wrapMode: Text.WrapAnywhere }
        }
    }
}
