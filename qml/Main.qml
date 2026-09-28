// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaTK.QindaQt
import "Format.js" as Format

// The QindaPortage window: navigation rail, the page, and the assistant
// dock. Context properties (main.cpp): systemModel, planModel, toolbox,
// llm, assistant, escalation, coordinator, startPage, startSelection.
//
// Menu authority is the coordinator's catalog (src/app/action_catalog.cpp);
// the global menu, the in-window AppMenuBar and the toolbar all dispatch
// through window.dispatch() by the same ids.
Tk.AppWindow {
    id: window

    // Written only by the menu export's fail-closed callback (main.cpp).
    property bool inWindowMenuVisible: true
    property string page: startPage.length > 0 ? startPage : "overview"
    // The package every view is about (category/name).
    property string selectedKey: startSelection
    property bool assistantVisible: true
    property string note: ""

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 560
    visible: true
    title: qsTr("QindaPortage")

    QindaQtTheme {}

    function dispatch(actionId) {
        switch (actionId) {
        case "file.refresh": systemModel.reload(); break
        case "file.settings": settingsDialog.open(); break
        case "file.quit": Qt.quit(); break
        case "view.overview": window.page = "overview"; break
        case "view.packages": window.page = "packages"; break
        case "view.graph": window.page = "graph"; break
        case "view.changes": window.page = "changes"; break
        case "view.sources": window.page = "sources"; break
        case "view.config": window.page = "config"; break
        case "view.activity": window.page = "activity"; break
        case "view.assistant": window.assistantVisible = !window.assistantVisible; break
        case "system.update": window.ask(qsTr("Preview a full @world update (-uDN) and explain what it would change and any risk.")); break
        case "system.depclean": window.ask(qsTr("Which installed packages are orphans that depclean would remove, and is any of them risky to remove?")); break
        case "system.sync": window.ask(qsTr("Plan a repository sync.")); break
        case "system.apply": window.page = "changes"; break
        case "assistant.new": assistant.reset(); break
        case "assistant.load": llm.ensureLoaded(); break
        case "assistant.unload": llm.unload(); break
        case "assistant.qinda": assistant.useEndpoint("qinda"); break
        case "assistant.escalate": escalateDialog.open(); break
        case "help.about": aboutDialog.open(); break
        }
    }

    // Opens the assistant and sends a request.
    function ask(text) {
        window.assistantVisible = true
        assistant.send(text)
    }

    // Approval from the chat or the dock's Apply button. The plan is
    // previewed first when it changed since the last preview; pkexec then
    // asks polkit, and the helper snapshots before changing anything.
    property bool applyWhenPreviewed: false
    function applyPlan() {
        if (planModel.stepCount === 0 || planModel.applying) return
        if (planModel.previewCurrent && !planModel.previewing) {
            confirmOrApply()
            return
        }
        window.applyWhenPreviewed = true
        assistant.inject(qsTr("The user approved the plan. Previewing it once more, then applying."), false)
        if (!planModel.previewing) planModel.runPreview()
    }
    // The user approved; high-risk plans still get one explicit confirmation
    // that names the risks (the same list that stops the assistant).
    function confirmOrApply() {
        window.applyWhenPreviewed = false
        if (planModel.riskReasons.length > 0) {
            riskDialog.open()
            return
        }
        startApply()
    }
    function startApply() {
        window.applyWhenPreviewed = false
        assistant.inject(qsTr("The user approved the plan; applying it now (the local model is unloaded while it builds)."), false)
        if (!planModel.apply()) {
            assistant.inject(qsTr("Could not start the helper: %1").arg(planModel.applyLog.join("\n")), false)
            return
        }
        window.page = "changes"
    }

    function select(key) {
        window.selectedKey = key
    }

    function showNote(text) {
        window.note = text
        noteTimer.restart()
    }

    Connections {
        target: coordinator
        function onActionRequested(actionId) { window.dispatch(actionId) }
    }
    Connections {
        target: llm
        function onUnloaded(reason) { window.showNote(qsTr("Model unloaded (%1)").arg(reason)) }
    }
    Connections {
        target: assistant
        // The assistant verified its plan with pretend; show the same impact
        // on the Changes page without another click.
        function onAnswered(text) {
            if (planModel.stepCount > 0 && !planModel.previewCurrent && !planModel.previewing) planModel.runPreview()
        }
    }
    Connections {
        target: planModel
        function onPreviewChanged() {
            if (!window.applyWhenPreviewed || planModel.previewing || !planModel.previewCurrent) return
            if (planModel.preview.resolved) {
                window.confirmOrApply()
            } else {
                window.applyWhenPreviewed = false
                window.page = "changes"
                assistant.inject(qsTr("The preview no longer resolves, so nothing was applied. Problems: %1. Fix the plan.")
                                 .arg((planModel.preview.problems || []).map(function (p) { return p.title }).join("; ")), true)
            }
        }
        function onApplyFinished(ok, txn, error, logTail) {
            systemModel.reload()
            window.showNote(ok ? qsTr("Transaction %1 applied").arg(txn) : qsTr("Transaction %1 failed").arg(txn))
            // apply_plan reports to the assistant itself; only user-started
            // transactions are narrated here.
            if (toolbox.agentApplying()) return
            if (ok) assistant.inject(qsTr("Applied as transaction %1. It can be reverted from Activity.").arg(txn), false)
            if (!ok) {
                // The model was unloaded for the build; asking reloads it.
                assistant.inject(qsTr("Applying the plan failed (transaction %1): %2\nLast output:\n%3\n"
                                      + "Use build_log on the failed package, find the cause and propose a fix.")
                                 .arg(txn).arg(error).arg(logTail), window.assistantVisible)
            }
        }
    }
    Timer {
        id: noteTimer
        interval: 6000
        onTriggered: window.note = ""
    }

    menuBar: AppMenuBar {
        visible: window.inWindowMenuVisible
        menusModel: coordinator.menus
        onActivated: function (actionId) { coordinator.activateAction(actionId) }
    }

    toolBars: [
        Tk.ToolBar {
            compact: true

            Tk.Icon { name: "package"; color: Tk.Theme.color.accent }
            Tk.Label {
                text: qsTr("QindaPortage")
                font.weight: Font.DemiBold
            }
            Tk.Mono {
                objectName: "profileName"
                text: systemModel.profileName
                color: Tk.Theme.color.textMuted
            }
            Tk.Spacer {}
            PackageJump {
                objectName: "packageJump"
                Tk.Flex.basis: 280
                Tk.Flex.shrink: 1
                onChosen: function (key) {
                    window.select(key)
                    if (window.page === "overview") window.page = "packages"
                }
            }
            ModelChip {
                runtime: llm
                session: assistant
                onClicked: window.assistantVisible = !window.assistantVisible
            }
            Tk.IconButton {
                iconName: "refresh-cw"
                small: true
                tooltip: qsTr("Reload system state (F5)")
                onClicked: coordinator.activateAction("file.refresh")
            }
        }
    ]

    statusBar: Tk.StatusBar {
        Tk.StatusField {
            actionable: false
            iconName: systemModel.loading ? "loader" : "database"
            text: systemModel.loading ? qsTr("reading the system…")
                  : qsTr("%1 installed · %2 world · %3 orphans").arg(systemModel.stats.installed || 0)
                        .arg(systemModel.stats.world || 0).arg(systemModel.stats.orphans || 0)
        }
        Tk.StatusField {
            iconName: "cpu"
            text: Format.modelStatus(llm)
            tooltip: llm.error.length > 0 ? llm.error : qsTr("Local model state; it unloads itself when idle and before builds")
            onClicked: window.assistantVisible = true
        }
        Tk.StatusField {
            actionable: false
            visible: assistant.status.length > 0
            iconName: "sparkles"
            text: assistant.status
        }
        Tk.StatusField {
            objectName: "rootSession"
            visible: planModel.sessionActive
            iconName: "lock"
            text: planModel.applying ? qsTr("root session · applying")
                                     : qsTr("root session · ends after %1 min idle").arg(planModel.sessionIdleMinutes)
            tooltip: qsTr("The QindaPortage helper is authenticated and waiting for the next transaction. Click to end it now.")
            onClicked: planModel.endSession()
        }
        Tk.Spacer {}
        Tk.StatusField {
            actionable: false
            visible: window.note.length > 0
            iconName: "info"
            text: window.note
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: 0

        Tk.Box {
            Tk.Flex.shrink: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            implicitWidth: rail.width
            color: Tk.Theme.color.surface
            borderRight: 1
            borderColor: Tk.Theme.color.border

            Tk.NavigationRail {
                id: rail
                objectName: "navigationRail"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: expanded ? 176 : compactWidth
                expanded: window.width > 1200
                currentId: window.page
                groups: [
                    { id: "see", label: qsTr("System"), entries: [
                        { id: "overview", label: qsTr("Overview"), iconName: "layout-dashboard", destination: "overview" },
                        { id: "packages", label: qsTr("Packages"), iconName: "package", destination: "packages",
                          badge: String(systemModel.stats.installed || "") },
                        { id: "graph", label: qsTr("Dependencies"), iconName: "share-2", destination: "graph" },
                        { id: "config", label: qsTr("Configuration"), iconName: "sliders-horizontal", destination: "config" }
                    ] },
                    { id: "change", label: qsTr("Change"), entries: [
                        { id: "changes", label: qsTr("Changes"), iconName: "git-branch", destination: "changes" },
                        { id: "sources", label: qsTr("From source"), iconName: "download", destination: "sources" },
                        { id: "activity", label: qsTr("Activity"), iconName: "history", destination: "activity" }
                    ] }
                ]
                onDestinationRequested: function (id, destination) {
                    window.page = destination
                    rail.confirmRoute(id)
                }
            }
        }

        T.SplitView {
            id: split
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 1
                color: T.SplitHandle.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
            }

            Item {
                id: pages
                T.SplitView.fillWidth: true
                T.SplitView.minimumWidth: 480

                OverviewView {
                    anchors.fill: parent
                    visible: window.page === "overview"
                    system: systemModel
                    onPackageChosen: function (key) { window.select(key); window.page = "packages" }
                    onAskRequested: function (text) { window.ask(text) }
                    onPageRequested: function (id) { window.page = id }
                }
                PackagesView {
                    anchors.fill: parent
                    visible: window.page === "packages"
                    system: systemModel
                    selectedKey: window.selectedKey
                    onPackageChosen: function (key) { window.select(key) }
                    onGraphRequested: function (key) { window.select(key); window.page = "graph" }
                    onAskRequested: function (text) { window.ask(text) }
                }
                GraphView {
                    anchors.fill: parent
                    visible: window.page === "graph"
                    system: systemModel
                    selectedKey: window.selectedKey
                    onPackageChosen: function (key) { window.select(key) }
                    onDetailsRequested: function (key) { window.select(key); window.page = "packages" }
                }
                ConfigView {
                    anchors.fill: parent
                    visible: window.page === "config"
                    system: systemModel
                    onAskRequested: function (text) { window.ask(text) }
                }
                ChangesView {
                    anchors.fill: parent
                    visible: window.page === "changes"
                    plan: planModel
                    system: systemModel
                    runtime: llm
                    onAskRequested: function (text) { window.ask(text) }
                    onPackageChosen: function (key) { window.select(key) }
                }
                ActivityView {
                    anchors.fill: parent
                    visible: window.page === "activity"
                    plan: planModel
                }
                SourcesView {
                    anchors.fill: parent
                    visible: window.page === "sources"
                    sources: sourcesModel
                    onAskRequested: function (text) { window.ask(text) }
                    onPlanRequested: window.page = "changes"
                }
            }

            AssistantDock {
                id: dock
                objectName: "assistantDock"
                visible: window.assistantVisible
                T.SplitView.preferredWidth: 420
                T.SplitView.minimumWidth: 320
                session: assistant
                runtime: llm
                cloud: escalation
                plan: planModel
                onEscalateRequested: escalateDialog.open()
                onPlanRequested: window.page = "changes"
                onApplyRequested: window.applyPlan()
                onCloseRequested: window.assistantVisible = false
            }
        }
    }

    EscalateDialog {
        id: escalateDialog
        session: assistant
        cloud: escalation
        plan: planModel
        profile: systemModel.revision >= 0 ? toolbox.systemProfile() : ""
    }

    Tk.Dialog {
        id: settingsDialog
        objectName: "settingsDialog"
        title: qsTr("Settings")
        subtitle: qsTr("How the assistant works and how long the root session stays open")
        primaryText: qsTr("Save")
        secondaryText: qsTr("Cancel")
        dialogWidth: 520
        onAboutToShow: {
            idleField.value = planModel.sessionIdleMinutes
            modelIdleField.value = llm.idleSeconds
            actBox.checked = planModel.autonomous
        }
        onAccepted: {
            planModel.sessionIdleMinutes = idleField.value
            llm.idleSeconds = modelIdleField.value
            planModel.autonomous = actBox.checked
        }

        Tk.Grid {
            columns: "auto 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.sm
            alignItems: Tk.Grid.Center

            Tk.Caption { text: qsTr("Act on my behalf") }
            Tk.Switch {
                id: actBox
                tooltip: qsTr("The assistant applies verified plans itself; risky ones still wait for you")
            }
            Tk.Caption { text: qsTr("Root session idle (min)") }
            Tk.NumberField {
                id: idleField
                from: 1
                to: 240
                tooltip: qsTr("After authenticating once, the helper stays available until it has been idle this long")
            }
            Tk.Caption { text: qsTr("Unload model after (s)") }
            Tk.NumberField {
                id: modelIdleField
                from: 30
                to: 3600
                tooltip: qsTr("The local model gives its memory back after this long without a request")
            }
        }
    }

    Tk.MessageDialog {
        id: riskDialog
        objectName: "riskDialog"
        title: qsTr("This plan needs your explicit OK")
        variant: "warning"
        text: qsTr("The assistant never applies these on its own:\n• %1\n\nThe configuration is snapshotted first and can be "
                   + "reverted from Activity; package changes cannot be undone automatically.")
              .arg(planModel.riskReasons.join("\n• "))
        primaryText: qsTr("Apply anyway")
        secondaryText: qsTr("Cancel")
        onAccepted: window.startApply()
    }

    Tk.MessageDialog {
        id: aboutDialog
        title: qsTr("About QindaPortage")
        variant: "info"
        text: qsTr("See, plan and repair Portage with a local model that unloads itself before builds. "
                   + "Every change is a plan you approve; nothing is installed outside Portage.")
        primaryText: qsTr("Close")
    }
}
