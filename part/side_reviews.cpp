/*
    SPDX-FileCopyrightText: 2005 Enrico Ros <eros.kde@email.it>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "side_reviews.h"

// qt/kde includes
#include <QAction>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QStringList>
#include <QTextDocument>
#include <QToolBar>
#include <QTreeView>

#include <KLocalizedString>
#include <KTitleWidget>
#include <KViewStateSerializer>

// local includes
#include "annotationmodel.h"
#include "annotationpopup.h"
#include "annotationproxymodels.h"
#include "core/annotations.h"
#include "core/document.h"
#include "core/page.h"
#include "ktreeviewsearchline.h"
#include "settings.h"

class TreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit TreeView(Okular::Document *document, QWidget *parent = Q_NULLPTR)
        : QTreeView(parent)
        , m_document(document)
    {
    }

protected:
    QModelIndex dropTargetAt(const QPoint &position) const
    {
        QModelIndex target = indexAt(position);
        while (target.isValid() && !(model()->flags(target) & Qt::ItemIsDropEnabled)) {
            target = target.parent();
        }
        return target;
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        bool supportedMimeType = false;
        if (model() && event->mimeData()) {
            const QStringList supportedTypes = model()->mimeTypes();
            for (const QString &format : event->mimeData()->formats()) {
                if (supportedTypes.contains(format)) {
                    supportedMimeType = true;
                    break;
                }
            }
        }
        if (event->proposedAction() == Qt::MoveAction && supportedMimeType) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        QTreeView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        const QModelIndex target = dropTargetAt(event->position().toPoint());
        if (target.isValid() && model()->canDropMimeData(event->mimeData(), Qt::MoveAction, -1, 0, target)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QModelIndex target = dropTargetAt(event->position().toPoint());
        if (target.isValid() && model()->dropMimeData(event->mimeData(), Qt::MoveAction, -1, 0, target)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        event->ignore();
    }

    void paintEvent(QPaintEvent *event) override
    {
        bool hasAnnotations = false;
        for (uint i = 0; i < m_document->pages(); ++i) {
            if (m_document->page(i)->hasAnnotations()) {
                hasAnnotations = true;
                break;
            }
        }
        if (!hasAnnotations) {
            QPainter p(viewport());
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setClipRect(event->rect());

            QTextDocument document;
            document.setHtml(
                i18n("<div align=center><h3>No annotations</h3>"
                     "To create new annotations press F6 or select <i>Tools -&gt; Annotations</i>"
                     " from the menu.</div>"));
            document.setTextWidth(width() - 50);

            const uint w = document.size().width() + 20;
            const uint h = document.size().height() + 20;

            p.setBrush(palette().window());
            p.translate(0.5, 0.5);
            p.drawRoundedRect(15, 15, w, h, 3, 3);
            p.translate(20, 20);
            document.drawContents(&p);

        } else {
            QTreeView::paintEvent(event);
        }
    }

private:
    Okular::Document *m_document;
};

Reviews::Reviews(QWidget *parent, Okular::Document *document)
    : QWidget(parent)
    , m_document(document)
{
    // create widgets and layout them vertically
    QVBoxLayout *vLayout = new QVBoxLayout(this);
    vLayout->setSpacing(6);

    KTitleWidget *titleWidget = new KTitleWidget(this);
    titleWidget->setLevel(4);
    titleWidget->setText(i18n("Annotations"));

    m_view = new TreeView(m_document, this);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->header()->hide();

    QToolBar *toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("reviewOptsBar"));
    QSizePolicy sp = toolBar->sizePolicy();
    sp.setVerticalPolicy(QSizePolicy::Minimum);
    toolBar->setSizePolicy(sp);

    m_model = new AnnotationModel(m_document, m_view);

    m_filterProxy = new PageFilterProxyModel(m_view);
    m_annotationListProxy = new AnnotationListProxyModel(m_view);
    m_authorProxy = new AuthorGroupProxyModel(m_view);

    m_filterProxy->setSourceModel(m_model);
    m_annotationListProxy->setSourceModel(m_filterProxy);
    m_authorProxy->setSourceModel(m_annotationListProxy);

    m_view->setModel(m_authorProxy);

    connect(m_authorProxy, &QAbstractItemModel::modelAboutToBeReset, this, &Reviews::saveTreeState);
    // Must be after setModel() so that we restore after QTreeView::reset cleared expanded items, not the other way around
    connect(m_authorProxy, &QAbstractItemModel::modelReset, this, &Reviews::restoreTreeState);

    m_searchLine = new KTreeViewSearchLine(this, m_view);
    m_searchLine->setPlaceholderText(i18n("Search…"));
    m_searchLine->setCaseSensitivity(Okular::Settings::self()->reviewsSearchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    m_searchLine->setRegularExpression(Okular::Settings::self()->reviewsSearchRegularExpression());
    connect(m_searchLine, &KTreeViewSearchLine::searchOptionsChanged, this, &Reviews::saveSearchOptions);
    vLayout->addWidget(titleWidget);
    vLayout->setAlignment(titleWidget, Qt::AlignHCenter);
    vLayout->addWidget(m_searchLine);
    vLayout->addWidget(m_view);
    vLayout->addWidget(toolBar);

    toolBar->setIconSize(QSize(16, 16));
    toolBar->setMovable(false);
    // - add Current Page Only button
    QAction *curPageOnlyAction = toolBar->addAction(QIcon::fromTheme(QStringLiteral("arrow-down")), i18n("Show annotations for current page only"));
    curPageOnlyAction->setCheckable(true);
    connect(curPageOnlyAction, &QAction::toggled, this, &Reviews::slotCurrentPageOnly);
    curPageOnlyAction->setChecked(Okular::Settings::currentPageOnly());

    // Adds space between left actions, so that the next two buttons are aligned to the right
    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolBar->addWidget(spacer);

    QAction *expandAll = toolBar->addAction(QIcon::fromTheme(QStringLiteral("expand-all")), i18n("Expand all elements"));
    connect(expandAll, &QAction::triggered, this, &Reviews::slotExpandAll);
    QAction *collapseAll = toolBar->addAction(QIcon::fromTheme(QStringLiteral("collapse-all")), i18n("Collapse all elements"));
    connect(collapseAll, &QAction::triggered, this, &Reviews::slotCollapseAll);

    connect(m_view, &TreeView::activated, this, &Reviews::activated);

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &TreeView::customContextMenuRequested, this, &Reviews::contextMenuRequested);
}

Reviews::~Reviews()
{
    m_document->removeObserver(this);
}

// BEGIN DocumentObserver Notifies
void Reviews::notifyCurrentPageChanged(int previousPage, int currentPage)
{
    Q_UNUSED(previousPage)

    m_filterProxy->setCurrentPage(currentPage);
}
// END DocumentObserver Notifies

void Reviews::reparseConfig()
{
    m_searchLine->setCaseSensitivity(Okular::Settings::reviewsSearchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    m_searchLine->setRegularExpression(Okular::Settings::reviewsSearchRegularExpression());
    m_view->update();
}

// BEGIN GUI Slots -> requestListViewUpdate
void Reviews::slotCurrentPageOnly(bool on)
{
    // store toggle state in Settings and update the listview
    Okular::Settings::setCurrentPageOnly(on);
    m_filterProxy->groupByCurrentPage(on);

    m_view->expandAll();
}

void Reviews::slotExpandAll()
{
    m_view->expandAll();
}

void Reviews::slotCollapseAll()
{
    m_view->collapseAll();
}
// END GUI Slots

void Reviews::activated(const QModelIndex &index)
{
    const QModelIndex authorIndex = m_authorProxy->mapToSource(index);
    const QModelIndex filterIndex = m_annotationListProxy->mapToSource(authorIndex);
    const QModelIndex annotIndex = m_filterProxy->mapToSource(filterIndex);

    Okular::Annotation *annotation = m_model->annotationForIndex(annotIndex);
    if (!annotation) {
        return;
    }

    int pageNumber = m_model->data(annotIndex, AnnotationModel::PageRole).toInt();
    const Okular::Page *page = m_document->page(pageNumber);

    // calculating the right coordinates to center the view on the annotation
    QRect rect = Okular::AnnotationUtils::annotationGeometry(annotation, page->width(), page->height());
    Okular::NormalizedRect nr(rect, (int)page->width(), (int)page->height());
    // set the viewport parameters
    Okular::DocumentViewport vp;
    vp.pageNumber = pageNumber;
    vp.rePos.enabled = true;
    vp.rePos.pos = Okular::DocumentViewport::Center;
    vp.rePos.normalizedX = (nr.right + nr.left) / 2.0;
    vp.rePos.normalizedY = (nr.bottom + nr.top) / 2.0;
    // setting the viewport
    m_document->setViewport(vp, nullptr, true);
}

QModelIndexList Reviews::retrieveAnnotations(const QModelIndex &idx) const
{
    QModelIndexList ret;
    if (idx.isValid()) {
        const QAbstractItemModel *model = idx.model();
        if (model->hasChildren(idx)) {
            int rowCount = model->rowCount(idx);
            for (int i = 0; i < rowCount; i++) {
                ret += retrieveAnnotations(model->index(i, idx.column(), idx));
            }
        } else {
            ret += idx;
        }
    }

    return ret;
}

void Reviews::contextMenuRequested(const QPoint pos)
{
    const QModelIndex clickedIndex = m_view->indexAt(pos);
    if (clickedIndex.isValid() && !m_view->selectionModel()->isSelected(clickedIndex)) {
        m_view->selectionModel()->select(clickedIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->setCurrentIndex(clickedIndex);
    }

    const bool authorGroup = m_authorProxy->isAuthorGroup(clickedIndex);
    const QString author = authorGroup ? clickedIndex.data().toString() : QString();
    const bool authorExpanded = authorGroup && m_view->isExpanded(clickedIndex);
    if (authorGroup) {
        m_view->setCurrentIndex(clickedIndex);
    }

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode, this);
    connect(&popup, &AnnotationPopup::openAnnotationWindow, this, &Reviews::openAnnotationWindow);

    const QModelIndexList indexes = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex &index : indexes) {
        const QModelIndexList annotations = retrieveAnnotations(index);
        for (const QModelIndex &idx : annotations) {
            const QModelIndex authorIndex = m_authorProxy->mapToSource(idx);
            const QModelIndex filterIndex = m_annotationListProxy->mapToSource(authorIndex);
            const QModelIndex annotIndex = m_filterProxy->mapToSource(filterIndex);
            Okular::Annotation *annotation = m_model->annotationForIndex(annotIndex);
            if (annotation) {
                const int pageNumber = m_model->data(annotIndex, AnnotationModel::PageRole).toInt();
                popup.addAnnotation(annotation, pageNumber);
            }
        }
    }

    if (!clickedIndex.isValid() && indexes.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction *renameAuthor = nullptr;
    QAction *editFolder = nullptr;
    QModelIndex clickedAnnotationIndex;
    Okular::Annotation *clickedAnnotation = nullptr;

    if (authorGroup) {
        renameAuthor = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu", "Rename"));
        renameAuthor->setObjectName(QStringLiteral("rename_annotation_author"));
        renameAuthor->setEnabled(m_document->canRenameAnnotationAuthor(author));
        menu.addSeparator();
    } else {
        const QModelIndex sourceIndex = m_filterProxy->mapToSource(m_annotationListProxy->mapToSource(m_authorProxy->mapToSource(clickedIndex)));
        clickedAnnotation = m_model->annotationForIndex(sourceIndex);
        if (clickedAnnotation) {
            clickedAnnotationIndex = sourceIndex;
            editFolder = menu.addAction(QIcon::fromTheme(QStringLiteral("folder")), i18nc("@action:inmenu", "Change Folder"));
            editFolder->setObjectName(QStringLiteral("edit_annotation_folder"));
            editFolder->setEnabled(m_model->canChangeAnnotationAuthor(clickedAnnotationIndex));
            menu.addSeparator();
        } else if (clickedIndex.isValid()) {
            popup.exec(m_view->viewport()->mapToGlobal(pos));
            return;
        }
    }

    popup.addActionsToMenu(&menu);
    QAction *selectedAction = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (selectedAction == renameAuthor) {
        bool accepted = false;
        const QString newAuthor = QInputDialog::getText(this, i18nc("@title:window", "Rename"), i18nc("@label:textbox", "Name:"), QLineEdit::Normal, author, &accepted).trimmed();
        if (accepted && !newAuthor.isEmpty() && m_document->renameAnnotationAuthor(author, newAuthor)) {
            for (int row = 0; row < m_authorProxy->rowCount(QModelIndex()); ++row) {
                const QModelIndex renamedIndex = m_authorProxy->index(row, 0);
                if (renamedIndex.data().toString() == newAuthor) {
                    m_view->setCurrentIndex(renamedIndex);
                    m_view->setExpanded(renamedIndex, authorExpanded || m_view->isExpanded(renamedIndex));
                    m_view->scrollTo(renamedIndex);
                    break;
                }
            }
        }
    } else if (selectedAction == editFolder) {
        bool accepted = false;
        const QString folder = QInputDialog::getText(this,
                                                     i18nc("@title:window", "Change Folder"),
                                                     i18nc("@label:textbox", "Folder:"),
                                                     QLineEdit::Normal,
                                                     clickedAnnotation->author(),
                                                     &accepted)
                                 .trimmed();
        if (accepted) {
            m_model->changeAnnotationAuthor(clickedAnnotationIndex, folder);
        }
    }
}

void Reviews::saveSearchOptions()
{
    Okular::Settings::setReviewsSearchRegularExpression(m_searchLine->regularExpression());
    Okular::Settings::setReviewsSearchCaseSensitive(m_searchLine->caseSensitivity() == Qt::CaseSensitive ? true : false);
    Okular::Settings::self()->save();
}

class AnnotationStateSaver : public KViewStateSerializer
{
    Q_OBJECT
public:
    explicit AnnotationStateSaver(QTreeView *view)
        : KViewStateSerializer()
    {
        setView(view);
    }
    QString indexToConfigString(const QModelIndex &index) const override
    {
        // Store the display text, with parent if any, e.g. "Section / Annotation"
        QString key = index.data().toString();
        if (auto parent = index.parent(); parent.isValid()) {
            key.prepend(parent.data().toString() + QLatin1String(" / "));
        }
        return key;
    }
    QModelIndex indexFromConfigString(const QAbstractItemModel *model, const QString &key) const override
    {
        const auto matchFirst = [model](const QModelIndex &startIndex, const QString &text) {
            const QModelIndexList list = model->match(startIndex, Qt::DisplayRole, text, 1, Qt::MatchRecursive);
            if (list.isEmpty()) {
                return QModelIndex();
            }
            return list.first();
        };

        const QStringList subKeys = key.split(QStringLiteral(" / "));
        QModelIndex index = model->index(0, 0);
        for (const QString &subKey : subKeys) {
            index = matchFirst(index, subKey);
            if (!index.isValid()) {
                break;
            }
        }
        return index;
    }
};

void Reviews::saveTreeState()
{
    AnnotationStateSaver saver(m_view);
    m_expansionKeys = saver.expansionKeys();
}

void Reviews::restoreTreeState()
{
    AnnotationStateSaver saver(m_view);
    saver.restoreExpanded(m_expansionKeys);
}

QAbstractItemModel *Reviews::annotationsModel() const
{
    return m_model;
}

#include "moc_side_reviews.cpp"
#include "side_reviews.moc"
