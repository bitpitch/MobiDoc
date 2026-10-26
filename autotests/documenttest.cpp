/*
    SPDX-FileCopyrightText: 2013 Fabio D 'Urso <fabiodurso@hotmail.it>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QMimeDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <threadweaver/queue.h>

#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/document_p.h"
#include "../core/generator.h"
#include "../core/observer.h"
#include "../core/page.h"
#include "../core/rotationjob_p.h"
#include "../settings_core.h"

class DocumentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCloseDuringRotationJob();
    void testDocdataMigration();
    void testEvaluateKeystrokeEventChange_data();
    void testEvaluateKeystrokeEventChange();
    void testDocumentBookmarkPersistence();
    void testDocumentBookmarkRenamePersistence();
};

void DocumentTest::testDocumentBookmarkPersistence()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), source));
    const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"));
    Okular::Document document(nullptr);
    QCOMPARE(document.openDocument(source, QUrl::fromLocalFile(source), mime), Okular::Document::OpenSuccess);
    QVERIFY(document.canAddDocumentBookmarks());
    const int originalCount = document.documentSynopsis() ? document.documentSynopsis()->children().size() : 0;
    QSignalSpy synopsisChanges(&document, &Okular::Document::documentSynopsisChanged);
    QVERIFY(!document.addDocumentBookmark(-1, QStringLiteral("Invalid")));
    QVERIFY(!document.addDocumentBookmark(0, QStringLiteral("  ")));
    QVERIFY(document.isHistoryClean());

    const QString firstTitle = QString::fromUtf8("Caffè 日本語");
    QVERIFY(document.addDocumentBookmark(0, firstTitle));
    QVERIFY(!document.isHistoryClean());
    QCOMPARE(document.documentSynopsis()->children().size(), originalCount + 1);
    QVERIFY(document.addDocumentBookmark(0, QStringLiteral("Second")));
    QCOMPARE(document.documentSynopsis()->children().size(), originalCount + 2);
    document.undo();
    QCOMPARE(document.documentSynopsis()->children().size(), originalCount + 1);
    document.redo();
    QCOMPARE(document.documentSynopsis()->children().size(), originalCount + 2);
    QCOMPARE(synopsisChanges.count(), 4);

    QString error;
    const QString saved = directory.filePath(QStringLiteral("saved.pdf"));
    QVERIFY2(document.saveChanges(saved, &error), qPrintable(error));
    document.setHistoryClean(true);
    QVERIFY(document.swapBackingFile(saved, QUrl::fromLocalFile(saved)));
    QCOMPARE(document.documentSynopsis()->children().size(), originalCount + 2);

    // Undo must still remove the embedded entry after the save replaced the backing file.
    document.undo();
    const QString undone = directory.filePath(QStringLiteral("undone.pdf"));
    QVERIFY2(document.saveChanges(undone, &error), qPrintable(error));
    Okular::Document reopened(nullptr);
    QCOMPARE(reopened.openDocument(undone, QUrl::fromLocalFile(undone), mime), Okular::Document::OpenSuccess);
    QCOMPARE(reopened.documentSynopsis()->children().size(), originalCount + 1);
    QCOMPARE(reopened.documentSynopsis()->children().constLast().title(), firstTitle);
    reopened.closeDocument();

    QVERIFY(document.swapBackingFile(undone, QUrl::fromLocalFile(undone)));
    document.redo();
    const QString redone = directory.filePath(QStringLiteral("redone.pdf"));
    QVERIFY2(document.saveChanges(redone, &error), qPrintable(error));
    QVERIFY(document.swapBackingFile(redone, QUrl::fromLocalFile(redone)));
    const QString savedAgain = directory.filePath(QStringLiteral("saved-again.pdf"));
    QVERIFY2(document.saveChanges(savedAgain, &error), qPrintable(error));
    QCOMPARE(reopened.openDocument(savedAgain, QUrl::fromLocalFile(savedAgain), mime), Okular::Document::OpenSuccess);
    QCOMPARE(reopened.documentSynopsis()->children().size(), originalCount + 2);
    QCOMPARE(reopened.documentSynopsis()->children().at(originalCount).title(), firstTitle);
    QCOMPARE(reopened.documentSynopsis()->children().constLast().title(), QStringLiteral("Second"));
}

void DocumentTest::testDocumentBookmarkRenamePersistence()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), source));
    const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"));
    Okular::Document document(nullptr);
    QCOMPARE(document.openDocument(source, QUrl::fromLocalFile(source), mime), Okular::Document::OpenSuccess);
    const QString originalTitle = QString::fromUtf8("Capitolo originale: caffè 日本語");
    QVERIFY(document.addDocumentBookmark(0, originalTitle));
    QString error;
    const QString initial = directory.filePath(QStringLiteral("initial.pdf"));
    QVERIFY2(document.saveChanges(initial, &error), qPrintable(error));
    document.closeDocument();

    // Reopen so the renamed entry is an existing PDF bookmark, rather than a new one.
    QCOMPARE(document.openDocument(initial, QUrl::fromLocalFile(initial), mime), Okular::Document::OpenSuccess);
    const auto bookmarks = document.documentSynopsis()->children();
    const QString id = bookmarks.constLast().id();
    const QString renamedTitle = QString::fromUtf8("Capitolo rinominato: 日本語");
    QVERIFY(!document.renameDocumentBookmark(QStringLiteral("missing"), renamedTitle));
    QVERIFY(!document.renameDocumentBookmark(id, QStringLiteral("  ")));
    QVERIFY(!document.renameDocumentBookmark(id, originalTitle));
    QVERIFY(document.renameDocumentBookmark(id, renamedTitle));
    QCOMPARE(document.documentSynopsis()->children().constLast().title(), renamedTitle);
    document.undo();
    QCOMPARE(document.documentSynopsis()->children().constLast().title(), originalTitle);
    document.redo();
    QCOMPARE(document.documentSynopsis()->children().constLast().title(), renamedTitle);

    const QString renamed = directory.filePath(QStringLiteral("renamed.pdf"));
    QVERIFY2(document.saveChanges(renamed, &error), qPrintable(error));
    QVERIFY(document.swapBackingFile(renamed, QUrl::fromLocalFile(renamed)));
    auto verifySaved = [&](const QString &path, const QString &title) {
        Okular::Document reopened(nullptr);
        QCOMPARE(reopened.openDocument(path, QUrl::fromLocalFile(path), mime), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.documentSynopsis()->children().size(), bookmarks.size());
        QCOMPARE(reopened.documentSynopsis()->children().constLast().title(), title);
        QVERIFY(reopened.documentSynopsis()->children().constLast().viewPort());
        QCOMPARE(reopened.documentSynopsis()->children().constLast().viewPort()->pageNumber, 0);
    };
    verifySaved(renamed, renamedTitle);

    // Undo and redo must persist after saving replaced the backing file.
    document.undo();
    const QString undone = directory.filePath(QStringLiteral("undone.pdf"));
    QVERIFY2(document.saveChanges(undone, &error), qPrintable(error));
    verifySaved(undone, originalTitle);
    QVERIFY(document.swapBackingFile(undone, QUrl::fromLocalFile(undone)));
    document.redo();
    const QString redone = directory.filePath(QStringLiteral("redone.pdf"));
    QVERIFY2(document.saveChanges(redone, &error), qPrintable(error));
    verifySaved(redone, renamedTitle);
}

// Test that we don't crash if the document is closed while a RotationJob
// is enqueued/running
void DocumentTest::testCloseDuringRotationJob()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));
    Okular::Document *m_document = new Okular::Document(nullptr);
    const QString testFile = QStringLiteral(KDESRCDIR "data/file1.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);

    Okular::DocumentObserver *dummyDocumentObserver = new Okular::DocumentObserver();
    m_document->addObserver(dummyDocumentObserver);

    m_document->openDocument(testFile, QUrl(), mime);
    m_document->setRotation(1);

    // Tell ThreadWeaver not to start any new job
    ThreadWeaver::Queue::instance()->suspend();

    // Request a pixmap. A RotationJob will be enqueued but not started
    Okular::PixmapRequest *pixmapReq = new Okular::PixmapRequest(dummyDocumentObserver, 0, 100, 100, qApp->devicePixelRatio(), 1, Okular::PixmapRequest::NoFeature);
    m_document->requestPixmaps({pixmapReq});

    // Delete the document
    delete m_document;

    // Resume job processing and wait for the RotationJob to finish
    ThreadWeaver::Queue::instance()->resume();
    ThreadWeaver::Queue::instance()->finish();
    qApp->processEvents();

    delete dummyDocumentObserver;
}

// Test that, if there's a XML file in docdata referring to a document, we
// detect that it must be migrated, that it doesn't get wiped out if you close
// the document without migrating and that it does get wiped out after migrating
void DocumentTest::testDocdataMigration()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QUrl testFileUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QString testFilePath = testFileUrl.toLocalFile();
    const qint64 testFileSize = QFileInfo(testFilePath).size();

    // Copy XML file to the docdata/ directory
    const QString docDataPath = Okular::DocumentPrivate::docDataFileName(testFileUrl, testFileSize);
    QFile::remove(docDataPath);
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1-docdata.xml"), docDataPath));

    // Open our document
    Okular::Document *m_document = new Okular::Document(nullptr);
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFilePath);
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);

    // Check that the annotation from file1-docdata.xml was loaded
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QCOMPARE(m_document->page(0)->annotations().constFirst()->uniqueName(), QStringLiteral("testannot"));

    // Check that we detect that it must be migrated
    QVERIFY(m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // Reopen the document and check that the annotation is still present
    // (because we have not migrated)
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QCOMPARE(m_document->page(0)->annotations().constFirst()->uniqueName(), QStringLiteral("testannot"));
    QVERIFY(m_document->isDocdataMigrationNeeded());

    // Do the migration
    QTemporaryFile migratedSaveFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
    QVERIFY(migratedSaveFile.open());
    migratedSaveFile.close();
    QString errorString;
    QVERIFY(m_document->saveChanges(migratedSaveFile.fileName(), &errorString));
    QVERIFY(errorString.isEmpty());
    m_document->docdataMigrationDone();
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // Now the docdata file should have no annotations, let's check
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 0);
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // And the new file should have 1 annotation, let's check
    QCOMPARE(m_document->openDocument(migratedSaveFile.fileName(), QUrl::fromLocalFile(migratedSaveFile.fileName()), mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    delete m_document;
}

void DocumentTest::testEvaluateKeystrokeEventChange_data()
{
    QTest::addColumn<QString>("oldVal");
    QTest::addColumn<QString>("newVal");
    QTest::addColumn<int>("selStart");
    QTest::addColumn<int>("selEnd");
    QTest::addColumn<QString>("expectedDiff");

    QTest::addRow("empty") << ""
                           << "" << 0 << 0 << "";
    QTest::addRow("a") << ""
                       << "a" << 0 << 0 << "a";
    QTest::addRow("ab") << "a"
                        << "b" << 0 << 1 << "b";
    QTest::addRow("ab2") << "a"
                         << "ab" << 1 << 1 << "b";
    QTest::addRow("kaesekuchen") << "Käse"
                                 << "Käsekuchen" << 4 << 4 << "kuchen";
    QTest::addRow("replace") << "kuchen"
                             << "wurst" << 0 << 6 << "wurst";
    QTest::addRow("okular") << "Oku"
                            << "Okular" << 3 << 3 << "lar";
    QTest::addRow("okular2") << "Oku"
                             << "Okular" << 0 << 3 << "Okular";
    QTest::addRow("removal1") << "a"
                              << "" << 0 << 1 << "";
    QTest::addRow("removal2") << "ab"
                              << "a" << 1 << 2 << "";
    QTest::addRow("overlapping chang") << "abcd"
                                       << "abclmnopd" << 1 << 3 << "bclmnop";
    QTest::addRow("unicode") << "☮🤌"
                             << "☮🤌❤️" << 2 << 2 << "❤️";
    QTest::addRow("unicode2") << "☮"
                              << "☮🤌❤️" << 1 << 1 << "🤌❤️";
    QTest::addRow("unicode3") << "🤍"
                              << "🤌" << 0 << 1 << "🤌";
}

void DocumentTest::testEvaluateKeystrokeEventChange()
{
    QFETCH(QString, oldVal);
    QFETCH(QString, newVal);
    QFETCH(int, selStart);
    QFETCH(int, selEnd);
    QFETCH(QString, expectedDiff);

    QCOMPARE(Okular::DocumentPrivate::evaluateKeystrokeEventChange(oldVal, newVal, selStart, selEnd), expectedDiff);
}

QTEST_MAIN(DocumentTest)
#include "documenttest.moc"
