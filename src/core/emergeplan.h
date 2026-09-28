// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

struct UseFlagState
{
    QString flag;          // USE_EXPAND flags are prefixed: "abi_x86_32"
    bool enabled = false;
    bool changed = false;  // "*": differs from the installed package
    bool added = false;    // "%": newly in IUSE
    bool locked = false;   // "(...)": forced or masked by the profile
};

// One line of the merge list.
struct MergeItem
{
    enum Action { New, NewSlot, Upgrade, Downgrade, Reinstall, Rebuild, NoMerge, Block, Uninstall };

    Action action = New;
    QString kind;          // "ebuild", "binary", "nomerge", "blocks", "uninstall"
    QString flags;         // the raw flag columns
    QString key;           // category/name
    QString version;
    QString slot;
    QString repository;
    QString oldVersion;    // installed version this replaces
    QString oldSlot;
    int buildId = 0;       // binary package build id
    int depth = 0;         // --tree indent
    bool binary = false;
    bool keywordMasked = false; // "~" or "*": needs an accept_keywords entry
    bool fetchRestricted = false;
    bool interactive = false;
    bool blockerResolved = true;
    QString blocker;       // for Block items: the blocking atom and message
    QVector<UseFlagState> use;
    qint64 downloadKiB = -1;

    QString cpv() const { return key + QLatin1Char('-') + version; }
    QStringList changedFlags() const;
    static QString actionName(Action a);
};

// A change that would make the request resolvable.
struct Fix
{
    enum Kind { SetUse, AcceptKeyword, AcceptLicense, Unmask, RebuildOrUpdate, TryAlternative, Other };
    Kind kind = Other;
    QString atom;
    QStringList values;    // flags ("+x", "-y"), keywords, licenses
    QString reason;        // "# required by" chain or explanation
    static QString kindName(Kind k);
};

struct Problem
{
    enum Kind { SlotConflict, Blocker, Masked, RequiredUse, UseChange, KeywordChange, LicenseChange,
                MaskChange, NotFound, Unsatisfied, Other };
    Kind kind = Other;
    QString title;
    QStringList packages;
    QString detail;        // the relevant resolver text
    QVector<Fix> fixes;    // candidate fixes, most direct first
    static QString kindName(Kind k);
};

struct EmergePlan
{
    QVector<MergeItem> items;
    QVector<Problem> problems;
    QString summary;       // the "Total: …" line
    int total = 0, upgrades = 0, downgrades = 0, newPackages = 0, newSlots = 0, reinstalls = 0,
        uninstalls = 0, binaries = 0;
    qint64 downloadKiB = 0;
    QStringList rebuildCauses; // "perl-5.44.0 causes rebuilds for 74 packages"
    bool resolved() const;
    // Items that would actually be merged (not nomerge/blocks).
    QVector<MergeItem> merges() const;

    // Compact form for the assistant: counts, merges (truncated), problems.
    QJsonObject toJson(int maxItems = 60) const;
};

class EmergePlanParser
{
public:
    static EmergePlan parse(const QString &output);
    static MergeItem parseItemLine(const QString &line, bool *ok);
    static QVector<Fix> fixesForRequiredUse(const QString &atom, const QString &constraints,
                                             const QStringList &enabled);
};

} // namespace QindaPortage::Core
