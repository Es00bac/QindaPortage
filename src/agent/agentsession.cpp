// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/agentsession.h"

#include "agent/llmruntime.h"

#include <QDateTime>
#include <QJsonDocument>

namespace QindaPortage::Agent {

namespace {

constexpr int MaxIterations = 16;
// AGENT-NOTE: ~4 characters per token; the local context is 16k tokens and
// the reply needs room, so history is compacted above ~44k characters.
constexpr int CompactAboveChars = 44000;

// AGENT-CONTRACT: the tool names referenced here must exist in the app's
// Toolbox (src/app/toolbox.cpp); tst_agentsession checks the prompt text.
const char *const Rules = R"(You are the package manager of a Gentoo Linux machine. You work through tools; the code behind them is exact, you are the planner.

Rules:
- Never guess a package name, version or USE flag. Use find_packages / package_info first. Tool errors include "didYouMean": use it.
- "+flag" enables a flag, "-flag" disables it.
- Before proposing any change, run pretend with the change applied and read its problems and fixes. Repeat until "resolved" is true.
- Changes only go into the draft plan through plan_* tools; nothing is installed until the user approves the plan. Give every plan step a one-line rationale a human would accept.
- A plan that installs, rebuilds or removes something must contain the plan_emerge / plan_remove step that does it, not only the configuration changes.
- Prefer, in order: a binary package or the stable version; a USE change on the specific package; an accept_keywords entry for the specific version; a patch; a fixed ebuild in the qinda-local overlay. Never change global USE or ACCEPT_KEYWORDS.
- Software that is not in any repository is packaged with probe_source then plan_ebuild in the qinda-local overlay, so Portage tracks it. Never suggest pip, npm -g, curl|sh or make install.
- If something is incompatible, do not refuse: find why (build_log, web_fetch of bug reports, upstream patches) and make it work with a patch or a fixed ebuild.
- If you are stuck after a few attempts, say so plainly and suggest escalating.
- Answer briefly: what you will do, why, and what the user must approve.)";

QString shortJson(const QJsonObject &o, int max)
{
    QString text = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (text.size() > max) text = text.left(max) + QStringLiteral("…");
    return text;
}

} // namespace

AgentSession::AgentSession(LlmRuntime *runtime, ToolRegistry *tools, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_tools(tools)
{
    m_endpoints.append(Endpoint{QStringLiteral("local"), QStringLiteral("Local model"), {}, {}, {}, 1});
    connect(&m_client, &LlmClient::contentDelta, this, [this](const QString &t) {
        m_streaming += t;
        Q_EMIT streamingChanged();
    });
    connect(&m_client, &LlmClient::reasoningDelta, this, [this](const QString &) {
        setStatus(QStringLiteral("thinking…"));
    });
    connect(&m_client, &LlmClient::finished, this, &AgentSession::onFinished);
    connect(&m_client, &LlmClient::failed, this, [this](const QString &error) {
        addEntry({{QStringLiteral("kind"), QStringLiteral("error")}, {QStringLiteral("text"), error}});
        m_escalationSuggested = currentEndpoint().tier < 2;
        setStatus(QStringLiteral("model request failed"));
        setBusy(false);
    });
    if (m_runtime) {
        connect(m_runtime, &LlmRuntime::ready, this, [this] {
            if (m_waitingForModel) {
                m_waitingForModel = false;
                requestCompletion();
            }
        });
        connect(m_runtime, &LlmRuntime::failed, this, [this](const QString &error) {
            if (!m_waitingForModel) return;
            m_waitingForModel = false;
            addEntry({{QStringLiteral("kind"), QStringLiteral("error")},
                      {QStringLiteral("text"), QStringLiteral("The local model could not load: %1").arg(error)}});
            m_escalationSuggested = true;
            setStatus(QStringLiteral("local model unavailable"));
            setBusy(false);
        });
    }
}

void AgentSession::setRemoteEndpoints(const QVector<Endpoint> &endpoints)
{
    m_endpoints.resize(1);
    m_endpoints += endpoints;
    if (m_endpointIndex >= m_endpoints.size()) m_endpointIndex = 0;
    Q_EMIT endpointChanged();
}

const Endpoint &AgentSession::currentEndpoint() const
{
    return m_endpoints.at(m_endpointIndex);
}

QString AgentSession::endpointLabel() const
{
    if (m_endpointIndex == 0 && m_runtime && !m_runtime->modelLabel().isEmpty()) {
        return m_runtime->modelLabel();
    }
    return currentEndpoint().label;
}

int AgentSession::tier() const
{
    return currentEndpoint().tier;
}

QVariantList AgentSession::endpointList() const
{
    QVariantList out;
    for (const Endpoint &e : m_endpoints) {
        out.append(QVariantMap{{QStringLiteral("id"), e.id}, {QStringLiteral("label"), e.label},
                               {QStringLiteral("tier"), e.tier}});
    }
    return out;
}

void AgentSession::setThinking(bool on)
{
    if (on == m_thinking) return;
    m_thinking = on;
    Q_EMIT settingsChanged();
}

QString AgentSession::systemPrompt() const
{
    QString prompt = QString::fromUtf8(Rules);
    if (m_contextProvider) {
        prompt += QStringLiteral("\n\nThis machine:\n") + m_contextProvider();
    }
    prompt += QStringLiteral("\n\nToday is %1.").arg(QDate::currentDate().toString(Qt::ISODate));
    return prompt;
}

void AgentSession::setBusy(bool b)
{
    if (b == m_busy) return;
    m_busy = b;
    Q_EMIT busyChanged();
}

void AgentSession::setStatus(const QString &s)
{
    m_status = s;
    Q_EMIT statusChanged();
}

void AgentSession::addEntry(const QVariantMap &entry)
{
    QVariantMap e = entry;
    e.insert(QStringLiteral("time"), QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    m_transcript.append(e);
    Q_EMIT transcriptChanged();
}

void AgentSession::send(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_busy) return;
    if (m_messages.isEmpty()) {
        m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                      {QStringLiteral("content"), systemPrompt()}});
    }
    QString content = trimmed;
    if (m_hintProvider) {
        const QString hints = m_hintProvider(trimmed);
        if (!hints.isEmpty()) content += QStringLiteral("\n\n[Resolved by the system, not by the user]\n") + hints;
    }
    m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}});
    addEntry({{QStringLiteral("kind"), QStringLiteral("user")}, {QStringLiteral("text"), trimmed}});
    m_escalationSuggested = false;
    m_answerChecked = false;
    m_recentCalls.clear();
    m_iterations = 0;
    startTurn();
}

