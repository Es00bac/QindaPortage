// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Install software that no repository carries: probe a URL, check how its
// dependencies map to Gentoo atoms, edit the drafted ebuild, and add it to
// the plan so Portage builds, tracks and can remove it.
Tk.Flex {
    id: view

    property var sources
    readonly property var info: view.sources ? view.sources.info : ({})
    readonly property bool hasResult: view.sources && view.sources.packageName.length > 0

    signal askRequested(string text)
    signal planRequested()

    direction: Tk.Flex.Column
    gap: 0

    Tk.ToolBar {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        Tk.Icon { name: "download"; color: Tk.Theme.color.accent; size: Tk.Theme.size.iconSm }
        Tk.TextField {
            id: urlField
            objectName: "sourceUrl"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            mono: true
            placeholderText: "https://github.com/owner/project  ·  https://…/release.tar.gz  ·  /path/to/tree"
            tooltip: qsTr("Where the software lives")
            onAccepted: view.sources.probe(text)
        }
        Tk.Button {
            variant: "accent"
            iconName: "search"
            text: qsTr("Probe")
            busy: view.sources && view.sources.busy
            available: urlField.text.trim().length > 0
            onClicked: view.sources.probe(urlField.text)
        }
        Tk.Button {
            iconName: "sparkles"
            text: qsTr("Let the assistant do it")
            tooltip: qsTr("The assistant probes, fixes the draft, maps dependencies and verifies with pretend")
            available: urlField.text.trim().length > 0
            onClicked: view.askRequested(qsTr("Install %1 as a Portage package in the qinda-local overlay. Probe it, fix the draft ebuild, map every dependency to an existing Gentoo package, verify with pretend, and plan it.").arg(urlField.text.trim()))
        }
    }

    Tk.Notice {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: view.sources && view.sources.busy
        variant: "info"
        text: view.sources ? view.sources.progress : ""
    }
    Tk.Notice {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: view.sources && !view.sources.busy && view.sources.error.length > 0
        variant: "danger"
        text: view.sources ? view.sources.error : ""
    }

    Item {
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.alignSelf: Tk.Flex.Stretch

        Tk.EmptyState {
            anchors.fill: parent
            visible: !view.hasResult && !(view.sources && view.sources.busy)
            iconName: "download"
            title: qsTr("Anything, tracked by Portage")
            text: qsTr("Point at a repository, a release tarball or a local tree. QindaPortage writes an ebuild into the "
                       + "qinda-local overlay, so the software installs, updates and uninstalls like every other package.")
        }

        T.SplitView {
            anchors.fill: parent
            visible: view.hasResult
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 1
                color: T.SplitHandle.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
            }

            Tk.Scroll {
                id: factsScroll
                T.SplitView.preferredWidth: 420
                T.SplitView.minimumWidth: 320
                padding: Tk.Theme.space.md

                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    width: factsScroll.width - 2 * Tk.Theme.space.md

                    Tk.Heading {
                        level: 2
                        text: view.sources ? view.sources.packageName + "-" + view.sources.version : ""
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                    }
                    Tk.Notice {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        visible: view.sources && view.sources.alreadyPackaged.length > 0
                        variant: "warning"
                        title: qsTr("Already packaged")
                        text: view.sources ? qsTr("%1 is available from a repository. Installing that is simpler and gets updates; "
                                                  + "package it here only if you need upstream %2.")
                                                 .arg(view.sources.alreadyPackaged.join(", ")).arg(view.sources.version) : ""
                    }
                    Tk.Label {
                        text: view.info.description || ""
                        wrapMode: Text.Wrap
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                    }
                    Tk.KeyValue { Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Build system"); value: view.info.buildSystem || "—" }
                    Tk.KeyValue { Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("License"); value: view.info.license || view.info.spdxLicense || "—" }
                    Tk.KeyValue { Tk.Flex.alignSelf: Tk.Flex.Stretch; key: qsTr("Version from"); value: view.info.snapshot ? qsTr("commit %1").arg(String(view.info.commit || "").slice(0, 10)) : (view.info.tag || "—") }
                    Tk.Flex {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        gap: Tk.Theme.space.sm
                        align: Tk.Flex.Center
                        Tk.Caption { text: qsTr("Category") }
                        Tk.TextField {
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            small: true
                            mono: true
                            text: view.sources ? view.sources.category : ""
                            tooltip: qsTr("Gentoo category for the new package")
                            onEditingFinished: view.sources.category = text
                        }
                    }
                    Tk.Mono {
                        text: view.sources ? view.sources.ebuildPath : ""
                        color: Tk.Theme.color.textMuted
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        elide: Text.ElideMiddle
                    }
                    Repeater {
                        model: view.info.warnings || []
                        delegate: Tk.Notice {
                            required property var modelData
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            variant: "warning"
                            text: String(modelData)
                        }
                    }
                    Tk.SectionHeader {
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        title: qsTr("Dependencies")
                        count: String(view.sources ? view.sources.dependencies.length : 0)
                    }
                    Repeater {
                        model: view.sources ? view.sources.dependencies : []
                        delegate: Tk.ListRow {
                            required property var modelData
                            Tk.Flex.alignSelf: Tk.Flex.Stretch
                            small: true
                            iconName: modelData.resolvedBy === "unresolved" ? "circle-alert" : "circle-check"
                            text: modelData.name || ""
                            secondaryText: (modelData.atoms && modelData.atoms.length > 0 ? modelData.atoms.join(" ") : qsTr("no Gentoo package found"))
                                           + " · " + (modelData.resolvedBy || "")
                            tooltip: modelData.note || ""
                        }
                    }
                }
            }

            Tk.Flex {
                T.SplitView.fillWidth: true
                direction: Tk.Flex.Column
                gap: 0

                Tk.ToolBar {
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    compact: true
                    Tk.Label { text: qsTr("Draft ebuild"); font.weight: Font.DemiBold }
                    Tk.Caption { text: qsTr("editable; lines marked AGENT: need attention") }
                    Tk.Spacer {}
                    Tk.Button {
                        small: true
                        variant: "accent"
                        iconName: "list-plus"
                        text: qsTr("Add to plan")
                        tooltip: qsTr("Write this ebuild into qinda-local and emerge it (after you approve the plan)")
                        onClicked: {
                            var why = view.sources.addToPlan()
                            if (why.length > 0) addNotice.text = why
                            else view.planRequested()
                        }
                    }
                }
                Tk.Notice {
                    id: addNotice
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    visible: text.length > 0
                    variant: "danger"
                    dismissible: true
                    onDismissed: text = ""
                }
                Tk.Scroll {
                    id: draftScroll
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    Tk.TextArea {
                        objectName: "draftEbuild"
                        width: draftScroll.width
                        mono: true
                        text: view.sources ? view.sources.draft : ""
                        tooltip: qsTr("The ebuild that will be written")
                        onEditingFinished: view.sources.draft = text
                    }
                }
            }
        }
    }
}
