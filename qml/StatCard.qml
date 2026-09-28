// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One number with its label and a secondary line, optionally clickable.
Tk.Card {
    id: card

    property string label
    property string value
    property string detail
    property string iconName
    property string variant: "default"   // "accent", "warning", "danger", "success"

    interactive: card.tooltip.length > 0
    implicitWidth: 170

    readonly property color tone: card.variant === "accent" ? Tk.Theme.color.accent
                                : card.variant === "warning" ? Tk.Theme.color.warning
                                : card.variant === "danger" ? Tk.Theme.color.danger
                                : card.variant === "success" ? Tk.Theme.color.success
                                : Tk.Theme.color.text

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Icon { name: card.iconName; size: Tk.Theme.size.iconSm; color: card.tone; visible: card.iconName.length > 0 }
            Tk.Overline { text: card.label }
        }
        Tk.Heading {
            objectName: "statValue"
            level: 1
            text: card.value
            color: card.tone
        }
        Tk.Caption {
            text: card.detail
            visible: text.length > 0
            elide: Text.ElideRight
            Tk.Flex.alignSelf: Tk.Flex.Stretch
        }
    }
}
