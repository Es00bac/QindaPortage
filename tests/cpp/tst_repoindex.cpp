// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/portageconfig.h"
#include "core/repoindex.h"

#include "fixtureroot.h"

#include <QTest>

using namespace QindaPortage::Core;

class TstRepoIndex : public QObject
{
    Q_OBJECT

    QTemporaryDir root;
    PortageConfig config;
    RepoIndex index;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(makeFixtureRoot(root));
        QVERIFY(config.load(root.path()));
        QCOMPARE(config.repositories().size(), 2);
        QVERIFY(index.load(config));
    }
    void counts()
    {
        QCOMPARE(index.count(), 5);
        QCOMPARE(index.versionCount(), 7);
        QCOMPARE(index.categories(), (QStringList{QStringLiteral("app-editors"), QStringLiteral("app-misc"),
                                                  QStringLiteral("dev-libs")}));
    }
    void cacheEntries()
    {
        const AvailablePackage *jq = index.find(QStringLiteral("app-misc/jq"));
        QVERIFY(jq);
        QCOMPARE(jq->versions.size(), 2);
        QCOMPARE(jq->versions.first().version, QStringLiteral("1.7.1"));
        QCOMPARE(jq->versions.last().version, QStringLiteral("1.8.2"));
        QCOMPARE(jq->versions.last().slot, QStringLiteral("0/1"));
        QVERIFY(jq->versions.last().exact);
        QCOMPARE(jq->versions.last().iuse, (QStringList{QStringLiteral("oniguruma"), QStringLiteral("static-libs"),
                                                         QStringLiteral("test")}));
        QCOMPARE(jq->description, QStringLiteral("A lightweight and flexible command-line JSON processor"));
        QCOMPARE(jq->homepage, QStringLiteral("https://jqlang.org/"));
        QCOMPARE(jq->repositories(), QStringList{QStringLiteral("gentoo")});
    }
    void scrapedEbuilds()
    {
        const AvailablePackage *p = index.find(QStringLiteral("app-misc/newtool"));
        QVERIFY(p);
        QCOMPARE(p->versions.size(), 2);
        QVERIFY(!p->versions.first().exact);
        QCOMPARE(p->versions.first().repo, QStringLiteral("extra"));
        QCOMPARE(p->versions.first().keywords, QStringList{QStringLiteral("~amd64")});
        QCOMPARE(p->versions.first().iuse, (QStringList{QStringLiteral("gui"), QStringLiteral("test")}));
        QCOMPARE(p->description, QStringLiteral("A new tool from an overlay"));
        const auto scraped = RepoIndex::scrapeEbuild(QStringLiteral(
            "EAPI=8\nDESCRIPTION='single quoted'\nIUSE=\"a\n\tb +c\"\nSLOT=0/2\nIUSE+=\" late\"\n"));
        QCOMPARE(scraped.value(QStringLiteral("DESCRIPTION")), QStringLiteral("single quoted"));
        QCOMPARE(scraped.value(QStringLiteral("IUSE")), QStringLiteral("a b +c"));
        QCOMPARE(scraped.value(QStringLiteral("SLOT")), QStringLiteral("0/2"));
        QCOMPARE(scraped.value(QStringLiteral("EAPI")), QStringLiteral("8"));
    }
    void masks()
    {
        const AvailablePackage *p = index.find(QStringLiteral("app-misc/masked"));
        QVERIFY(p && p->versions.first().masked);
        QVERIFY(!index.find(QStringLiteral("app-misc/jq"))->versions.last().masked);
        QVERIFY(!index.bestVisibleVersion(QStringLiteral("app-misc/masked"), {QStringLiteral("amd64")}));

        QTemporaryDir unmaskedRoot;
        QVERIFY(makeFixtureRoot(unmaskedRoot, QStringLiteral("app-misc/masked")));
        PortageConfig unmaskedConfig;
        QVERIFY(unmaskedConfig.load(unmaskedRoot.path()));
        RepoIndex unmasked;
        QVERIFY(unmasked.load(unmaskedConfig));
        QVERIFY(!unmasked.find(QStringLiteral("app-misc/masked"))->versions.first().masked);
    }
    void visibility()
    {
        const QStringList stable{QStringLiteral("amd64")};
        const QStringList testing{QStringLiteral("amd64"), QStringLiteral("~amd64")};
        QCOMPARE(index.bestVisibleVersion(QStringLiteral("app-misc/jq"), stable)->version, QStringLiteral("1.7.1"));
        QCOMPARE(index.bestVisibleVersion(QStringLiteral("app-misc/jq"), testing)->version, QStringLiteral("1.8.2"));
        QCOMPARE(index.bestVisibleVersion(QStringLiteral("app-misc/jq"), {QStringLiteral("**")})->version,
                 QStringLiteral("1.8.2"));
        // Only the profile's package.accept_keywords makes newtool-0.9 visible.
        QVERIFY(!index.bestVisibleVersion(QStringLiteral("app-misc/newtool"), stable));
        const AvailableVersion *nt = index.bestVisibleVersion(QStringLiteral("app-misc/newtool"), stable, &config);
        QVERIFY(nt);
        QCOMPARE(nt->version, QStringLiteral("0.9"));
        QVERIFY(!index.bestVisibleVersion(QStringLiteral("no/such"), testing));
    }
    void search()
    {
        const auto exact = index.search(QStringLiteral("jq"));
        QVERIFY(!exact.isEmpty());
        QCOMPARE(exact.first().key, QStringLiteral("app-misc/jq"));
        QCOMPARE(exact.first().score, 1000);
        QCOMPARE(exact.first().bestVersion, QStringLiteral("1.8.2"));
        const auto prefix = index.search(QStringLiteral("new"));
        QCOMPARE(prefix.first().key, QStringLiteral("app-misc/newtool"));
        QVERIFY(prefix.first().score >= 700);
        const auto described = index.search(QStringLiteral("text editor"));
        QCOMPARE(described.size(), 1);
        QCOMPARE(described.first().key, QStringLiteral("app-editors/vim"));
        QVERIFY(index.search(QString()).isEmpty());
        index.setInstalledLookup([](const QString &key) { return key == QLatin1String("app-misc/jq"); });
        QVERIFY(index.search(QStringLiteral("jq")).first().installed);
        index.setInstalledLookup({});
    }
    void metadata()
    {
        QVERIFY(index.metadata(QStringLiteral("app-misc/jq"), QStringLiteral("1.7.1"))
                    .value(QStringLiteral("RDEPEND")).contains(QStringLiteral("dev-libs/oniguruma")));
        QCOMPARE(index.metadata(QStringLiteral("app-misc/newtool"), QStringLiteral("1.0")).value(QStringLiteral("LICENSE")),
                 QStringLiteral("MIT"));
    }
};

QTEST_GUILESS_MAIN(TstRepoIndex)
#include "tst_repoindex.moc"
