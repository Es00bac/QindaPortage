// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs an approved Plan through the privileged helper and turns its
// JSON-lines output into signals. The GUI never changes the system itself;
// this class only starts `qindaportage-apply` (see docs/helper.md).
//
// AGENT-CONTRACT: aboutToStart() is emitted synchronously before the helper
// is launched; the app connects it to LlmRuntime::unload() (src/agent) so
// the model's RAM and GPU memory are free before emerge starts building.

#pragma once

#include "ops/plan.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QVector>

namespace QindaPortage::Ops {

class Executor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    // The helper invocation without its subcommand, e.g.
    // {"pkexec", {"/usr/libexec/qindaportage/qindaportage-apply"}}.
    struct Command
    {
        QString program;
        QStringList arguments;
    };

    static constexpr const char *InstalledHelper = "/usr/libexec/qindaportage/qindaportage-apply";
    static constexpr const char *JournalDir = "/var/lib/qindaportage/journal";

    explicit Executor(QObject *parent = nullptr);
    ~Executor() override;

    // pkexec + the installed helper when it exists; otherwise `sudo -n` +
    // `<sourceTree>/helper/qindaportage-apply` (development tree).
    static Command defaultCommand(const QString &sourceTree = QString());
    void setCommand(const Command &command) { m_command = command; }
    Command command() const { return m_command; }

    // Validates and starts `apply`; false when invalid or already running.
    bool start(const Plan &plan, QString *error = nullptr);
    bool revert(const QString &txn, QString *error = nullptr);
    // Terminates the helper (it journals the transaction as interrupted).
    void cancel();
    bool isRunning() const { return m_process != nullptr; }
    QString currentTxn() const { return m_txn; }

    // Journal entries, newest first. Unreadable or malformed files are skipped.
    static QVector<QJsonObject> journal(const QString &dir = QString::fromLatin1(JournalDir));

Q_SIGNALS:
    void aboutToStart();
    void runningChanged();
    void stepStarted(int index, const QString &text);
    void logLine(const QString &line);
    // Revert only: the helper's suggested commands to undo merged packages.
    void undoSuggested(const QStringList &commands);
    void finished(bool ok, const QString &txn, const QString &error);

private:
    bool launch(const QStringList &subcommand, const QByteArray &input, QString *error);
    void readOutput();
    void handleLine(const QByteArray &line);
    void processFinished(int exitCode, QProcess::ExitStatus status);

    Command m_command;
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    QString m_txn;
    bool m_gotResult = false;
    QStringList m_stderrTail;
};

} // namespace QindaPortage::Ops
