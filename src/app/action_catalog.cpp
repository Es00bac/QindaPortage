// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/action_catalog.h"

namespace QindaPortage::App {

namespace {

using QindaQt::AppShell::ActionSpec;

ActionSpec action(const char *id, const char *menuId, const char *menuLabel, int menuOrder,
                  const char *label, const char *shortcut, int order, bool destructive = false)
{
    ActionSpec spec;
    // AGENT-GUARD: labels contain non-ASCII ("Settings…"); fromUtf8 keeps
    // the exported dbusmenu labels intact (see QindaLlamaWatch).
    spec.id = QString::fromUtf8(id);
    spec.menuId = QString::fromUtf8(menuId);
    spec.menuLabel = QString::fromUtf8(menuLabel);
    spec.menuOrder = menuOrder;
    spec.label = QString::fromUtf8(label);
    spec.shortcut = QKeySequence(QString::fromUtf8(shortcut));
    spec.order = order;
    spec.checkable = false;
    spec.checked = false;
    spec.destructive = destructive;
    return spec;
}

} // namespace

QList<ActionSpec> buildActions()
{
    // AGENT-GUARD: AppShell's registry rejects an action with no shortcut
    // ("invalid presentation state"), so every entry carries one.
    return {
        action("file.refresh", "file", "File", 0, "Reload System State", "F5", 0),
        action("file.settings", "file", "File", 0, "Settings…", "Ctrl+,", 1),
        action("file.quit", "file", "File", 0, "Quit", "Ctrl+Q", 2),

        action("view.overview", "view", "View", 10, "Overview", "Ctrl+1", 0),
        action("view.packages", "view", "View", 10, "Packages", "Ctrl+2", 1),
        action("view.graph", "view", "View", 10, "Dependency Graph", "Ctrl+3", 2),
        action("view.changes", "view", "View", 10, "Changes", "Ctrl+4", 3),
        action("view.sources", "view", "View", 10, "Install from Source", "Ctrl+5", 4),
        action("view.config", "view", "View", 10, "Configuration", "Ctrl+6", 5),
        action("view.activity", "view", "View", 10, "Activity", "Ctrl+7", 6),
        action("view.assistant", "view", "View", 10, "Show Assistant", "Ctrl+J", 7),

        action("system.update", "system", "System", 20, "Preview World Update", "Ctrl+U", 0),
        action("system.depclean", "system", "System", 20, "Preview Orphan Cleanup", "Ctrl+Shift+D", 1),
        action("system.sync", "system", "System", 20, "Sync Repositories…", "Ctrl+Shift+S", 2),
        action("system.apply", "system", "System", 20, "Apply Plan…", "Ctrl+Return", 3, true),

        action("assistant.new", "assistant", "Assistant", 30, "New Conversation", "Ctrl+N", 0),
        action("assistant.load", "assistant", "Assistant", 30, "Load Local Model", "Ctrl+L", 1),
        action("assistant.unload", "assistant", "Assistant", 30, "Unload Local Model", "Ctrl+Shift+L", 2),
        action("assistant.qinda", "assistant", "Assistant", 30, "Continue on qinda", "Ctrl+Shift+Q", 3),
        action("assistant.escalate", "assistant", "Assistant", 30, "Ask Claude or Codex…", "Ctrl+Shift+E", 4),

        action("help.about", "help", "Help", 40, "About QindaPortage", "F1", 0),
    };
}

} // namespace QindaPortage::App
