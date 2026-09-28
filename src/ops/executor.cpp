// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/executor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace QindaPortage::Ops {

Executor::Executor(QObject *parent)
    : QObject(parent)
    , m_command(defaultCommand())
{
}

Executor::Command Executor::defaultCommand(const QString &sourceTree)
{
    const QString installed = QString::fromLatin1(InstalledHelper);
    if (QFileInfo(installed).isExecutable()) return {QStringLiteral("pkexec"), {installed}};
    QString tree = sourceTree;
#ifdef QINDAPORTAGE_SOURCE_DIR
    if (tree.isEmpty()) tree = QStringLiteral(QINDAPORTAGE_SOURCE_DIR);
#endif
    // AGENT-NOTE: development only. sudo -n never prompts; without a
    // passwordless sudo rule the helper fails to start and says so.
    return {QStringLiteral("sudo"), {QStringLiteral("-n"), tree + QStringLiteral("/helper/qindaportage-apply")}};
}

bool Executor::start(const Plan &plan, QString *error)
{
    QString why;
    if (!plan.validate(&why)) {
        if (error) *error = why;
        return false;
    }
    Plan copy = plan;
    if (copy.id.isEmpty()) copy.id = Plan::newId();
    if (!copy.created.isValid()) copy.created = QDateTime::currentDateTimeUtc();
    m_txn = copy.id;
    return launch({QStringLiteral("apply")}, copy.toJsonBytes() + '\n',
                  QJsonObject{{QStringLiteral("command"), QStringLiteral("apply")}, {QStringLiteral("plan"), copy.toJson()}},
                  error);
}

bool Executor::revert(const QString &txn, QString *error)
{
    static const QRegularExpression re(QStringLiteral("^[0-9]{8}-[0-9]{6}-[0-9a-f]{4}$"));
    if (!re.match(txn).hasMatch()) {
        if (error) *error = QStringLiteral("bad transaction id");
        return false;
    }
    m_txn = txn;
    return launch({QStringLiteral("revert"), txn}, {},
                  QJsonObject{{QStringLiteral("command"), QStringLiteral("revert")}, {QStringLiteral("txn"), txn}}, error);
}

bool Executor::launch(const QStringList &subcommand, const QByteArray &input, const QJsonObject &request,
                      QString *error)
{
    const bool session = m_sessionIdleSeconds > 0;
    if (m_busy || (!session && m_process)) {
        if (error) *error = QStringLiteral("a transaction is already running");
        return false;
    }
    Q_EMIT aboutToStart();
    m_stderrTail.clear();
    m_gotResult = false;
    if (!m_process) {
        m_buffer.clear();
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &Executor::readOutput);
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            const QStringList lines = QString::fromUtf8(m_process->readAllStandardError())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &l : lines) {
                Q_EMIT logLine(l);
                m_stderrTail.append(l);
            }
            while (m_stderrTail.size() > 8) m_stderrTail.removeFirst();
        });
        connect(m_process, &QProcess::finished, this, &Executor::processFinished);
        const QStringList args = session
            ? QStringList{QStringLiteral("--idle-timeout"), QString::number(m_sessionIdleSeconds), QStringLiteral("serve")}
            : subcommand;
        m_process->start(m_command.program, m_command.arguments + args);
        if (!m_process->waitForStarted(10000)) {
            const QString why = QStringLiteral("cannot start %1: %2").arg(m_command.program, m_process->errorString());
            delete m_process;
            m_process = nullptr;
            if (error) *error = why;
            return false;
        }
        if (session) Q_EMIT sessionChanged();
    }
    if (session) {
        m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    } else {
        if (!input.isEmpty()) m_process->write(input);
        m_process->closeWriteChannel();
    }
    m_busy = true;
    Q_EMIT runningChanged();
    return true;
}

void Executor::endSession()
{
    // EOF on its stdin ends the helper's serve loop; it runs as root, so it
    // is asked to leave rather than signalled.
    if (m_process && !m_busy && m_sessionIdleSeconds > 0) m_process->closeWriteChannel();
}

Executor::~Executor()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->closeWriteChannel();
        m_process->waitForFinished(3000);
    }
}

void Executor::readOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (!line.trimmed().isEmpty()) handleLine(line);
    }
}

void Executor::handleLine(const QByteArray &line)
{
    // AGENT-CONTRACT: event shapes documented in docs/helper.md.
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        Q_EMIT logLine(QString::fromUtf8(line));
        return;
    }
    const QJsonObject o = doc.object();
    const QString event = o.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("step")) {
        Q_EMIT stepStarted(o.value(QStringLiteral("index")).toInt(), o.value(QStringLiteral("text")).toString());
    } else if (event == QLatin1String("log")) {
        Q_EMIT logLine(o.value(QStringLiteral("line")).toString());
    } else if (event == QLatin1String("undo")) {
        QStringList commands;
        for (const QJsonValue &v : o.value(QStringLiteral("commands")).toArray()) commands.append(v.toString());
        Q_EMIT undoSuggested(commands);
    } else if (event == QLatin1String("result")) {
        m_gotResult = true;
        const QString txn = o.value(QStringLiteral("txn")).toString();
        if (!txn.isEmpty()) m_txn = txn;
        // In a session the helper stays; the transaction is over now.
        if (m_sessionIdleSeconds > 0) {
            m_busy = false;
            Q_EMIT runningChanged();
        }
        Q_EMIT finished(o.value(QStringLiteral("ok")).toBool(), m_txn, o.value(QStringLiteral("error")).toString());
    } else if (event == QLatin1String("bye")) {
        Q_EMIT logLine(QStringLiteral("root session ended (%1)").arg(o.value(QStringLiteral("reason")).toString()));
    }
}

void Executor::processFinished(int exitCode, QProcess::ExitStatus status)
{
    readOutput();
    if (!m_buffer.trimmed().isEmpty()) handleLine(m_buffer);
    m_buffer.clear();
    if (m_busy && !m_gotResult) {
        // pkexec: 126 = dismissed, 127 = not authorized; sudo -n: 1.
        QString why = status == QProcess::CrashExit ? QStringLiteral("the helper was killed")
                                                    : QStringLiteral("the helper exited with %1").arg(exitCode);
        if (m_command.program == QLatin1String("pkexec") && (exitCode == 126 || exitCode == 127)) {
            why = QStringLiteral("authorization was not granted");
        }
        if (!m_stderrTail.isEmpty()) why += QStringLiteral(": ") + m_stderrTail.join(QLatin1Char(' '));
        Q_EMIT finished(false, m_txn, why);
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_busy = false;
    Q_EMIT runningChanged();
    if (m_sessionIdleSeconds > 0) Q_EMIT sessionChanged();
}

void Executor::cancel()
{
    if (m_process) m_process->terminate();
}

QVector<QJsonObject> Executor::journal(const QString &dir)
{
    QVector<QJsonObject> out;
    QDir d(dir);
    for (const QString &name : d.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed)) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) out.append(doc.object());
    }
    return out;
}

} // namespace QindaPortage::Ops
