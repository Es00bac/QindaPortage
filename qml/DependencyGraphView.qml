// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Draws a laid-out neighbourhood from SystemModel.neighbourhood(): columns
// of package nodes (reverse dependencies left, dependencies right) and the
// edges between them. Pan by dragging, zoom with Ctrl+wheel or the buttons.
//
// `highlights` maps a package key to a change kind ("new", "upgrade",
// "rebuild", "remove") for impact previews.
Item {
    id: graph

    property var layout: ({ nodes: [], edges: [] })
    property var highlights: ({})
    property real zoom: 1.0
    // AGENT-CONTRACT: NodeHeight matches SystemModel::neighbourhood's
    // RowHeight minus the row gap (34 - 8).
    readonly property real nodeHeight: 26
    readonly property var nodeById: {
        var map = {}
        var nodes = graph.layout.nodes || []
        for (var i = 0; i < nodes.length; ++i) map[nodes[i].id] = nodes[i]
        return map
    }
    readonly property real contentWidth: {
        var w = 0
        var nodes = graph.layout.nodes || []
        for (var i = 0; i < nodes.length; ++i) w = Math.max(w, nodes[i].x + nodes[i].w)
        return w + 2 * Tk.Theme.space.xl
    }
    readonly property real contentHeight: {
        var h = 0
        var nodes = graph.layout.nodes || []
        for (var i = 0; i < nodes.length; ++i) h = Math.max(h, nodes[i].y + graph.nodeHeight)
        return h + 2 * Tk.Theme.space.xl
    }

    signal nodeChosen(string key)
    signal nodeOpened(string key)

    function roleColor(role) {
        switch (role) {
        case "world": return Tk.Theme.color.accent
        case "system": return Tk.Theme.color.info
        case "orphan": return Tk.Theme.color.warning
        default: return Tk.Theme.color.textMuted
        }
    }
    function changeColor(kind) {
        switch (kind) {
        case "new": return Tk.Theme.color.success
        case "upgrade": return Tk.Theme.color.accent
        case "downgrade": case "remove": return Tk.Theme.color.danger
        case "rebuild": return Tk.Theme.color.warning
        default: return Tk.Theme.color.border
        }
    }
    function edgeColor(types) {
        if (types.indexOf("run") >= 0) return Qt.alpha(Tk.Theme.color.accent, 0.55)
        if (types.indexOf("post") >= 0) return Qt.alpha(Tk.Theme.color.info, 0.55)
        return Qt.alpha(Tk.Theme.color.textMuted, 0.45)
    }
    function fit() {
        if (graph.contentWidth <= 0 || graph.contentHeight <= 0) return
        graph.zoom = Math.max(0.25, Math.min(1.5, Math.min(flick.width / graph.contentWidth,
                                                           flick.height / graph.contentHeight)))
    }
    onLayoutChanged: Qt.callLater(function () {
        graph.fit()
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
    })

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(width, graph.contentWidth * graph.zoom)
        contentHeight: Math.max(height, graph.contentHeight * graph.zoom)
        boundsBehavior: Flickable.StopAtBounds

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function (event) {
                graph.zoom = Math.max(0.2, Math.min(2.5, graph.zoom * (event.angleDelta.y > 0 ? 1.1 : 0.9)))
            }
        }

        Item {
            id: canvasRoot
            width: graph.contentWidth
            height: graph.contentHeight
            transformOrigin: Item.TopLeft
            scale: graph.zoom
            x: Math.max(0, (flick.width - graph.contentWidth * graph.zoom) / 2)
            y: Math.max(0, (flick.height - graph.contentHeight * graph.zoom) / 2)

            Canvas {
                id: wires
                objectName: "graphWires"
                anchors.fill: parent
                renderStrategy: Canvas.Cooperative
                property var edges: graph.layout.edges || []
                onEdgesChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var pad = Tk.Theme.space.xl
                    var list = wires.edges
                    for (var i = 0; i < list.length; ++i) {
                        var a = graph.nodeById[list[i].from]
                        var b = graph.nodeById[list[i].to]
                        if (!a || !b) continue
                        var x1 = pad + a.x + a.w, y1 = pad + a.y + graph.nodeHeight / 2
                        var x2 = pad + b.x, y2 = pad + b.y + graph.nodeHeight / 2
                        ctx.strokeStyle = graph.edgeColor(list[i].types)
                        ctx.lineWidth = (a.center || b.center) ? 1.6 : 1
                        ctx.beginPath()
                        ctx.moveTo(x1, y1)
                        if (x2 > x1) {
                            var mid = (x1 + x2) / 2
                            ctx.bezierCurveTo(mid, y1, mid, y2, x2, y2)
                        } else {
                            // Same column: loop out to the right.
                            var out = x1 + 30
                            ctx.bezierCurveTo(out, y1, out, y2, pad + b.x + b.w, y2)
                        }
                        ctx.stroke()
                    }
                }
            }

            Repeater {
                model: graph.layout.nodes || []
                delegate: Tk.Box {
                    id: node
                    required property var modelData
                    readonly property string change: graph.highlights[modelData.key] || ""
                    objectName: "graphNode_" + modelData.key
                    x: Tk.Theme.space.xl + modelData.x
                    y: Tk.Theme.space.xl + modelData.y
                    width: modelData.w
                    height: graph.nodeHeight
                    radius: Tk.Theme.radius.sm
                    color: modelData.center ? Tk.Theme.color.controlActiveBg
                           : node.change.length > 0 ? Qt.alpha(graph.changeColor(node.change), 0.18)
                           : Tk.Theme.color.panelAlt
                    borderColor: node.change.length > 0 ? graph.changeColor(node.change)
                                 : modelData.center || node.hovered ? Tk.Theme.color.accent : Tk.Theme.color.border
                    borderWidth: modelData.center || node.change.length > 0 ? 2 : 1
                    interactive: true
                    tooltip: qsTr("%1-%2 (%3)%4").arg(modelData.key).arg(modelData.version).arg(modelData.role)
                             .arg(node.change.length > 0 ? " — " + node.change : "")
                    onClicked: graph.nodeChosen(modelData.key)
                    onDoubleClicked: graph.nodeOpened(modelData.key)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        width: 3
                        radius: 1
                        color: graph.roleColor(node.modelData.role)
                    }
                    Tk.Label {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.sm + 2
                        anchors.rightMargin: Tk.Theme.space.xs
                        verticalAlignment: Text.AlignVCenter
                        text: node.modelData.label
                        font.weight: node.modelData.center ? Font.DemiBold : Font.Normal
                        color: node.modelData.center ? Tk.Theme.color.accentText : Tk.Theme.color.text
                    }
                }
            }
        }
    }

    Tk.Island {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Tk.Theme.space.md

        Tk.IconButton {
            iconName: "zoom-out"
            small: true
            tooltip: qsTr("Zoom out")
            onClicked: graph.zoom = Math.max(0.2, graph.zoom * 0.8)
        }
        Tk.Mono { text: Math.round(graph.zoom * 100) + "%" }
        Tk.IconButton {
            iconName: "zoom-in"
            small: true
            tooltip: qsTr("Zoom in")
            onClicked: graph.zoom = Math.min(2.5, graph.zoom * 1.25)
        }
        Tk.IconButton {
            iconName: "maximize"
            small: true
            tooltip: qsTr("Fit to view")
            onClicked: graph.fit()
        }
    }
}
