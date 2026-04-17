// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <markoff-parser/LinkTextParser.h>

using namespace Markoff;

class TestLinktext : public QObject {
    Q_OBJECT
private slots:
    void pathWithHeading();
    void pathWithBlockRef();
    void pathOnly();
    void headingOnly();
    void blockRefOnly();
    void emptyString();
    void pathWithSpacesInSubpath();
    void multipleHashes();
};

void TestLinktext::pathWithHeading()
{
    auto r = parseLinktext(QStringLiteral("Note#Heading"));
    QCOMPARE(r.path, QStringLiteral("Note"));
    QCOMPARE(r.subpath, QStringLiteral("#Heading"));
}

void TestLinktext::pathWithBlockRef()
{
    auto r = parseLinktext(QStringLiteral("Note#^blockid"));
    QCOMPARE(r.path, QStringLiteral("Note"));
    QCOMPARE(r.subpath, QStringLiteral("#^blockid"));
}

void TestLinktext::pathOnly()
{
    auto r = parseLinktext(QStringLiteral("Note"));
    QCOMPARE(r.path, QStringLiteral("Note"));
    QCOMPARE(r.subpath, QString());
}

void TestLinktext::headingOnly()
{
    auto r = parseLinktext(QStringLiteral("#Heading"));
    QCOMPARE(r.path, QString());
    QCOMPARE(r.subpath, QStringLiteral("#Heading"));
}

void TestLinktext::blockRefOnly()
{
    auto r = parseLinktext(QStringLiteral("#^block"));
    QCOMPARE(r.path, QString());
    QCOMPARE(r.subpath, QStringLiteral("#^block"));
}

void TestLinktext::emptyString()
{
    auto r = parseLinktext(QString());
    QCOMPARE(r.path, QString());
    QCOMPARE(r.subpath, QString());
}

void TestLinktext::pathWithSpacesInSubpath()
{
    auto r = parseLinktext(QStringLiteral("Note.md#Sub heading with spaces"));
    QCOMPARE(r.path, QStringLiteral("Note.md"));
    QCOMPARE(r.subpath, QStringLiteral("#Sub heading with spaces"));
}

void TestLinktext::multipleHashes()
{
    auto r = parseLinktext(QStringLiteral("Note#First#Second"));
    QCOMPARE(r.path, QStringLiteral("Note"));
    QCOMPARE(r.subpath, QStringLiteral("#First#Second"));
}

QTEST_APPLESS_MAIN(TestLinktext)
#include "tst_linktext.moc"
