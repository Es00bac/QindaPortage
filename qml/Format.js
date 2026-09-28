// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Display helpers shared by the views. No colours here: callers map the
// returned variant names onto Tk.Theme roles.

var dash = "—"

function plural(n, one, many) {
    return n + " " + (n === 1 ? one : many)
}

// Badge variant for a package's role in the graph.
function roleVariant(role) {
    switch (role) {
    case "world": return "accent"
    case "system": return "info"
    case "orphan": return "warning"
    default: return "default"
    }
}

function roleLabel(role) {
    switch (role) {
    case "world": return qsTr("world")
    case "system": return qsTr("system")
    case "orphan": return qsTr("orphan")
    case "dependency": return qsTr("dependency")
    default: return role
    }
}

function originLabel(origin) {
    switch (origin) {
    case "package.use": return qsTr("package.use")
    case "make.conf": return qsTr("make.conf")
    case "profile": return qsTr("profile")
    case "forced": return qsTr("forced by profile")
    case "masked": return qsTr("masked by profile")
    case "default": return qsTr("ebuild default")
    default: return origin
    }
}

function modelStatus(llm) {
    switch (llm.state) {
    case "ready": return qsTr("%1 loaded · %2 GiB free").arg(llm.modelLabel).arg(llm.availableGiB.toFixed(1))
    case "loading": return qsTr("loading %1…").arg(llm.modelLabel)
    case "unloading": return qsTr("unloading…")
    case "failed": return qsTr("model failed")
    default: return qsTr("model unloaded · %1 GiB free").arg(llm.availableGiB.toFixed(1))
    }
}

function actionVariant(action) {
    switch (action) {
    case "new": case "new-slot": return "success"
    case "upgrade": return "accent"
    case "downgrade": return "warning"
    case "uninstall": case "block": return "danger"
    default: return "default"
    }
}
