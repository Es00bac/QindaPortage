// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/llmclient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace QindaPortage::Agent {

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void LlmClient::resetAccumulator()
{
    m_buffer.clear();
    m_content.clear();
    m_reasoning.clear();
    m_calls.clear();
    m_timings = {};
    m_finishReason.clear();
}

void LlmClient::complete(const Endpoint &endpoint, const QJsonArray &messages, const QJsonArray &tools,
                         const Options &options)
{
    abort();
    resetAccumulator();
    QJsonObject body{{QStringLiteral("model"), endpoint.model},
                     {QStringLiteral("messages"), messages},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("temperature"), options.temperature},
                     {QStringLiteral("max_tokens"), options.maxTokens}};
    if (!tools.isEmpty()) body[QStringLiteral("tools")] = tools;
    // AGENT-NOTE: llama-server applies per-request template kwargs; Qwen3.5
    // honours enable_thinking. Other servers ignore the unknown key.
    body[QStringLiteral("chat_template_kwargs")] = QJsonObject{{QStringLiteral("enable_thinking"), options.thinking}};
    body[QStringLiteral("cache_prompt")] = true;

    QNetworkRequest request(QUrl(endpoint.baseUrl.toString() + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!endpoint.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + endpoint.apiKey.toUtf8());
    }
    // A long prompt on the iGPU can take minutes before the first byte.
    request.setTransferTimeout(10 * 60 * 1000);
    m_reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &LlmClient::readStream);
    connect(m_reply, &QNetworkReply::finished, this, &LlmClient::onFinished);
}

void LlmClient::abort()
{
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void LlmClient::readStream()
{
    if (!m_reply) return;
    m_buffer += m_reply->readAll();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl).trimmed();
        m_buffer.remove(0, nl + 1);
        if (!line.startsWith("data:")) continue;
        const QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") continue;
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isObject()) feedChunk(doc.object());
    }
}

void LlmClient::feedChunk(const QJsonObject &chunk)
{
    if (chunk.contains(QStringLiteral("timings"))) m_timings = chunk.value(QStringLiteral("timings")).toObject();
    const QJsonArray choices = chunk.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return;
    const QJsonObject choice = choices.first().toObject();
    const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
    if (!finish.isEmpty()) m_finishReason = finish;
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    const QString text = delta.value(QStringLiteral("content")).toString();
    if (!text.isEmpty()) {
        m_content += text;
        Q_EMIT contentDelta(text);
    }
    const QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
    if (!reasoning.isEmpty()) {
        m_reasoning += reasoning;
        Q_EMIT reasoningDelta(reasoning);
    }
    for (const QJsonValue &v : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject tc = v.toObject();
        const int index = tc.value(QStringLiteral("index")).toInt(m_calls.size());
        while (m_calls.size() <= index) m_calls.append(ToolCall{});
        ToolCall &call = m_calls[index];
        const QString id = tc.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) call.id = id;
        const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) call.name = name;
        call.arguments += fn.value(QStringLiteral("arguments")).toString();
    }
}

void LlmClient::onFinished()
{
    if (!m_reply) return;
    QNetworkReply *reply = m_reply;
    m_reply.clear();
    reply->deleteLater();
    readStream();
    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        Q_EMIT failed(QStringLiteral("%1 %2").arg(reply->errorString(), QString::fromUtf8(body.left(400))));
        return;
    }
    // Some servers send a final non-streamed object on errors.
    if (m_content.isEmpty() && m_calls.isEmpty() && !m_buffer.trimmed().isEmpty()) {
        const QJsonObject o = QJsonDocument::fromJson(m_buffer).object();
        if (o.contains(QStringLiteral("error"))) {
            Q_EMIT failed(QString::fromUtf8(QJsonDocument(o.value(QStringLiteral("error")).toObject()).toJson(QJsonDocument::Compact)));
            return;
        }
    }
    for (int i = 0; i < m_calls.size(); ++i) {
        if (m_calls[i].id.isEmpty()) m_calls[i].id = QStringLiteral("call_%1").arg(i);
    }
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), m_content}};
    if (!m_calls.isEmpty()) {
        QJsonArray calls;
        for (const ToolCall &c : m_calls) {
            calls.append(QJsonObject{{QStringLiteral("id"), c.id},
                                     {QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), c.name},
                                                                              {QStringLiteral("arguments"), c.arguments}}}});
        }
        message[QStringLiteral("tool_calls")] = calls;
    }
    QJsonObject timings = m_timings;
    timings[QStringLiteral("finish_reason")] = m_finishReason;
    Q_EMIT finished(message, m_calls, timings);
}

} // namespace QindaPortage::Agent
