// SPDX-License-Identifier: GPL-3.0-or-later
//
// BuildFiles: licenses, tag versions and categorisation hints (the
// dependency parsers are in buildfiles.cpp).
#include "ops/buildfiles.h"

#include "core/version.h"

#include <QRegularExpression>

namespace QindaPortage::Ops::BuildFiles {

bool declaresLibraryOnly(const QString &cmakeText)
{
    static const QRegularExpression lib(QStringLiteral("\\badd_library\\s*\\("), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression exe(QStringLiteral("\\badd_executable\\s*\\("), QRegularExpression::CaseInsensitiveOption);
    return lib.match(cmakeText).hasMatch() && !exe.match(cmakeText).hasMatch();
}

QString gentooLicense(const QString &spdxIn)
{
    // AGENT-NOTE: GitHub reports some licenses without -only/-or-later
    // ("GPL-3.0"); those map to the conservative "only" form. Every name on
    // the right exists in ::gentoo/licenses (checked on qinda-top).
    const QString spdx = spdxIn.trimmed();
    static const QList<QPair<const char *, const char *>> map = {
        {"MIT", "MIT"}, {"Apache-2.0", "Apache-2.0"},
        {"GPL-2.0", "GPL-2"}, {"GPL-2.0-only", "GPL-2"}, {"GPL-2.0-or-later", "GPL-2+"},
        {"GPL-3.0", "GPL-3"}, {"GPL-3.0-only", "GPL-3"}, {"GPL-3.0-or-later", "GPL-3+"},
        {"LGPL-2.1", "LGPL-2.1"}, {"LGPL-2.1-only", "LGPL-2.1"}, {"LGPL-2.1-or-later", "LGPL-2.1+"},
        {"LGPL-3.0", "LGPL-3"}, {"LGPL-3.0-only", "LGPL-3"}, {"LGPL-3.0-or-later", "LGPL-3+"},
        {"AGPL-3.0", "AGPL-3"}, {"AGPL-3.0-only", "AGPL-3"}, {"AGPL-3.0-or-later", "AGPL-3+"},
        {"BSD-3-Clause", "BSD"}, {"BSD-2-Clause", "BSD-2"}, {"0BSD", "0BSD"}, {"MPL-2.0", "MPL-2.0"},
        {"ISC", "ISC"}, {"Unlicense", "Unlicense"}, {"Zlib", "ZLIB"}, {"BSL-1.0", "Boost-1.0"},
        {"CC0-1.0", "CC0-1.0"}};
    for (const auto &[from, to] : map) {
        if (spdx.compare(QString::fromLatin1(from), Qt::CaseInsensitive) == 0) return QString::fromLatin1(to);
    }
    return {};
}

QString licenseFromText(const QString &textIn)
{
    const QString text = textIn.left(6000).simplified();
    auto has = [&](const char *s) { return text.contains(QString::fromLatin1(s), Qt::CaseInsensitive); };
    const bool later = has("any later version");
    if (has("GNU LESSER GENERAL PUBLIC LICENSE") || has("GNU Library General Public License")) {
        if (has("Version 3")) return later ? QStringLiteral("LGPL-3+") : QStringLiteral("LGPL-3");
        return later ? QStringLiteral("LGPL-2.1+") : QStringLiteral("LGPL-2.1");
    }
    if (has("GNU AFFERO GENERAL PUBLIC LICENSE")) return later ? QStringLiteral("AGPL-3+") : QStringLiteral("AGPL-3");
    if (has("GNU GENERAL PUBLIC LICENSE")) {
        if (has("Version 3")) return later ? QStringLiteral("GPL-3+") : QStringLiteral("GPL-3");
        if (has("Version 2")) return later ? QStringLiteral("GPL-2+") : QStringLiteral("GPL-2");
    }
    if (has("Apache License") && has("Version 2.0")) return QStringLiteral("Apache-2.0");
    if (has("Mozilla Public License") && has("2.0")) return QStringLiteral("MPL-2.0");
    if (has("Boost Software License")) return QStringLiteral("Boost-1.0");
    if (has("This is free and unencumbered software")) return QStringLiteral("Unlicense");
    if (has("Permission is hereby granted, free of charge")) return QStringLiteral("MIT");
    if (has("Permission to use, copy, modify, and/or distribute") || has("ISC License")) return QStringLiteral("ISC");
    if (has("Redistribution and use in source and binary forms")) {
        return has("Neither the name") || has("names of its contributors") ? QStringLiteral("BSD") : QStringLiteral("BSD-2");
    }
    if (has("provided 'as-is', without any express or implied")) return QStringLiteral("ZLIB");
    return {};
}

QString versionFromTag(const QString &tag)
{
    // Take the text from the first digit; map a/b/rc/pre suffixes to PMS.
    static const QRegularExpression re(QStringLiteral(
        "(\\d+(?:\\.\\d+)*)([a-z])?(?:[-_.]?(alpha|beta|rc|pre|a|b)\\.?(\\d*))?$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(tag.trimmed());
    if (!m.hasMatch()) return {};
    // Everything before the match must be a non-numeric prefix ("v", "jq-",
    // "release-"), never part of the version ("1.2-3" would be ambiguous).
    const QString prefix = tag.trimmed().left(m.capturedStart());
    for (const QChar c : prefix) if (c.isDigit()) return {};
    QString v = m.captured(1);
    if (!m.captured(2).isEmpty() && m.captured(3).isEmpty()) v += m.captured(2).toLower();
    QString suffix = m.captured(3).toLower();
    if (suffix == QLatin1String("a")) suffix = QStringLiteral("alpha");
    if (suffix == QLatin1String("b")) suffix = QStringLiteral("beta");
    if (!suffix.isEmpty()) v += QLatin1Char('_') + suffix + m.captured(4);
    return Core::Version::parse(v).isValid() ? v : QString();
}

} // namespace QindaPortage::Ops::BuildFiles
