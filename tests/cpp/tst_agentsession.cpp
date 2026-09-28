// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/agentsession.h"
#include "fakeopenai.h"

#include <QSignalSpy>
#include <QTest>

using namespace QindaPortage::Agent;

class TstAgentSession : public QObject
{
    Q_OBJECT

    ToolRegistry registry()
    {
        ToolRegistry r;
        r.add({QStringLiteral("find_packages"), QStringLiteral("search"),
               ToolRegistry::object({{QStringLiteral("query"), ToolRegistry::string(QStringLiteral("q"))}},
                                    {QStringLiteral("query")}),
               [](const QJsonObject &args, const ToolDone &done) {
                   done({{QStringLiteral("matches"), QJsonArray{QStringLiteral("app-misc/jq")}},
                         {QStringLiteral("query"), args.value(QStringLiteral("query"))}});
               }});
        return r;
    }

private Q_SLOTS:
    void toolLoopThenAnswer()
    {
        FakeOpenAi server;
        server.script = {FakeOpenAi::toolCall(QStringLiteral("find_packages"), QStringLiteral("{\"query\":\"jq\"}")),
                         FakeOpenAi::text(QStringLiteral("jq is app-misc/jq."))};
        ToolRegistry tools = registry();
        AgentSession session(nullptr, &tools);
        session.setRemoteEndpoints({Endpoint{QStringLiteral("remote"), QStringLiteral("Fake"),
                                             QUrl(server.baseUrl()), QStringLiteral("k"), QStringLiteral("m"), 2}});
        session.useEndpoint(QStringLiteral("remote"));
        session.setSystemContextProvider([] { return QStringLiteral("arch amd64"); });
        session.setRequestHintProvider([](const QString &) { return QStringLiteral("jq = app-misc/jq"); });
        QSignalSpy answered(&session, &AgentSession::answered);
        session.send(QStringLiteral("what is jq?"));
        QVERIFY(answered.wait(5000));
        QCOMPARE(answered.first().first().toString(), QStringLiteral("jq is app-misc/jq."));
        QCOMPARE(server.requests.size(), 2);
        // Second request carries the tool result.
        const QJsonArray msgs = server.requests.at(1).value(QStringLiteral("messages")).toArray();
        QCOMPARE(msgs.last().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
        QVERIFY(msgs.last().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("app-misc/jq")));
        // System prompt and deterministic hints were sent.
        QVERIFY(msgs.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("arch amd64")));
        QVERIFY(msgs.at(1).toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("jq = app-misc/jq")));
        QVERIFY(server.requests.first().contains(QStringLiteral("tools")));
        QVERIFY(!session.busy());
        // user, note (endpoint switch happened before), tool, assistant
        QCOMPARE(session.transcript().last().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("assistant"));
    }
    void unknownToolAndBadJsonAreAnswered()
    {
        FakeOpenAi server;
        server.script = {FakeOpenAi::toolCall(QStringLiteral("no_such_tool"), QStringLiteral("{}")),
                         FakeOpenAi::toolCall(QStringLiteral("find_packages"), QStringLiteral("{not json")),
                         FakeOpenAi::text(QStringLiteral("ok"))};
        ToolRegistry tools = registry();
        AgentSession session(nullptr, &tools);
        session.setRemoteEndpoints({Endpoint{QStringLiteral("remote"), QStringLiteral("Fake"),
                                             QUrl(server.baseUrl()), {}, QStringLiteral("m"), 2}});
        session.useEndpoint(QStringLiteral("remote"));
        QSignalSpy answered(&session, &AgentSession::answered);
        session.send(QStringLiteral("x"));
        QVERIFY(answered.wait(5000));
        const QJsonArray msgs = server.requests.at(2).value(QStringLiteral("messages")).toArray();
        QStringList toolContents;
        for (const auto &m : msgs) {
            if (m.toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")) {
                toolContents.append(m.toObject().value(QStringLiteral("content")).toString());
            }
        }
        QCOMPARE(toolContents.size(), 2);
        QVERIFY(toolContents.at(0).contains(QStringLiteral("unknown tool")));
        QVERIFY(toolContents.at(1).contains(QStringLiteral("not a JSON object")));
    }
    void repeatedCallStops()
    {
        FakeOpenAi server;
        for (int i = 0; i < 4; ++i) {
            server.script.append(FakeOpenAi::toolCall(QStringLiteral("find_packages"), QStringLiteral("{\"query\":\"x\"}")));
        }
        ToolRegistry tools = registry();
        AgentSession session(nullptr, &tools);
        session.setRemoteEndpoints({Endpoint{QStringLiteral("remote"), QStringLiteral("Fake"),
                                             QUrl(server.baseUrl()), {}, QStringLiteral("m"), 2}});
        session.useEndpoint(QStringLiteral("remote"));
        QSignalSpy busy(&session, &AgentSession::busyChanged);
        session.send(QStringLiteral("x"));
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy() && busy.size() >= 2, 5000);
        QVERIFY(session.escalationSuggested());
        QCOMPARE(server.requests.size(), 3);
    }
};

QTEST_GUILESS_MAIN(TstAgentSession)
#include "tst_agentsession.moc"
