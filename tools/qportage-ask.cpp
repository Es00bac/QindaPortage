// SPDX-License-Identifier: GPL-3.0-or-later
//
// qportage-ask: sends one request to the QindaPortage assistant with the
// real tools and prints the transcript and the resulting draft plan. A
// development and verification seam, not installed. Nothing is applied.
//
//   qportage-ask [--tier local|qinda] "install jq with static libraries"

#include "agent/agentsession.h"
#include "agent/llmruntime.h"
#include "app/planmodel.h"
#include "app/sourcesmodel.h"
#include "app/systemmodel.h"
#include "app/toolbox.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTextStream>
#include <QTimer>

using namespace QindaPortage;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    QString tier = QStringLiteral("local");
    if (args.size() >= 2 && args.first() == QLatin1String("--tier")) {
        tier = args.at(1);
        args = args.mid(2);
    }
    const QString request = args.join(QLatin1Char(' '));
    QTextStream out(stdout);

    App::SystemModel system;
    Agent::LlmRuntime llm;
    App::PlanModel plan;
    App::Toolbox toolbox(&system);
    toolbox.registerOpsTools(&plan);
    App::SourcesModel sources(&system, &plan);
    toolbox.registerSourceTools(&sources);
    Agent::AgentSession session(&llm, toolbox.registry());
    QFile key(QDir::homePath() + QStringLiteral("/.config/qinda-llm/api-key"));
    if (key.open(QIODevice::ReadOnly)) {
        session.setRemoteEndpoints({Agent::Endpoint{QStringLiteral("qinda"), QStringLiteral("qinda (35B)"),
                                                    QUrl(QStringLiteral("http://qinda.tailf118c.ts.net:8100/v1")),
                                                    QString::fromUtf8(key.readAll()).trimmed(), QStringLiteral("ornith"), 2}});
    }
    session.setSystemContextProvider([&toolbox] { return toolbox.systemProfile(); });
    session.setRequestHintProvider([&toolbox](const QString &t) { return toolbox.requestHints(t); });
    session.setAnswerCheck([&toolbox] { return toolbox.planCheck(); });
    if (tier != QLatin1String("local")) session.useEndpoint(tier);

    int printed = 0;
    auto flush = [&] {
        const QVariantList t = session.transcript();
        for (; printed < t.size(); ++printed) {
            const QVariantMap e = t.at(printed).toMap();
            const QString kind = e.value(QStringLiteral("kind")).toString();
            out << "[" << e.value(QStringLiteral("time")).toString() << "] " << kind;
            if (kind == QLatin1String("tool")) {
                out << " " << e.value(QStringLiteral("tool")).toString() << " " << e.value(QStringLiteral("args")).toString()
                    << "\n    -> " << e.value(QStringLiteral("text")).toString().left(700);
            } else {
                out << ": " << e.value(QStringLiteral("text")).toString();
            }
            out << "\n";
            out.flush();
        }
    };
    QObject::connect(&session, &Agent::AgentSession::transcriptChanged, &app, flush);
    QObject::connect(&session, &Agent::AgentSession::answered, &app, [&] {
        flush();
        out << "\n--- draft plan (" << plan.stepCount() << " steps) ---\n";
        for (const QString &line : plan.plan().describe()) out << "  " << line << "\n";
        for (const QString &frag : plan.configPreview()) out << frag << "\n";
        out.flush();
        llm.unload();
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    });
    QObject::connect(&session, &Agent::AgentSession::busyChanged, &app, [&] {
        if (!session.busy() && session.escalationSuggested()) {
            flush();
            out << "\n--- stopped: " << session.status() << "\n";
            llm.unload();
            QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        }
    });
    QObject::connect(&system, &App::SystemModel::reloaded, &app, [&] {
        out << "system loaded; asking " << (tier == QLatin1String("local") ? QStringLiteral("local model") : tier) << "\n";
        out.flush();
        plan.setGoal(request);
        session.send(request);
    });
    system.reload();
    QTimer::singleShot(20 * 60 * 1000, &app, [&] { out << "timeout\n"; llm.unload(); app.exit(2); });
    return app.exec();
}
