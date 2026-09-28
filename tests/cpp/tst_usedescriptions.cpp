// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/portageconfig.h"
#include "core/repoindex.h"
#include "core/usedescriptions.h"

#include "fixtureroot.h"

#include <QTest>

using namespace QindaPortage::Core;

class TstUseDescriptions : public QObject
{
    Q_OBJECT

    QTemporaryDir root;
    PortageConfig config;
    UseDescriptions desc;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(makeFixtureRoot(root));
        QVERIFY(config.load(root.path()));
        desc.load(config);
    }
    void localFromMetadataXml()
    {
        QCOMPARE(desc.describe(QStringLiteral("oniguruma"), QStringLiteral("app-misc/jq")),
                 QStringLiteral("Use dev-libs/oniguruma for regular expression support"));
        QCOMPARE(desc.sourceOf(QStringLiteral("oniguruma"), QStringLiteral("app-misc/jq")), QStringLiteral("local"));
        // metadata.xml of a second repository, with inline markup flattened.
        QCOMPARE(desc.describe(QStringLiteral("gui"), QStringLiteral("app-misc/newtool")),
                 QStringLiteral("Build the Qt front end"));
    }
    void localFromUseLocalDesc()
    {
        QCOMPARE(desc.describe(QStringLiteral("static-libs"), QStringLiteral("app-misc/jq")),
                 QStringLiteral("Build static libraries (from use.local.desc)"));
    }
    void globalAndExpand()
    {
        QCOMPARE(desc.describe(QStringLiteral("X")), QStringLiteral("Add support for X11"));
        QCOMPARE(desc.describe(QStringLiteral("gui"), QStringLiteral("app-editors/vim")),
                 QStringLiteral("Enable a graphical interface"));
        QCOMPARE(desc.sourceOf(QStringLiteral("X")), QStringLiteral("global"));
        QCOMPARE(desc.describe(QStringLiteral("video_cards_radeonsi")),
                 QStringLiteral("Build the Radeon Southern Islands driver"));
        QCOMPARE(desc.sourceOf(QStringLiteral("video_cards_radeonsi")), QStringLiteral("expand"));
        QVERIFY(desc.expandPrefixes().contains(QStringLiteral("video_cards")));
        QVERIFY(desc.describe(QStringLiteral("nonexistent")).isEmpty());
        QVERIFY(desc.sourceOf(QStringLiteral("nonexistent")).isEmpty());
    }
    void packagesUsing()
    {
        RepoIndex index;
        QVERIFY(index.load(config));
        QCOMPARE(UseDescriptions::packagesUsing(index, QStringLiteral("gui")),
                 (QStringList{QStringLiteral("app-editors/vim"), QStringLiteral("app-misc/newtool")}));
    }
};

QTEST_GUILESS_MAIN(TstUseDescriptions)
#include "tst_usedescriptions.moc"
