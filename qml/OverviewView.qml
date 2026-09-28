// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "Format.js" as Format

// The system at a glance: counts, how it is layered, where the disk goes,
// which repositories it comes from, and one-click questions for the
// assistant.
Tk.Scroll {
    id: view

    property var system
    readonly property var stats: view.system ? view.system.stats : ({})
    property string treemapCategory: ""

    signal packageChosen(string key)
    signal askRequested(string text)
    signal pageRequested(string id)

    padding: Tk.Theme.space.lg

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.lg
        width: view.width - 2 * Tk.Theme.space.lg

        Tk.Notice {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: view.system && view.system.loading && !view.stats.installed
            variant: "info"
            text: qsTr("Reading /var/db/pkg, the profile and /etc/portage…")
        }

        Tk.Flex {
            gap: Tk.Theme.space.md
            wrap: Tk.Flex.Wrap
            Tk.Flex.alignSelf: Tk.Flex.Stretch

            StatCard {
                objectName: "statInstalled"
                label: qsTr("Installed")
                iconName: "package"
                value: String(view.stats.installed || 0)
                detail: view.system ? view.system.formatBytes(view.stats.bytes || 0) + qsTr(" on disk") : ""
                tooltip: qsTr("Browse every installed package")
                onClicked: view.pageRequested("packages")
            }
            StatCard {
                label: qsTr("World")
                iconName: "list-checks"
                variant: "accent"
                value: String(view.stats.world || 0)
                detail: qsTr("%1 in the world file, %2 from the profile").arg(view.stats.worldFile || 0).arg(view.stats.profileSet || 0)
            }
            StatCard {
                label: qsTr("Dependencies")
                iconName: "share-2"
                value: String(view.stats.edges || 0)
                detail: qsTr("edges in effect with your USE flags")
                tooltip: qsTr("Open the dependency graph")
                onClicked: view.pageRequested("graph")
            }
            StatCard {
                objectName: "statOrphans"
                label: qsTr("Orphans")
                iconName: "circle-alert"
                variant: (view.stats.orphans || 0) > 0 ? "warning" : "success"
                value: String(view.stats.orphans || 0)
                detail: qsTr("removable with depclean")
                tooltip: qsTr("Ask the assistant whether they are safe to remove")
                onClicked: view.askRequested(qsTr("List the orphans depclean would remove and flag any that look risky to remove."))
            }
            StatCard {
                label: qsTr("Unsatisfied")
                iconName: "triangle-alert"
                variant: (view.stats.unsatisfied || 0) > 0 ? "danger" : "success"
                value: String(view.stats.unsatisfied || 0)
                detail: qsTr("dependencies with no installed provider")
            }
        }

        Tk.Panel {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            title: qsTr("How this system is built")
            iconName: "layers"
            collapsible: false

            LayerBar {
                width: parent.width
                stats: view.stats
            }
        }

        Tk.Flex {
            gap: Tk.Theme.space.lg
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            align: Tk.Flex.Stretch

            Tk.Panel {
                Tk.Flex.grow: 2
                Tk.Flex.basis: 0
                title: view.treemapCategory.length > 0 ? qsTr("Disk use — %1").arg(view.treemapCategory)
                                                       : qsTr("Disk use by category")
                iconName: "chart-area"
                collapsible: false
                actions: Tk.Button {
                    small: true
                    variant: "ghost"
                    iconName: "arrow-left"
                    text: qsTr("All categories")
                    visible: view.treemapCategory.length > 0
                    onClicked: view.treemapCategory = ""
                }

                Treemap {
                    objectName: "diskTreemap"
                    width: parent.width
                    implicitHeight: 320
                    height: 320
                    system: view.system
                    category: view.treemapCategory
                    onCategoryChosen: function (category) { view.treemapCategory = category }
                    onPackageChosen: function (key) { view.packageChosen(key) }
                }
            }

            Tk.Panel {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                title: qsTr("Repositories")
                iconName: "database"
                collapsible: false

                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    width: parent.width

                    Repeater {
                        model: view.system ? view.system.repositories : []
                        delegate: Tk.Flex {
                            id: repoRow
                            required property var modelData
                            direction: Tk.Flex.Column
                            gap: 2
                            Tk.Flex.alignSelf: Tk.Flex.Stretch

                            Tk.Flex {
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                gap: Tk.Theme.space.sm
                                Tk.Label { text: repoRow.modelData.name; font.weight: Font.DemiBold }
                                Tk.Badge {
                                    visible: repoRow.modelData.gone === true
                                    variant: "warning"
                                    text: qsTr("not configured")
                                }
                                Tk.Spacer {}
                                Tk.Mono {
                                    text: qsTr("%1 pkgs · %2").arg(repoRow.modelData.installed)
                                          .arg(view.system.formatBytes(repoRow.modelData.bytes))
                                    color: Tk.Theme.color.textMuted
                                }
                            }
                            Tk.ProgressBar {
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                thin: true
                                value: repoRow.modelData.installed / Math.max(1, view.stats.installed || 1)
                            }
                        }
                    }
                }
            }
        }

        Tk.Panel {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            title: qsTr("Ask about this system")
            iconName: "sparkles"
            collapsible: false

            Tk.Flex {
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                width: parent.width

                Repeater {
                    model: [
                        qsTr("What would a full @world update change right now, and is anything risky?"),
                        qsTr("Why is the largest package on this system installed?"),
                        qsTr("Are there packages installed from keyworded (~amd64) versions I should review?"),
                        qsTr("Which of my package.use entries no longer do anything?")
                    ]
                    delegate: Tk.Chip {
                        required property string modelData
                        small: true
                        iconName: "message-square"
                        text: modelData
                        onClicked: view.askRequested(modelData)
                    }
                }
            }
        }
    }
}
