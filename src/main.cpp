// SPDX-License-Identifier: GPL-3.0-or-later
//
// QindaPortage bootstrap. Composition (GUI thread, constructor-visible):
//
//   SystemModel      — vardb, config and dependency graph (`systemModel`)
//   LlmRuntime       — the local llama-server child process (`llm`)
//   Toolbox          — the assistant's tools over the live models
//   AgentSession     — the assistant's conversation and tool loop (`assistant`)
//   CloudEscalation  — tier 3 through claude / codex (`escalation`)
//   ApplicationCoordinator — AppShell actions and the global menu (`coordinator`)
//   Main.qml (Tk.AppWindow)
//
// Destruction runs in reverse: the menu export and window go before the
// models they bind to; LlmRuntime's destructor stops the model process.

#include "agent/agentsession.h"
#include "agent/escalation.h"
#include "agent/llmruntime.h"
#include "app/action_catalog.h"
#include "app/planmodel.h"
#include "app/sourcesmodel.h"
#include "app/systemmodel.h"
#include "app/toolbox.h"

#include <qindaqt/app_shell/application_coordinator.h>
#include <qindaqt/app_shell/menu_export/first_party_composition.h>

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <memory>

using namespace QindaPortage;

namespace {

// Tier 2: qinda's qinda-llm router over Tailscale, when its key exists.
QVector<Agent::Endpoint> remoteEndpoints()
{
    QVector<Agent::Endpoint> out;
    QFile key(QDir::homePath() + QStringLiteral("/.config/qinda-llm/api-key"));
    if (key.open(QIODevice::ReadOnly)) {
        const QString url = qEnvironmentVariable("QINDAPORTAGE_QINDA_URL",
                                                 QStringLiteral("http://qinda.tailf118c.ts.net:8100/v1"));
        out.append(Agent::Endpoint{QStringLiteral("qinda"), QStringLiteral("qinda (35B)"), QUrl(url),
                                   QString::fromUtf8(key.readAll()).trimmed(), QStringLiteral("ornith"), 2});
    }
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("qindaportage"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaPortage"));
    QGuiApplication::setOrganizationName(QStringLiteral("QindaQt"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.Portage"));
    QGuiApplication::setApplicationVersion(QStringLiteral(QINDAPORTAGE_VERSION));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.qindaqt.Portage")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QindaPortage — see, plan and repair Portage with a local LLM"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("page"), QStringLiteral("Open on a page (overview, packages, graph, changes, "
                                                             "sources, config, activity)"),
                      QStringLiteral("page")});
    parser.addOption({QStringLiteral("select"), QStringLiteral("Select a package (category/name) at start"),
                      QStringLiteral("atom")});
    // Verification seams: send a request at start (optionally on a tier),
    // render the window, save the settled frame, exit.
    parser.addOption({QStringLiteral("ask"), QStringLiteral("Send this request to the assistant once the system is loaded"),
                      QStringLiteral("text")});
    parser.addOption({QStringLiteral("tier"), QStringLiteral("Assistant endpoint for --ask (local, qinda)"),
                      QStringLiteral("id")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save a window screenshot to PATH and exit"),
                      QStringLiteral("path")});
    parser.process(application);

    App::SystemModel systemModel;
    Agent::LlmRuntime llm;
    App::PlanModel planModel;
    App::Toolbox toolbox(&systemModel);
    toolbox.registerOpsTools(&planModel);
    App::SourcesModel sourcesModel(&systemModel, &planModel);
    toolbox.registerSourceTools(&sourcesModel);
    Agent::AgentSession assistant(&llm, toolbox.registry());
    assistant.setRemoteEndpoints(remoteEndpoints());
    assistant.setSystemContextProvider([&toolbox] { return toolbox.systemProfile(); });
    assistant.setRequestHintProvider([&toolbox](const QString &text) { return toolbox.requestHints(text); });
    assistant.setAnswerCheck([&toolbox] { return toolbox.planCheck(); });
    Agent::CloudEscalation escalation;
    // AGENT-CONTRACT: the model must not hold its memory while the helper
    // builds (docs/design.md "Model lifecycle"); Executor emits this
    // synchronously before it launches the helper.
    QObject::connect(planModel.executor(), &Ops::Executor::aboutToStart, &llm, &Agent::LlmRuntime::unload);

    QindaQt::AppShell::ApplicationCoordinator coordinator;
    coordinator.setApplicationName(QStringLiteral("QindaPortage"));
    if (const QindaQt::AppShell::Error error = coordinator.replaceActions(App::buildActions()); !error.ok()) {
        qFatal("qindaportage: action catalog rejected: %s", qPrintable(error.message));
    }
    auto syncModelActions = [&coordinator, &llm] {
        const bool loaded = llm.state() == Agent::LlmRuntime::State::Ready;
        static_cast<void>(coordinator.setActionEnabled(QStringLiteral("assistant.unload"), loaded));
    };
    QObject::connect(&llm, &Agent::LlmRuntime::stateChanged, &coordinator, syncModelActions);
    syncModelActions();

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    context->setContextProperty(QStringLiteral("systemModel"), &systemModel);
    context->setContextProperty(QStringLiteral("planModel"), &planModel);
    context->setContextProperty(QStringLiteral("toolbox"), &toolbox);
    context->setContextProperty(QStringLiteral("sourcesModel"), &sourcesModel);
    context->setContextProperty(QStringLiteral("llm"), &llm);
    context->setContextProperty(QStringLiteral("assistant"), &assistant);
    context->setContextProperty(QStringLiteral("escalation"), &escalation);
    context->setContextProperty(QStringLiteral("coordinator"), &coordinator);
    context->setContextProperty(QStringLiteral("startPage"), parser.value(QStringLiteral("page")));
    context->setContextProperty(QStringLiteral("startSelection"), parser.value(QStringLiteral("select")));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("QindaPortage"), QStringLiteral("Main"));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (window == nullptr) return 1;

    if (parser.isSet(QStringLiteral("ask"))) {
        const QString request = parser.value(QStringLiteral("ask"));
        const QString tier = parser.value(QStringLiteral("tier"));
        QObject::connect(&systemModel, &App::SystemModel::reloaded, &assistant, [&assistant, &planModel, request, tier] {
            if (!tier.isEmpty() && tier != QLatin1String("local")) assistant.useEndpoint(tier);
            planModel.setGoal(request);
            assistant.send(request);
        }, Qt::SingleShotConnection);
    }
    systemModel.reload();

    if (parser.isSet(QStringLiteral("screenshot"))) {
        const QString path = parser.value(QStringLiteral("screenshot"));
        const int delayMs = qEnvironmentVariableIntValue("QINDAPORTAGE_SCREENSHOT_MS") > 0
            ? qEnvironmentVariableIntValue("QINDAPORTAGE_SCREENSHOT_MS") : 4000;
        QTimer::singleShot(delayMs, window, [window, path] {
            if (window->grabWindow().save(path)) std::fprintf(stdout, "screenshot=%s\n", qPrintable(path));
            else std::fprintf(stderr, "qindaportage: could not save %s\n", qPrintable(path));
            QCoreApplication::quit();
        });
    }

    QPointer<QQuickWindow> windowGuard(window);
    std::unique_ptr<QObject> menuExport = QindaQt::AppShell::MenuExport::composeFirstPartyMenuExport(
        coordinator, *window, QDBusConnection::sessionBus(), [windowGuard](bool localMenuVisible) {
            if (windowGuard != nullptr) windowGuard->setProperty("inWindowMenuVisible", localMenuVisible);
        });

    const int result = QGuiApplication::exec();
    menuExport.reset();
    return result;
}