void AgentSession::inject(const QString &text, bool runTurn)
{
    if (m_messages.isEmpty()) {
        m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                      {QStringLiteral("content"), systemPrompt()}});
    }
    m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                  {QStringLiteral("content"), QStringLiteral("[System report]\n") + text}});
    addEntry({{QStringLiteral("kind"), QStringLiteral("note")}, {QStringLiteral("text"), text}});
    if (runTurn && !m_busy) {
        m_iterations = 0;
        m_recentCalls.clear();
        startTurn();
    }
}

void AgentSession::startTurn()
{
    setBusy(true);
    // AGENT-NOTE: loading a model that does not fit pushes the laptop into
    // swap while the user is working; when qinda's tier is configured, use
    // it instead and say so (docs/design.md "Model choice").
    if (m_endpointIndex == 0 && m_runtime && m_runtime->state() != LlmRuntime::State::Ready
        && !m_runtime->fitsNow() && m_endpoints.size() > 1) {
        m_endpointIndex = 1;
        Q_EMIT endpointChanged();
        addEntry({{QStringLiteral("kind"), QStringLiteral("note")},
                  {QStringLiteral("text"), QStringLiteral("Only %1 GiB of memory is free, too little for a local model "
                                                          "without swapping; using %2 instead.")
                                               .arg(m_runtime->availableGiB(), 0, 'f', 1).arg(currentEndpoint().label)}});
    }
    if (m_endpointIndex == 0 && m_runtime && m_runtime->state() != LlmRuntime::State::Ready) {
        setStatus(QStringLiteral("loading the local model…"));
        m_waitingForModel = true;
        m_runtime->ensureLoaded();
        return;
    }
    requestCompletion();
}

void AgentSession::requestCompletion()
{
    compactHistory();
    Endpoint endpoint = currentEndpoint();
    if (m_endpointIndex == 0 && m_runtime) {
        endpoint.baseUrl = m_runtime->baseUrl();
        endpoint.model = m_runtime->modelId();
        m_runtime->touch();
    }
    m_streaming.clear();
    Q_EMIT streamingChanged();
    setStatus(QStringLiteral("%1 is working…").arg(endpointLabel()));
    LlmClient::Options options;
    options.thinking = m_thinking;
    m_client.complete(endpoint, m_messages, m_tools->toJson(), options);
}

void AgentSession::onFinished(const QJsonObject &message, const QVector<ToolCall> &calls, const QJsonObject &timings)
{
    if (m_runtime && m_endpointIndex == 0) m_runtime->touch();
    m_messages.append(message);
    const QString text = message.value(QStringLiteral("content")).toString().trimmed();
    m_streaming.clear();
    Q_EMIT streamingChanged();
    if (!text.isEmpty()) {
        addEntry({{QStringLiteral("kind"), QStringLiteral("assistant")}, {QStringLiteral("text"), text},
                  {QStringLiteral("tokensPerSecond"), timings.value(QStringLiteral("predicted_per_second")).toDouble()}});
    }
    if (calls.isEmpty() && m_answerCheck && !m_answerChecked) {
        const QString problem = m_answerCheck();
        if (!problem.isEmpty()) {
            m_answerChecked = true;
            m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                          {QStringLiteral("content"), QStringLiteral("[System check]\n") + problem}});
            addEntry({{QStringLiteral("kind"), QStringLiteral("note")}, {QStringLiteral("text"), problem}});
            requestCompletion();
            return;
        }
    }
    if (calls.isEmpty()) {
        setStatus(QString());
        setBusy(false);
        Q_EMIT answered(text);
        return;
    }
    if (++m_iterations > MaxIterations) {
        addEntry({{QStringLiteral("kind"), QStringLiteral("note")},
                  {QStringLiteral("text"), QStringLiteral("Stopped after %1 tool rounds without an answer.").arg(MaxIterations)}});
        m_escalationSuggested = true;
        setStatus(QStringLiteral("stuck — escalate?"));
        setBusy(false);
        return;
    }
    m_pendingCalls = calls;
    runNextTool();
}

