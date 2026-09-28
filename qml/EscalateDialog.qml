// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Tier 3: shows exactly what would be sent to Claude or Codex, lets the
// user edit it, and sends only on approval (design safety rule 4).
Tk.Dialog {
    id: dialog

    property var session
    property var cloud
    property var plan
    property string profile: ""
    property string provider: dialog.cloud && dialog.cloud.providers.length > 0 ? dialog.cloud.providers[0] : ""

    title: qsTr("Ask a cloud model")
    subtitle: qsTr("Only the text below is sent. The answer comes back as advice; the local assistant checks it with emerge --pretend before it can enter your plan.")
    dialogWidth: 720
    primaryText: qsTr("Send to %1").arg(dialog.provider === "codex" ? "Codex" : "Claude")
    secondaryText: qsTr("Cancel")
    primaryEnabled: dialog.provider.length > 0 && brief.text.trim().length > 0

    onAboutToShow: {
        var goal = dialog.plan && dialog.plan.goal.length > 0 ? dialog.plan.goal : qsTr("(see the conversation)")
        var planText = dialog.plan ? dialog.plan.steps.map(function (s) { return "- " + s.text + " — " + s.rationale }).join("\n") : ""
        brief.text = dialog.cloud.buildBrief(goal, dialog.profile, dialog.session.transcript, planText)
    }
    onAccepted: dialog.cloud.run(dialog.provider, brief.text)

    Connections {
        target: dialog.cloud
        function onAnswered(provider, text) {
            dialog.session.inject(qsTr("An expert consultant (%1) suggests the following. Check every suggestion with your tools "
                                       + "(pretend, package_info) before adding anything to the plan; say which parts you rejected and why.\n\n%2")
                                  .arg(provider).arg(text), true)
        }
        function onFailed(provider, error) {
            dialog.session.inject(qsTr("The %1 consultation failed: %2").arg(provider).arg(error), false)
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Segmented {
            small: true
            visible: dialog.cloud && dialog.cloud.providers.length > 1
            model: dialog.cloud ? dialog.cloud.providers : []
            currentIndex: dialog.cloud ? dialog.cloud.providers.indexOf(dialog.provider) : 0
            onActivated: function (index) { dialog.provider = dialog.cloud.providers[index] }
        }
        Tk.Notice {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: !dialog.cloud || dialog.cloud.providers.length === 0
            variant: "warning"
            text: qsTr("Neither the claude nor the codex CLI is on PATH.")
        }
        Tk.Scroll {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            implicitHeight: 360
            Tk.TextArea {
                id: brief
                objectName: "escalationBrief"
                width: parent.width
                mono: true
                rows: 16
                tooltip: qsTr("The brief that will be sent")
            }
        }
        Tk.Caption {
            text: qsTr("%1 characters. No secrets, keys or files outside this text are sent.").arg(brief.text.length)
        }
    }
}
