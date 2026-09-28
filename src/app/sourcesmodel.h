// SPDX-License-Identifier: GPL-3.0-or-later
//
// SourcesModel drives "install from a URL" (docs/design.md "External
// software"): probe a repository, tarball or directory, map its declared
// dependencies to Gentoo atoms, draft an ebuild for the qinda-local overlay,
// and put the ebuild and its emerge into the plan. The assistant's
// probe_source tool uses the same object, so its probes show on the page.

#pragma once

#include "ops/dependencyhints.h"
#include "ops/sourceinfo.h"
#include "ops/sourceprobe.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace QindaPortage::App {

class PlanModel;
class SystemModel;

class SourcesModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap info READ info NOTIFY resultChanged)
    Q_PROPERTY(QVariantList dependencies READ dependencies NOTIFY resultChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY resultChanged)
    Q_PROPERTY(QString packageName READ packageName NOTIFY resultChanged)
    Q_PROPERTY(QString version READ version NOTIFY resultChanged)
    Q_PROPERTY(QString ebuildPath READ ebuildPath NOTIFY resultChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY resultChanged)
    Q_PROPERTY(QStringList alreadyPackaged READ alreadyPackaged NOTIFY resultChanged)

public:
    SourcesModel(SystemModel *system, PlanModel *plan, QObject *parent = nullptr);

    bool busy() const { return m_probe.busy(); }
    QString progress() const { return m_progress; }
    QString error() const { return m_info.error; }
    QVariantMap info() const;
    QVariantList dependencies() const;
    QString category() const { return m_category; }
    void setCategory(const QString &c);
    QString packageName() const;
    QString version() const { return m_info.version; }
    QString ebuildPath() const;
    QString draft() const { return m_draft; }
    void setDraft(const QString &text);
    const Ops::SourceInfo &sourceInfo() const { return m_info; }
    // Repository packages with the probed software's name
    // ("sys-apps/hexyl-0.17.0::gentoo"): prefer them over a new ebuild.
    QStringList alreadyPackaged() const;
    const QVector<Ops::DependencyResolution> &resolutions() const { return m_resolutions; }

    Q_INVOKABLE void probe(const QString &url);
    Q_INVOKABLE void cancel();
    // Adds WriteEbuild (+ metadata.xml) and an emerge of the new package to
    // the plan; returns the reason when a step is rejected.
    Q_INVOKABLE QString addToPlan();

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void probed(bool ok);

private:
    SystemModel *m_system;
    PlanModel *m_plan;
    Ops::SourceProbe m_probe;
    Ops::SourceInfo m_info;
    QVector<Ops::DependencyResolution> m_resolutions;
    QString m_progress;
    QString m_category;
    QString m_draft;
};

} // namespace QindaPortage::App