void AgentSession::runNextTool()
{
    if (m_pendingCalls.isEmpty()) {
        requestCompletion();
        return;
    }
    const ToolCall call = m_pendingCalls.takeFirst();
    const int generation = m_generation;
    auto finish = [this, call, generation](const QJsonObject &result) {
        if (generation != m_generation) return; // stopped meanwhile
        m_messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                      {QStringLiteral("tool_call_id"), call.id},
                                      {QStringLiteral("content"),
                                       QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}});
        const ToolSpec *spec = m_tools->find(call.name);
        addEntry({{QStringLiteral("kind"), QStringLiteral("tool")},
                  {QStringLiteral("tool"), call.name},
                  {QStringLiteral("args"), call.arguments},
                  {QStringLiteral("plan"), spec && spec->mutatesPlan},
                  {QStringLiteral("ok"), !result.contains(QStringLiteral("error"))},
                  {QStringLiteral("text"), shortJson(result, 600)}});
        runNextTool();
    };

    // Loop detection: the same call three times means the model is stuck.
    const QString signature = call.name + QLatin1Char(' ') + call.arguments;
    m_recentCalls.append(signature);
    if (m_recentCalls.count(signature) >= 3) {
        m_pendingCalls.clear();
        m_escalationSuggested = true;
        addEntry({{QStringLiteral("kind"), QStringLiteral("note")},
                  {QStringLiteral("text"), QStringLiteral("The model repeated %1 three times; stopping.").arg(call.name)}});
        setStatus(QStringLiteral("stuck — escalate?"));
        setBusy(false);
        return;
    }
    const ToolSpec *spec = m_tools->find(call.name);
    if (!spec) {
        finish(QJsonObject{{QStringLiteral("error"), QStringLiteral("unknown tool %1").arg(call.name)},
                           {QStringLiteral("tools"), QJsonArray::fromStringList(m_tools->names())}});
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument args = QJsonDocument::fromJson(call.arguments.toUtf8(), &parseError);
    if (!call.arguments.trimmed().isEmpty() && !args.isObject()) {
        finish(QJsonObject{{QStringLiteral("error"),
                            QStringLiteral("arguments are not a JSON object: %1").arg(parseError.errorString())}});
        return;
    }
    setStatus(QStringLiteral("running %1…").arg(call.name));
    spec->handler(args.object(), finish);
}

void AgentSession::compactHistory()
{
    // Keep the system prompt, the latest user message and the latest tool
    // results intact; elide old tool results first, oldest first.
    auto size = [this] {
        return QJsonDocument(m_messages).toJson(QJsonDocument::Compact).size();
    };
    for (int i = 1; i < m_messages.size() - 6 && size() > CompactAboveChars; ++i) {
        QJsonObject m = m_messages.at(i).toObject();
        if (m.value(QStringLiteral("role")).toString() != QLatin1String("tool")) continue;
        if (m.value(QStringLiteral("content")).toString().size() < 200) continue;
        m[QStringLiteral("content")] = QStringLiteral("{\"elided\":\"old result removed to save context; call the tool again if needed\"}");
        m_messages[i] = m;
    }
}

void AgentSession::stop()
{
    ++m_generation;
    m_client.abort();
    m_pendingCalls.clear();
    m_waitingForModel = false;
    setStatus(QStringLiteral("stopped"));
    setBusy(false);
}

void AgentSession::reset()
{
    stop();
    m_messages = {};
    m_transcript.clear();
    m_escalationSuggested = false;
    m_endpointIndex = 0;
    setStatus(QString());
    Q_EMIT transcriptChanged();
    Q_EMIT endpointChanged();
}

void AgentSession::useEndpoint(const QString &id)
{
    for (int i = 0; i < m_endpoints.size(); ++i) {
        if (m_endpoints.at(i).id != id) continue;
        m_endpointIndex = i;
        Q_EMIT endpointChanged();
        addEntry({{QStringLiteral("kind"), QStringLiteral("note")},
                  {QStringLiteral("text"), QStringLiteral("Now using %1.").arg(endpointLabel())}});
        // The local model is not needed while a remote tier works.
        if (i != 0 && m_runtime) m_runtime->unload();
        return;
    }
}

} // namespace QindaPortage::Agent
