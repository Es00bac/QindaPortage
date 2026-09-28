// SPDX-License-Identifier: GPL-3.0-or-later
//
// A scripted OpenAI-compatible server for tests: each POST to
// /v1/chat/completions answers with the next scripted SSE stream and records
// the request body.

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

class FakeOpenAi : public QTcpServer
{
public:
    QList<QByteArray> script;   // SSE bodies, one per request
    QList<QJsonObject> requests;

    FakeOpenAi()
    {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, s, [this, s, buffer] {
                    *buffer += s->readAll();
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QByteArray headers = buffer->left(headerEnd).toLower();
                    const int cl = headers.indexOf("content-length:");
                    const int length = cl < 0 ? 0 : headers.mid(cl + 15, headers.indexOf("\r\n", cl) - cl - 15).trimmed().toInt();
                    if (buffer->size() < headerEnd + 4 + length) return;
                    requests.append(QJsonDocument::fromJson(buffer->mid(headerEnd + 4, length)).object());
                    const QByteArray body = script.isEmpty() ? QByteArray("data: [DONE]\n\n") : script.takeFirst();
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            }
        });
    }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort()); }

    static QByteArray text(const QString &content)
    {
        return "data: " + QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{
                   {QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), content}}}}}}}).toJson(QJsonDocument::Compact)
               + "\n\ndata: [DONE]\n\n";
    }
    static QByteArray toolCall(const QString &name, const QString &args)
    {
        // Split the arguments over two chunks like real servers do.
        const QString a1 = args.left(args.size() / 2), a2 = args.mid(args.size() / 2);
        auto chunk = [](const QJsonObject &tc) {
            return "data: " + QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{
                       {QStringLiteral("delta"), QJsonObject{{QStringLiteral("tool_calls"), QJsonArray{tc}}}}}}}})
                       .toJson(QJsonDocument::Compact) + "\n\n";
        };
        return chunk(QJsonObject{{QStringLiteral("index"), 0}, {QStringLiteral("id"), QStringLiteral("c1")},
                                 {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), name},
                                                                          {QStringLiteral("arguments"), a1}}}})
               + chunk(QJsonObject{{QStringLiteral("index"), 0},
                                   {QStringLiteral("function"), QJsonObject{{QStringLiteral("arguments"), a2}}}})
               + "data: [DONE]\n\n";
    }
};
