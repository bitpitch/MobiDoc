/*
    SPDX-FileCopyrightText: 2026 Mobicod

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QAction>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMimeDatabase>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <memory>

#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/page.h"
#include "../part/side_reviews.h"
#include "../settings.h"

class AnnotationAuthorDragTest : public QObject
{
    Q_OBJECT

    static Okular::TextAnnotation *addNote(Okular::Document &document, int page, const QString &author)
    {
        auto *annotation = new Okular::TextAnnotation;
        annotation->setAuthor(author);
        annotation->setContents(author);
        annotation->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.2, 0.2));
        document.addPageAnnotation(page, annotation);
        return annotation;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Okular::Settings::instance(QStringLiteral("annotationauthordragtest"));
        Okular::Settings::setGroupByPage(true);
        Okular::Settings::setGroupByAuthor(true);
        Okular::Settings::setCurrentPageOnly(false);
    }

    void movePage_data()
    {
        QTest::addColumn<bool>("dropOnPage");
        QTest::newRow("author") << false;
        QTest::newRow("page-owned-by-author") << true;
    }

    void movePage()
    {
        QFETCH(bool, dropOnPage);
        Okular::Document document(nullptr);
        const QString file = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
        QCOMPARE(document.openDocument(file, QUrl(), QMimeDatabase().mimeTypeForFile(file)), Okular::Document::OpenSuccess);

        auto *first = addNote(document, 0, QStringLiteral("Alice"));
        auto *second = addNote(document, 0, QStringLiteral("Alice"));
        auto *otherPage = addNote(document, 1, QStringLiteral("Alice"));
        auto *destination = addNote(document, 2, QStringLiteral("Bob"));

        Reviews reviews(nullptr, &document);
        reviews.resize(400, 600);
        reviews.show();
        auto *view = reviews.findChild<QTreeView *>();
        QVERIFY(view);
        view->expandAll();
        QCoreApplication::processEvents();

        auto *model = view->model();
        QCOMPARE(model->supportedDragActions(), Qt::DropActions(Qt::MoveAction));
        QCOMPARE(model->rowCount(), 2);
        const QModelIndex alice = model->index(0, 0);
        const QModelIndex bob = model->index(1, 0);
        QCOMPARE(alice.data().toString(), QStringLiteral("Alice"));
        QCOMPARE(bob.data().toString(), QStringLiteral("Bob"));
        const QModelIndex sourcePage = model->index(0, 0, alice);
        const QModelIndex target = dropOnPage ? model->index(0, 0, bob) : bob;
        QVERIFY(model->flags(sourcePage) & Qt::ItemIsDragEnabled);
        std::unique_ptr<QMimeData> mime(model->mimeData({sourcePage}));
        QVERIFY(!mime->formats().isEmpty());
        QVERIFY(model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, target));

        const QPoint position = view->visualRect(target).center();
        QDragEnterEvent enter(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(first->author(), QStringLiteral("Bob"));
        QCOMPARE(second->author(), QStringLiteral("Bob"));
        QCOMPARE(otherPage->author(), QStringLiteral("Alice"));
        QCOMPARE(destination->author(), QStringLiteral("Bob"));
        document.undo();
        QCOMPARE(first->author(), QStringLiteral("Alice"));
        QCOMPARE(second->author(), QStringLiteral("Alice"));
    }

    void renameAuthor_data()
    {
        QTest::addColumn<QString>("newAuthor");
        QTest::newRow("new-group") << QStringLiteral("Chapter 1");
        QTest::newRow("existing-group") << QStringLiteral("Bob");
    }

    void renameAuthor()
    {
        QFETCH(QString, newAuthor);
        Okular::Document document(nullptr);
        const QString file = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
        QCOMPARE(document.openDocument(file, QUrl(), QMimeDatabase().mimeTypeForFile(file)), Okular::Document::OpenSuccess);
        auto *first = addNote(document, 0, QStringLiteral("Alice"));
        auto *second = addNote(document, 1, QStringLiteral("Alice"));
        auto *other = addNote(document, 2, QStringLiteral("Bob"));

        QVERIFY(document.canRenameAnnotationAuthor(QStringLiteral("Alice")));
        QVERIFY(document.renameAnnotationAuthor(QStringLiteral("Alice"), newAuthor));
        QCOMPARE(first->author(), newAuthor);
        QCOMPARE(second->author(), newAuthor);
        QCOMPARE(other->author(), QStringLiteral("Bob"));
        document.undo();
        QCOMPARE(first->author(), QStringLiteral("Alice"));
        QCOMPARE(second->author(), QStringLiteral("Alice"));
        document.redo();
        QCOMPARE(first->author(), newAuthor);
        QCOMPARE(second->author(), newAuthor);
        document.undo();

        second->setFlags(second->flags() | Okular::Annotation::DenyWrite);
        QVERIFY(!document.canRenameAnnotationAuthor(QStringLiteral("Alice")));
        QVERIFY(!document.renameAnnotationAuthor(QStringLiteral("Alice"), newAuthor));
        QCOMPARE(first->author(), QStringLiteral("Alice"));
        QCOMPARE(second->author(), QStringLiteral("Alice"));
    }

    void renameAuthorFromMenu()
    {
        Okular::Document document(nullptr);
        const QString file = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
        QCOMPARE(document.openDocument(file, QUrl(), QMimeDatabase().mimeTypeForFile(file)), Okular::Document::OpenSuccess);
        auto *first = addNote(document, 0, QStringLiteral("Alice"));
        auto *hiddenPage = addNote(document, 1, QStringLiteral("Alice"));
        Reviews reviews(nullptr, &document);
        reviews.resize(400, 600);
        reviews.show();
        reviews.slotCurrentPageOnly(true);
        reviews.notifyCurrentPageChanged(-1, 0);
        auto *view = reviews.findChild<QTreeView *>();
        QVERIFY(view);
        QCoreApplication::processEvents();
        const QModelIndex author = view->model()->index(0, 0);
        QCOMPARE(author.data().toString(), QStringLiteral("Alice"));
        const QPoint position = view->visualRect(author).center();

        bool actionFound = false;
        bool dialogFound = false;
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) {
                return;
            }
            QAction *rename = menu->findChild<QAction *>(QStringLiteral("rename_annotation_author"));
            if (!rename || !rename->isEnabled()) {
                menu->close();
                return;
            }
            actionFound = true;
            menu->setActiveAction(rename);
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                if (dialog) {
                    dialogFound = true;
                    dialog->setTextValue(QStringLiteral("Chapter 1"));
                    dialog->accept();
                }
            });
            QTest::keyClick(menu, Qt::Key_Return);
        });
        QVERIFY(QMetaObject::invokeMethod(&reviews, "contextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, position)));
        QVERIFY(actionFound);
        QVERIFY(dialogFound);
        QCOMPARE(first->author(), QStringLiteral("Chapter 1"));
        QCOMPARE(hiddenPage->author(), QStringLiteral("Chapter 1"));
        document.undo();
        QCOMPARE(first->author(), QStringLiteral("Alice"));
        QCOMPARE(hiddenPage->author(), QStringLiteral("Alice"));
        reviews.slotCurrentPageOnly(false);
    }

    void authorChangesSurviveSave_data()
    {
        QTest::addColumn<bool>("renameGroup");
        QTest::newRow("move-page-to-author") << false;
        QTest::newRow("rename-author-group") << true;
    }

    void authorChangesSurviveSave()
    {
        QFETCH(bool, renameGroup);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Okular::Document document(nullptr);
        const QString source = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
        const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"));
        QCOMPARE(document.openDocument(source, QUrl::fromLocalFile(source), mime), Okular::Document::OpenSuccess);

        const auto addInk = [&](int page, const QString &author) {
            auto *ink = new Okular::InkAnnotation;
            ink->setAuthor(author);
            ink->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.4, 0.4));
            ink->setInkPaths({{Okular::NormalizedPoint(0.1, 0.1), Okular::NormalizedPoint(0.2, 0.3), Okular::NormalizedPoint(0.4, 0.4)}});
            document.addPageAnnotation(page, ink);
            return ink->uniqueName();
        };
        const QString first = addInk(0, QStringLiteral("Alice"));
        const QString second = addInk(0, QStringLiteral("Alice"));
        const QString otherPage = addInk(2, QStringLiteral("Alice"));
        addInk(1, QStringLiteral("Bob"));

        if (renameGroup) {
            QVERIFY(document.renameAnnotationAuthor(QStringLiteral("Alice"), QStringLiteral("Bob")));
        } else {
            QVERIFY(document.changePageAnnotationsAuthor(0, {first, second}, QStringLiteral("Bob")));
        }

        QString error;
        const QString saved = directory.filePath(QStringLiteral("saved.pdf"));
        QVERIFY2(document.saveChanges(saved, &error), qPrintable(error));
        QVERIFY(document.swapBackingFile(saved, QUrl::fromLocalFile(saved)));
        QCOMPARE(document.page(0)->annotation(first)->author(), QStringLiteral("Bob"));
        QCOMPARE(document.page(0)->annotation(second)->author(), QStringLiteral("Bob"));
        QCOMPARE(document.page(2)->annotation(otherPage)->author(), renameGroup ? QStringLiteral("Bob") : QStringLiteral("Alice"));

        // Saving replaces the generated annotation objects. The macro's child
        // commands must now refer to the new objects for undo/redo to remain safe.
        document.undo();
        QCOMPARE(document.page(0)->annotation(first)->author(), QStringLiteral("Alice"));
        QCOMPARE(document.page(0)->annotation(second)->author(), QStringLiteral("Alice"));
        QCOMPARE(document.page(2)->annotation(otherPage)->author(), QStringLiteral("Alice"));
        const QString undone = directory.filePath(QStringLiteral("undone.pdf"));
        QVERIFY2(document.saveChanges(undone, &error), qPrintable(error));
        QVERIFY(document.swapBackingFile(undone, QUrl::fromLocalFile(undone)));

        document.redo();
        QCOMPARE(document.page(0)->annotation(first)->author(), QStringLiteral("Bob"));
        const QString redone = directory.filePath(QStringLiteral("redone.pdf"));
        QVERIFY2(document.saveChanges(redone, &error), qPrintable(error));
        QVERIFY(document.swapBackingFile(redone, QUrl::fromLocalFile(redone)));
        document.undo();
        QCOMPARE(document.page(0)->annotation(first)->author(), QStringLiteral("Alice"));

        Okular::Document reopened(nullptr);
        QCOMPARE(reopened.openDocument(redone, QUrl::fromLocalFile(redone), mime), Okular::Document::OpenSuccess);
        auto *ink = dynamic_cast<Okular::InkAnnotation *>(reopened.page(0)->annotation(first));
        QVERIFY(ink);
        QCOMPARE(ink->author(), QStringLiteral("Bob"));
        QCOMPARE(ink->inkPaths().size(), 1);
        QCOMPARE(ink->inkPaths().first().size(), 3);
    }
};

QTEST_MAIN(AnnotationAuthorDragTest)
#include "annotationauthordragtest.moc"
