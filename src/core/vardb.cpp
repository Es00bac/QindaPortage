// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/vardb.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace QindaPortage::Core {

namespace {

QString readSmall(const QString &dir, const char *name)
{
    QFile f(dir + QLatin1Char('/') + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QStringList words(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

} // namespace

QStringList InstalledPackage::enabledIuse() const
{
    QStringList out;
    for (const QString &flag : iuse) {
        if (use.contains(flag)) out.append(flag);
    }
    return out;
}

bool VarDb::readPackage(const QString &dir, const QString &category, const QString &pf,
                        InstalledPackage *out)
{
    InstalledPackage p;
    p.category = category;
    if (!Version::splitNameVersion(pf, &p.name, &p.versionText)) return false;
    p.version = Version::parse(p.versionText);
    p.path = dir;
    p.slot = readSmall(dir, "SLOT");
    if (p.slot.isEmpty()) p.slot = QStringLiteral("0");
    p.repository = readSmall(dir, "repository");
    p.eapi = readSmall(dir, "EAPI");
    p.description = readSmall(dir, "DESCRIPTION");
    p.homepage = readSmall(dir, "HOMEPAGE");
    p.license = readSmall(dir, "LICENSE");
    p.keywords = readSmall(dir, "KEYWORDS");
    const QStringList use = words(readSmall(dir, "USE"));
    p.use = QSet<QString>(use.begin(), use.end());
    for (QString flag : words(readSmall(dir, "IUSE"))) {
        if (flag.startsWith(QLatin1Char('+'))) {
            flag.remove(0, 1);
            p.iuseDefaultOn.insert(flag);
        } else if (flag.startsWith(QLatin1Char('-'))) {
            flag.remove(0, 1);
        }
        if (!p.iuse.contains(flag)) p.iuse.append(flag);
    }
    p.depend = readSmall(dir, "DEPEND");
    p.rdepend = readSmall(dir, "RDEPEND");
    p.bdepend = readSmall(dir, "BDEPEND");
    p.pdepend = readSmall(dir, "PDEPEND");
    p.idepend = readSmall(dir, "IDEPEND");
    p.inherited = words(readSmall(dir, "INHERITED"));
    p.size = readSmall(dir, "SIZE").toLongLong();
    p.buildTime = readSmall(dir, "BUILD_TIME").toLongLong();
    *out = std::move(p);
    return true;
}

bool VarDb::load(const QString &root)
{
    m_root = root;
    m_packages.clear();
    m_byKey.clear();
    m_byCpv.clear();
    m_error.clear();

    QDir rootDir(root);
    if (!rootDir.exists()) {
        m_error = QStringLiteral("%1 does not exist").arg(root);
        return false;
    }
    const QStringList categories = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &category : categories) {
        QDir catDir(rootDir.filePath(category));
        for (const QString &pf : catDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (pf.startsWith(QLatin1String("-MERGING-"))) continue;
            InstalledPackage pkg;
            if (readPackage(catDir.filePath(pf), category, pf, &pkg)) {
                m_packages.append(std::move(pkg));
            }
        }
    }
    std::sort(m_packages.begin(), m_packages.end(), [](const auto &a, const auto &b) {
        if (a.category != b.category) return a.category < b.category;
        if (a.name != b.name) return a.name < b.name;
        return Version::compare(a.version, b.version) < 0;
    });
    for (int i = 0; i < m_packages.size(); ++i) {
        m_byKey[m_packages.at(i).key()].append(i);
        m_byCpv.insert(m_packages.at(i).cpv(), i);
    }
    return true;
}

QVector<OwnedFile> VarDb::contents(const InstalledPackage &pkg)
{
    QVector<OwnedFile> out;
    QFile f(pkg.path + QStringLiteral("/CONTENTS"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        OwnedFile file;
        if (line.startsWith(QLatin1String("dir "))) {
            file.type = OwnedFile::Dir;
            file.path = line.mid(4);
        } else if (line.startsWith(QLatin1String("obj "))) {
            // "obj <path> <md5> <mtime>": the path may contain spaces.
            file.type = OwnedFile::File;
            const QStringList parts = line.mid(4).split(QLatin1Char(' '));
            file.path = parts.mid(0, std::max<qsizetype>(1, parts.size() - 2)).join(QLatin1Char(' '));
        } else if (line.startsWith(QLatin1String("sym "))) {
            // "sym <path> -> <target> <mtime>"
            file.type = OwnedFile::Symlink;
            const QString body = line.mid(4);
            const int arrow = body.indexOf(QStringLiteral(" -> "));
            file.path = body.left(arrow);
            const QString rest = body.mid(arrow + 4);
            file.target = rest.left(rest.lastIndexOf(QLatin1Char(' ')));
        } else {
            file.type = OwnedFile::Other;
            file.path = line.section(QLatin1Char(' '), 1, 1);
        }
        out.append(file);
    }
    return out;
}

QString VarDb::ownerOf(const QString &path) const
{
    const QByteArray needle = (QLatin1Char(' ') + path + QLatin1Char(' ')).toUtf8();
    for (const InstalledPackage &pkg : m_packages) {
        QFile f(pkg.path + QStringLiteral("/CONTENTS"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (f.readAll().contains(needle)) return pkg.cpv();
    }
    return {};
}

} // namespace QindaPortage::Core
