// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import "Format.js" as Format

// Toolbar chip: which model the assistant uses and whether it is resident.
Tk.Chip {
    id: chip

    property var runtime
    property var session

    small: true
    iconName: chip.runtime.state === "loading" || chip.session.busy ? "loader" : "sparkles"
    active: chip.runtime.state === "ready"
    text: chip.session.tier > 1 ? chip.session.endpointLabel
          : chip.runtime.state === "ready" ? chip.runtime.modelLabel
          : chip.runtime.state === "loading" ? qsTr("loading…")
          : qsTr("model off")
    tooltip: Format.modelStatus(chip.runtime) + "\n" + qsTr("Toggle the assistant (Ctrl+J)")
}
