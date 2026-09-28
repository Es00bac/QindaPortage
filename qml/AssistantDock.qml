// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "Format.js" as Format

// The assistant: conversation, tool trace, draft-plan summary, model tier
// and escalation. `session` is Agent::AgentSession, `runtime` LlmRuntime,
// `cloud` CloudEscalation, `plan` PlanModel.
Tk.Box {
    id: dock

    property var session
    property var runtime
    property var cloud
    property var plan

    signal escalateRequested()
    signal closeRequested()
    signal planRequested()
    // The user approved the pending plan in the chat or with the Apply button.
    signal applyRequested()

    color: Tk.Theme.color.panel
    borderLeft: 1
    borderColor: Tk.Theme.color.border

    function send() {
        var text = input.text.trim()
        if (text.length === 0 || dock.session.busy) return
        // AGENT-NOTE: the assistant cannot apply anything itself; a short
        // "yes / apply / go ahead" while a plan waits is the user's approval
        // and goes to the same apply path as the Apply button.
        if (dock.plan && dock.plan.stepCount > 0 && !dock.plan.applying && Format.isApproval(text)) {
            input.text = ""
            dock.applyRequested()
            return
        }
        if (dock.plan && dock.plan.goal.length === 0) dock.plan.goal = text
        dock.session.send(text)
        input.text = ""
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0

        // Header: what answers, and how.
        Tk.ToolBar {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            compact: true

            Tk.Icon { name: "sparkles"; color: Tk.Theme.color.accent; size: Tk.Theme.size.iconSm }
            Tk.Label { text: qsTr("Assistant"); font.weight: Font.DemiBold }
            Tk.Segmented {
                objectName: "tierSwitch"
                small: true
                model: (dock.session ? dock.session.endpoints : []).map(function (e) {
                    return { text: e.id === "local" ? qsTr("Local") : e.label, tooltip: qsTr("Tier %1").arg(e.tier) }
                })
                currentIndex: {
                    var list = dock.session ? dock.session.endpoints : []
                    for (var i = 0; i < list.length; ++i) if (list[i].tier === dock.session.tier && (i === 0) === (dock.session.tier === 1)) return i
                    return 0
                }
                onActivated: function (index) { dock.session.useEndpoint(dock.session.endpoints[index].id) }
            }
            Tk.Spacer {}
            Tk.IconButton {
                small: true
                iconName: "cloud"
                tooltip: qsTr("Ask Claude or Codex (you approve what is sent)")
                enabled: dock.cloud && dock.cloud.providers.length > 0 && !dock.cloud.running
                onClicked: dock.escalateRequested()
            }
            Tk.IconButton {
                small: true
                iconName: "message-square-plus"
                tooltip: qsTr("New conversation (Ctrl+N)")
                onClicked: dock.session.reset()
            }
            Tk.IconButton {
                small: true
                iconName: "x"
                tooltip: qsTr("Hide the assistant (Ctrl+J)")
                onClicked: dock.closeRequested()
            }
        }

        // Model line.
        Tk.Box {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            padding: Tk.Theme.space.sm
            borderBottom: 1
            borderColor: Tk.Theme.color.border
            color: Tk.Theme.color.surface

            Tk.Flex {
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.Badge {
                    dot: true
                    variant: dock.runtime.state === "ready" ? "success"
                             : dock.runtime.state === "loading" ? "warning"
                             : dock.runtime.state === "failed" ? "danger" : "default"
                }
                Tk.Caption {
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    text: dock.session.tier > 1 ? qsTr("%1 · the local model is unloaded").arg(dock.session.endpointLabel)
                                                : Format.modelStatus(dock.runtime)
                    elide: Text.ElideRight
                }
                Tk.Switch {
                    objectName: "actSwitch"
                    small: true
                    text: qsTr("act")
                    checked: dock.plan ? dock.plan.autonomous : false
                    tooltip: qsTr("Act on my behalf: the assistant applies verified plans itself. Risky changes "
                                  + "(removing system packages, toolchain downgrades, unmasking, live ebuilds, huge merges) "
                                  + "still wait for you. Off: it only plans and you apply.")
                    onToggled: dock.plan.autonomous = checked
                }
                Tk.Switch {
                    small: true
                    text: qsTr("think")
                    checked: dock.session.thinking
                    tooltip: qsTr("Let the model reason before answering: better for diagnosing failures, much slower on this laptop")
                    onToggled: dock.session.thinking = checked
                }
                Tk.Button {
                    small: true
                    variant: "ghost"
                    iconName: dock.runtime.state === "ready" ? "circle-off" : "play"
                    text: dock.runtime.state === "ready" ? qsTr("Unload") : qsTr("Load")
                    tooltip: dock.runtime.state === "ready" ? qsTr("Give the memory back now") : qsTr("Load the local model now")
                    visible: dock.session.tier === 1
                    onClicked: dock.runtime.state === "ready" ? dock.runtime.unload() : dock.runtime.ensureLoaded()
                }
            }
        }

        Tk.Scroll {
            id: scroll
            objectName: "assistantTranscript"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            padding: Tk.Theme.space.sm

            Tk.Flex {
                id: transcript
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                width: scroll.width - 2 * Tk.Theme.space.sm
                onImplicitHeightChanged: Qt.callLater(scroll.scrollToBottom)

                Tk.EmptyState {
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    visible: dock.session.transcript.length === 0
                    iconName: "sparkles"
                    title: qsTr("Ask for anything Portage")
                    text: qsTr("“Install obs-studio with browser support”, “why is qtwebengine installed?”, "
                               + "“install https://github.com/sharkdp/fd”, “update the system safely”. "
                               + "Nothing changes until you approve the plan.")
                }

                Repeater {
                    model: dock.session.transcript
                    delegate: TranscriptEntry {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                    }
                }

                // Live tokens.
                Tk.Box {
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    visible: dock.session.busy
                    padding: Tk.Theme.space.sm
                    radius: Tk.Theme.radius.md
                    color: Tk.Theme.color.panelAlt
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Flex {
                            gap: Tk.Theme.space.xs
                            align: Tk.Flex.Center
                            Tk.Spinner { size: 12 }
                            Tk.Caption { text: dock.session.status }
                        }
                        Tk.Label {
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            visible: dock.session.streamingText.length > 0
                            text: dock.session.streamingText
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Tk.Notice {
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    visible: dock.session.escalationSuggested && !dock.session.busy
                    variant: "warning"
                    title: qsTr("Stuck")
                    text: qsTr("This model could not finish. Continue with a stronger one?")
                    actions: Tk.Flex {
                        gap: Tk.Theme.space.xs
                        Tk.Button {
                            small: true
                            text: qsTr("qinda 35B")
                            visible: dock.session.endpoints.length > 1 && dock.session.tier < 2
                            onClicked: {
                                dock.session.useEndpoint("qinda")
                                dock.session.inject(qsTr("Continue the task above where the previous model stopped."), true)
                            }
                        }
                        Tk.Button {
                            small: true
                            text: qsTr("Claude / Codex…")
                            onClicked: dock.escalateRequested()
                        }
                    }
                }
            }
        }

        // Draft plan summary.
        Tk.Box {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: dock.plan && dock.plan.stepCount > 0
            padding: Tk.Theme.space.sm
            color: Tk.Theme.color.accentSubtle
            borderTop: 1
            borderColor: Tk.Theme.color.border
            Tk.Flex {
                gap: Tk.Theme.space.sm
                align: Tk.Flex.Center
                Tk.Icon { name: "list-checks"; color: Tk.Theme.color.accent; size: Tk.Theme.size.iconSm }
                Tk.Label {
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    text: !dock.plan ? ""
                          : dock.plan.applying ? qsTr("Applying: %1").arg(dock.plan.applyStep)
                          : qsTr("Plan: %1 — say \"apply\" to run it").arg(Format.plural(dock.plan.stepCount, qsTr("step"), qsTr("steps")))
                    elide: Text.ElideRight
                }
                Tk.Button {
                    small: true
                    variant: "ghost"
                    text: qsTr("Review")
                    iconName: "arrow-right"
                    tooltip: qsTr("Open the plan and its impact on the Changes page")
                    onClicked: dock.planRequested()
                }
                Tk.Button {
                    objectName: "dockApply"
                    small: true
                    variant: "accent"
                    iconName: "play"
                    text: dock.plan && dock.plan.applying ? qsTr("Applying…") : qsTr("Apply")
                    busy: dock.plan && (dock.plan.applying || dock.plan.previewing)
                    tooltip: qsTr("Run the plan (you can also just say \"apply\")")
                    onClicked: dock.applyRequested()
                }
            }
        }

        // Input.
        Tk.Box {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            padding: Tk.Theme.space.sm
            borderTop: 1
            borderColor: Tk.Theme.color.border
            color: Tk.Theme.color.surface

            Tk.Flex {
                gap: Tk.Theme.space.sm
                align: Tk.Flex.End
                Tk.TextArea {
                    id: input
                    objectName: "assistantInput"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    rows: 2
                    placeholderText: qsTr("Ask or request a change… (Ctrl+Enter sends)")
                    tooltip: qsTr("Message to the assistant")
                    Keys.onPressed: function (event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                            dock.send()
                            event.accepted = true
                        }
                    }
                }
                Tk.Button {
                    variant: dock.session.busy ? "default" : "accent"
                    iconName: dock.session.busy ? "square" : "send"
                    text: dock.session.busy ? qsTr("Stop") : qsTr("Send")
                    onClicked: dock.session.busy ? dock.session.stop() : dock.send()
                }
            }
        }
    }
}
