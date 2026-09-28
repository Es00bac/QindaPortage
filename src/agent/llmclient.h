// SPDX-License-Identifier: GPL-3.0-or-later
//
// One streaming OpenAI-compatible chat completion at a time: the local
// llama-server (tier 1) or qinda's qinda-llm router (tier 2). Tool-call
// deltas are accumulated into complete calls.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace QindaPortage::Agent {

struct Endpoint
{
    QString id;       // "local", "qinda"
    QString label;    // shown in the UI
    QUrl baseUrl;     // ".../v1"
    QString apiKey;   // empty for the local server
    QString model;
    int tier = 1;
};

struct ToolCall
{
    QString id;
    QString name;
    QString arguments; // JSON text as produced by the model
};

class LlmClient final : public QObject
{
    Q_OBJECT

public:
    explicit LlmClient(QObject *parent = nullptr);

    struct Options
    {
        double temperature = 0.2;
        bool thinking = false;
        int maxTokens = 1536;
    };

    // Starts a request; any running one is aborted first.
    void complete(const Endpoint &endpoint, const QJsonArray &messages, const QJsonArray &tools,
                  const Options &options);
    void abort();
    bool busy() const { return !m_reply.isNull(); }

    // Parses one SSE "data:" payload into the accumulating message (exposed
    // for tests).
    void feedChunk(const QJsonObject &chunk);
    void resetAccumulator();
    QString content() const { return m_content; }
    QVector<ToolCall> toolCalls() const { return m_calls; }

Q_SIGNALS:
    void contentDelta(const QString &text);
    void reasoningDelta(const QString &text);
    // The assistant message as it belongs in the transcript, plus timings
    // (llama.cpp "timings" object when present).
    void finished(const QJsonObject &assistantMessage, const QVector<ToolCall> &calls,
                  const QJsonObject &timings);
    void failed(const QString &error);

private:
    void readStream();
    void onFinished();

    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
    QString m_content;
    QString m_reasoning;
    QVector<ToolCall> m_calls;
    QJsonObject m_timings;
    QString m_finishReason;
};

} // namespace QindaPortage::Agent
