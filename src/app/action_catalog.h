// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <qindaqt/app_shell/application_coordinator.h>

#include <QList>

namespace QindaPortage::App {

// AGENT-CONTRACT: action ids are dispatched by Main.qml's dispatch();
// adding one here without a case there leaves a dead menu entry.
QList<QindaQt::AppShell::ActionSpec> buildActions();

} // namespace QindaPortage::App
