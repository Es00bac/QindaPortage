// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Ops {

// One change a plan makes. Which fields a kind uses:
//
//   SetUse         atom, values = flags ("x", "+x", "-x")
//   AcceptKeyword  atom, values = keywords ("~amd64", "**")
//   AcceptLicense  atom, values = license names or @groups
//   Unmask / Mask  atom
//   AddPatch       atom = "cat/pn" or "=cat/pn-ver", path = file name
//                  ("fix-gcc15.patch"), content = the patch
//   WriteEbuild    path = "cat/pn/<file>" inside the qinda-local overlay
//                  (pn-ver.ebuild, metadata.xml, files/...), content
//   Emerge         values = atoms or @sets, options = emerge options
//   Unmerge        values = atoms; safe removal: deselect + depclean, so
//                  it fails instead of breaking dependents
//   Depclean       values = atoms (empty: whole system)
//   Sync           values = repository names (empty: all)
//
// AGENT-CONTRACT: the JSON form (toJson/fromJson) is what
// helper/qindaportage-apply reads on stdin; kinds are the lower-case names
// from kindName(). The helper re-validates with the same rules
// (helper/qportage_validate.py); change both and both tests together.
struct PlanStep
{
    enum Kind { SetUse, AcceptKeyword, AcceptLicense, Unmask, Mask, AddPatch, WriteEbuild, Emerge,
                Unmerge, Depclean, Sync };

    Kind kind = Emerge;
    QString atom;
    QStringList values;
    QString rationale;
    QString path;
    QString content;
    QStringList options;

    static QString kindName(Kind kind);
    static bool kindFromName(const QString &name, Kind *kind);
    bool isConfigChange() const { return kind <= Mask; }
    bool isBuild() const { return kind >= Emerge; }

    QJsonObject toJson() const;
    static PlanStep fromJson(const QJsonObject &o, bool *ok);
    // Empty when valid, else why not.
    QString validate() const;
    // One human line: "Enable ssl on dev-libs/baz (package.use)".
    QString describe() const;
    // The package.* file kind a config step writes ("package.use"), or empty.
    QString configFile() const;
    // The "atom tokens" line written to the config file.
    QString configLine() const;
};

class Plan
{
public:
    QString id;         // generated when empty
    QString title;
    QString goal;       // the user's request, in their words
    QDateTime created;
    QVector<PlanStep> steps;

    static Plan fromJson(const QJsonObject &o, QString *error = nullptr);
    QJsonObject toJson() const;
    QByteArray toJsonBytes() const;

    bool isEmpty() const { return steps.isEmpty(); }
    // Validates every step; the first error is returned as "step N: why".
    bool validate(QString *error = nullptr) const;
    QStringList describe() const;

    // The text appended to each managed file, keyed by config file kind
    // ("package.use" ...). Each line gets a dated comment with its
    // rationale above it, the house style of /etc/portage fragments.
    QMap<QString, QString> configFragments(const QDate &date = QDate::currentDate()) const;
    // Atoms/sets and options of the Emerge steps, merged, for `--pretend`.
    QStringList emergeTargets() const;
    QStringList emergeOptions() const;
    bool writesEbuilds() const;

    static QString newId();
};

namespace Validate {
bool isAtom(const QString &text, QString *why = nullptr);
bool isUseFlagToken(const QString &text);
bool isKeyword(const QString &text);
bool isLicense(const QString &text);
bool isRepoName(const QString &text);
bool isSetName(const QString &text);
// A relative path of plain components: no "..", no absolute, no empty parts.
bool isSafeRelativePath(const QString &path);
// "--opt" or "--opt=value" from the allowlist.
bool isAllowedEmergeOption(const QString &option);
QString sanitizeRationale(const QString &text);
} // namespace Validate

} // namespace QindaPortage::Ops
