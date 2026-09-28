// SPDX-License-Identifier: GPL-3.0-or-later
//
// AgentSession runs the assistant's tool loop: user text -> model -> tool
// calls -> tool results -> model ... -> an answer. It owns the transcript
// the Assistant dock shows and decides when to escalate a tier
// (docs/design.md "The assistant").

#pragma once

#include "agent/llmclient.h"
#include "agent/toolregistry.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QVariantList>

#include <functional>

namespace QindaPortage::Agent {

class LlmRuntime;

class AgentSession final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingChanged)
    Q_PROPERTY(QString endpointLabel READ endpointLabel NOTIFY endpointChanged)
    Q_PROPERTY(int tier READ tier NOTIFY endpointChanged)
    Q_PROPERTY(bool escalationSuggested READ escalationSuggested NOTIFY statusChanged)
    Q_PROPERTY(bool thinking READ thinking WRITE setThinking NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList endpoints READ endpointList NOTIFY endpointChanged)

public:
    explicit AgentSession(LlmRuntime *runtime, ToolRegistry *tools, QObject *parent = nullptr);

    // Tier 1 is always the local runtime; more endpoints (qinda) are added
    // by the app from settings.
    void setRemoteEndpoints(const QVector<Endpoint> &endpoints);
    // Supplies the compact system profile embedded in the system prompt.
    void setSystemContextProvider(std::function<QString()> provider) { m_contextProvider = std::move(provider); }
    // Checked when the model answers without tool calls: a non-empty result
    // is sent back as a system report for one more turn (once per request),
    // e.g. "the plan changes USE but installs nothing".
    void setAnswerCheck(std::function<QString()> check) { m_answerCheck = std::move(check); }
    // Supplies deterministic hints for a request (package names resolved in
    // code from the user's words) so the model does not have to guess them.
    void setRequestHintProvider(std::function<QString(const QString &)> provider)
    {
        m_hintProvider = std::move(provider);
    }

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList transcript() const { return m_transcript; }
    QString streamingText() const { return m_streaming; }
    QString endpointLabel() const;
    int tier() const;
    bool escalationSuggested() const { return m_escalationSuggested; }
    bool thinking() const { return m_thinking; }
    void setThinking(bool on);
    QVariantList endpointList() const;

    // The system prompt as it would be sent now (for tests and the UI's
    // "what the model sees" view).
    QString systemPrompt() const;
    const QJsonArray &messages() const { return m_messages; }

public Q_SLOTS:
    void send(const QString &text);
    // Adds a note the model will see on its next turn (e.g. "the build of
    // X failed: ..."), then runs a turn.
    void inject(const QString &text, bool runTurn);
    void stop();
    void reset();
    // Switches to endpoint `id` ("local", "qinda", …) and continues the
    // conversation there.
    void useEndpoint(const QString &id);

Q_SIGNALS:
    void busyChanged();
    void statusChanged();
    void transcriptChanged();
    void streamingChanged();
    void endpointChanged();
    void settingsChanged();
    // The model finished a request (no tool calls pending).
    void answered(const QString &text);

private:
    void setBusy(bool b);
    void setStatus(const QString &s);
    void addEntry(const QVariantMap &entry);
    void startTurn();
    void requestCompletion();
    void onFinished(const QJsonObject &message, const QVector<ToolCall> &calls, const QJsonObject &timings);
    void runNextTool();
    void compactHistory();
    const Endpoint &currentEndpoint() const;

    LlmRuntime *m_runtime;
    ToolRegistry *m_tools;
    LlmClient m_client;
    QVector<Endpoint> m_endpoints; // [0] = local
    int m_endpointIndex = 0;
    QJsonArray m_messages;
    QVariantList m_transcript;
    QString m_streaming;
    QString m_status;
    QVector<ToolCall> m_pendingCalls;
    QStringList m_recentCalls;  // "name args#resulthash" for loop detection
    QHash<QString, QString> m_lastResultHash;
    QString m_repeatSignature;
    std::function<QString()> m_contextProvider;
    std::function<QString(const QString &)> m_hintProvider;
    std::function<QString()> m_answerCheck;
    bool m_answerChecked = false;
    int m_iterations = 0;
    int m_generation = 0;       // bumps on stop/reset so late callbacks are ignored
    bool m_busy = false;
    bool m_escalationSuggested = false;
    bool m_thinking = false;
    bool m_waitingForModel = false;
};

} // namespace QindaPortage::Agent
