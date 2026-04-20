// libs/markoff/tests/tst_resourceprovider.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include "markoff/ResourceProvider.h"

class TestResourceProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testResolveImageExists();
    void testResolveImageNotFound();
    void testResolveEmbedExists();
    void testResolveEmbedNotFound();
    void testLinkExistsWithExtension();
    void testLinkExistsWithoutExtension();
    void testLinkNotExists();
};

void TestResourceProvider::testResolveImageExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("photo.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QUrl url = provider.resolveImage(QStringLiteral("photo.png"));
    QVERIFY(url.isValid());
    QVERIFY(url.isLocalFile());
    QVERIFY(url.toLocalFile().endsWith(QStringLiteral("photo.png")));
}

void TestResourceProvider::testResolveImageNotFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    QUrl url = provider.resolveImage(QStringLiteral("missing.png"));
    QVERIFY(url.isEmpty());
}

void TestResourceProvider::testResolveEmbedExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("Other Note.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    QTextStream ts(&f);
    ts << "# Other Note\n\nSome content.\n";
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    auto content = provider.resolveEmbed(QStringLiteral("Other Note"));
    QVERIFY(content.has_value());
    QVERIFY(content->contains(QStringLiteral("Some content.")));
}

void TestResourceProvider::testResolveEmbedNotFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    auto content = provider.resolveEmbed(QStringLiteral("Missing Note"));
    QVERIFY(!content.has_value());
}

void TestResourceProvider::testLinkExistsWithExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("MyNote.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(provider.linkExists(QStringLiteral("MyNote.md")));
}

void TestResourceProvider::testLinkExistsWithoutExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("MyNote.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(provider.linkExists(QStringLiteral("MyNote")));
}

void TestResourceProvider::testLinkNotExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Markoff::FilesystemResourceProvider provider(dir.path());
    QVERIFY(!provider.linkExists(QStringLiteral("Nope")));
}

QTEST_MAIN(TestResourceProvider)
#include "tst_resourceprovider.moc"
