/*
    SPDX-FileCopyrightText: 2007 Tobias Koenig <tokoe@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "annotationproxymodels.h"

#include <QDataStream>
#include <QIODevice>
#include <QItemSelection>
#include <QList>
#include <QMimeData>

#include <QIcon>

#include "annotationmodel.h"
#include "core/annotations.h"
#include "gui/debug_ui.h"

PageFilterProxyModel::PageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , mGroupByCurrentPage(false)
    , mCurrentPage(-1)
{
    setDynamicSortFilter(true);
}

void PageFilterProxyModel::groupByCurrentPage(bool value)
{
    if (mGroupByCurrentPage == value) {
        return;
    }

    mGroupByCurrentPage = value;

    invalidateFilter();
}

void PageFilterProxyModel::setCurrentPage(int page)
{
    if (mCurrentPage == page) {
        return;
    }

    mCurrentPage = page;

    // no need to invalidate when we're not showing the current page only
    if (!mGroupByCurrentPage) {
        return;
    }

    invalidateFilter();
}

bool PageFilterProxyModel::filterAcceptsRow(int row, const QModelIndex &sourceParent) const
{
    if (!mGroupByCurrentPage) {
        return true;
    }

    const QModelIndex pageIndex = sourceModel()->index(row, 0, sourceParent);
    int page = sourceModel()->data(pageIndex, AnnotationModel::PageRole).toInt();

    return (page == mCurrentPage);
}

AnnotationListProxyModel::AnnotationListProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

int AnnotationListProxyModel::columnCount(const QModelIndex &) const
{
    return 1;
}

int AnnotationListProxyModel::rowCount(const QModelIndex &parentIndex) const
{
    return parentIndex.isValid() ? 0 : mIndexes.count();
}

QModelIndex AnnotationListProxyModel::index(int row, int column, const QModelIndex &parentIndex) const
{
    if (parentIndex.isValid() || row < 0 || row >= mIndexes.count() || column != 0) {
        return QModelIndex();
    }
    return createIndex(row, column);
}

QModelIndex AnnotationListProxyModel::parent(const QModelIndex &) const
{
    return QModelIndex();
}

QModelIndex AnnotationListProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    const int row = mIndexes.indexOf(sourceIndex);
    if (row < 0) {
        return QModelIndex();
    }
    return index(row, sourceIndex.column());
}

QModelIndex AnnotationListProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || proxyIndex.row() < 0 || proxyIndex.row() >= mIndexes.count()) {
        return QModelIndex();
    }
    return mIndexes[proxyIndex.row()];
}

void AnnotationListProxyModel::setSourceModel(QAbstractItemModel *model)
{
    if (sourceModel()) {
        disconnect(sourceModel(), &QAbstractItemModel::layoutAboutToBeChanged, this, &AnnotationListProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::layoutChanged, this, &AnnotationListProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::modelAboutToBeReset, this, &AnnotationListProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::modelReset, this, &AnnotationListProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsAboutToBeInserted, this, &AnnotationListProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsInserted, this, &AnnotationListProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsAboutToBeRemoved, this, &AnnotationListProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsRemoved, this, &AnnotationListProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::dataChanged, this, &AnnotationListProxyModel::sourceDataChanged);
    }

    QAbstractProxyModel::setSourceModel(model);

    connect(sourceModel(), &QAbstractItemModel::layoutAboutToBeChanged, this, &AnnotationListProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::layoutChanged, this, &AnnotationListProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::modelAboutToBeReset, this, &AnnotationListProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::modelReset, this, &AnnotationListProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsAboutToBeInserted, this, &AnnotationListProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsInserted, this, &AnnotationListProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsAboutToBeRemoved, this, &AnnotationListProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsRemoved, this, &AnnotationListProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::dataChanged, this, &AnnotationListProxyModel::sourceDataChanged);

    rebuildIndexes();
}

void AnnotationListProxyModel::doRebuildIndexes()
{
    mIndexes.clear();
    for (int row = 0; row < sourceModel()->rowCount(); ++row) {
        const QModelIndex pageIndex = sourceModel()->index(row, 0);
        for (int subRow = 0; subRow < sourceModel()->rowCount(pageIndex); ++subRow) {
            mIndexes.append(sourceModel()->index(subRow, 0, pageIndex));
        }
    }
}

void AnnotationListProxyModel::aboutToRebuild()
{
    beginResetModel();
}

void AnnotationListProxyModel::rebuild()
{
    doRebuildIndexes();
    endResetModel();
}

void AnnotationListProxyModel::rebuildIndexes()
{
    aboutToRebuild();
    rebuild();
}

void AnnotationListProxyModel::sourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    const QModelIndex proxyTopLeft = mapFromSource(topLeft);
    const QModelIndex proxyBottomRight = mapFromSource(bottomRight);
    if (proxyTopLeft.isValid() && proxyBottomRight.isValid()) {
        Q_EMIT dataChanged(proxyTopLeft, proxyBottomRight, roles);
    }
}

class AuthorGroupItem
{
public:
    enum Type { Root, Author, Annotation };

    explicit AuthorGroupItem(AuthorGroupItem *parent, Type type = Root, const QModelIndex &index = QModelIndex())
        : mParent(parent)
        , mType(type)
        , mIndex(index)
    {
    }

    ~AuthorGroupItem()
    {
        qDeleteAll(mChilds);
    }

    AuthorGroupItem(const AuthorGroupItem &) = delete;
    AuthorGroupItem &operator=(const AuthorGroupItem &) = delete;

    void appendChild(AuthorGroupItem *child)
    {
        mChilds.append(child);
    }
    AuthorGroupItem *parent() const
    {
        return mParent;
    }
    AuthorGroupItem *child(int row) const
    {
        return mChilds.value(row);
    }
    int childCount() const
    {
        return mChilds.count();
    }

    void dump(int level = 0)
    {
        QString prefix;
        for (int i = 0; i < level; ++i) {
            prefix += QLatin1Char(' ');
        }

        qCDebug(OkularUiDebug, "%s%s", qPrintable(prefix), (mType == Root ? "Root" : (mType == Author ? "Author" : "Annotation")));

        for (int i = 0; i < mChilds.count(); ++i) {
            mChilds[i]->dump(level + 2);
        }
    }

    const AuthorGroupItem *findIndex(const QModelIndex &index) const
    {
        if (index == mIndex) {
            return this;
        }

        for (int i = 0; i < mChilds.count(); ++i) {
            const AuthorGroupItem *item = mChilds[i]->findIndex(index);
            if (item) {
                return item;
            }
        }

        return nullptr;
    }

    int row() const
    {
        return (mParent ? mParent->mChilds.indexOf(const_cast<AuthorGroupItem *>(this)) : 0);
    }

    Type type() const
    {
        return mType;
    }
    QModelIndex index() const
    {
        return mIndex;
    }

    void setAuthor(const QString &author)
    {
        mAuthor = author;
    }
    QString author() const
    {
        return mAuthor;
    }

private:
    AuthorGroupItem *mParent;
    Type mType;
    QModelIndex mIndex;
    QList<AuthorGroupItem *> mChilds;
    QString mAuthor;
};

class AuthorGroupProxyModel::Private
{
public:
    explicit Private(AuthorGroupProxyModel *parent)
        : mParent(parent)
        , mRoot(nullptr)
    {
    }
    ~Private()
    {
        delete mRoot;
    }

    AuthorGroupProxyModel *mParent;
    AuthorGroupItem *mRoot;
};

AuthorGroupProxyModel::AuthorGroupProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
    , d(new Private(this))
{
}

AuthorGroupProxyModel::~AuthorGroupProxyModel()
{
    delete d;
}

int AuthorGroupProxyModel::columnCount(const QModelIndex &) const
{
    return 1;
}

int AuthorGroupProxyModel::rowCount(const QModelIndex &parentIndex) const
{
    AuthorGroupItem *item = nullptr;
    if (!parentIndex.isValid()) {
        item = d->mRoot;
    } else {
        item = static_cast<AuthorGroupItem *>(parentIndex.internalPointer());
    }

    return item ? item->childCount() : 0;
}

QModelIndex AuthorGroupProxyModel::index(int row, int column, const QModelIndex &parentIndex) const
{
    if (!hasIndex(row, column, parentIndex)) {
        return QModelIndex();
    }

    AuthorGroupItem *parentItem = nullptr;
    if (!parentIndex.isValid()) {
        parentItem = d->mRoot;
    } else {
        parentItem = static_cast<AuthorGroupItem *>(parentIndex.internalPointer());
    }

    AuthorGroupItem *child = parentItem->child(row);
    if (child) {
        return createIndex(row, column, child);
    } else {
        return QModelIndex();
    }
}

QModelIndex AuthorGroupProxyModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    AuthorGroupItem *childItem = static_cast<AuthorGroupItem *>(index.internalPointer());
    AuthorGroupItem *parentItem = childItem->parent();

    if (parentItem == d->mRoot) {
        return QModelIndex();
    } else {
        return createIndex(parentItem->row(), 0, parentItem);
    }
}

QModelIndex AuthorGroupProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid()) {
        return QModelIndex();
    }

    const AuthorGroupItem *item = d->mRoot->findIndex(sourceIndex);
    if (!item) {
        return QModelIndex();
    }

    return createIndex(item->row(), 0, const_cast<AuthorGroupItem *>(item));
}

QModelIndex AuthorGroupProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid()) {
        return QModelIndex();
    }

    const AuthorGroupItem *item = static_cast<AuthorGroupItem *>(proxyIndex.internalPointer());

    return item->index();
}

void AuthorGroupProxyModel::setSourceModel(QAbstractItemModel *model)
{
    if (sourceModel()) {
        disconnect(sourceModel(), &QAbstractItemModel::layoutAboutToBeChanged, this, &AuthorGroupProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::layoutChanged, this, &AuthorGroupProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::modelAboutToBeReset, this, &AuthorGroupProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::modelReset, this, &AuthorGroupProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsAboutToBeInserted, this, &AuthorGroupProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsInserted, this, &AuthorGroupProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsAboutToBeRemoved, this, &AuthorGroupProxyModel::aboutToRebuild);
        disconnect(sourceModel(), &QAbstractItemModel::rowsRemoved, this, &AuthorGroupProxyModel::rebuild);
        disconnect(sourceModel(), &QAbstractItemModel::dataChanged, this, &AuthorGroupProxyModel::sourceDataChanged);
    }

    QAbstractProxyModel::setSourceModel(model);

    connect(sourceModel(), &QAbstractItemModel::layoutAboutToBeChanged, this, &AuthorGroupProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::layoutChanged, this, &AuthorGroupProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::modelAboutToBeReset, this, &AuthorGroupProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::modelReset, this, &AuthorGroupProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsAboutToBeInserted, this, &AuthorGroupProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsInserted, this, &AuthorGroupProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsAboutToBeRemoved, this, &AuthorGroupProxyModel::aboutToRebuild);
    connect(sourceModel(), &QAbstractItemModel::rowsRemoved, this, &AuthorGroupProxyModel::rebuild);
    connect(sourceModel(), &QAbstractItemModel::dataChanged, this, &AuthorGroupProxyModel::sourceDataChanged);

    rebuildIndexes();
}

static bool isAuthorItem(const QModelIndex &index)
{
    if (!index.isValid()) {
        return false;
    }

    const AuthorGroupItem *item = static_cast<AuthorGroupItem *>(index.internalPointer());
    return (item->type() == AuthorGroupItem::Author);
}

static const AuthorGroupItem *authorDropTarget(const AuthorGroupProxyModel *model, int row, const QModelIndex &parent)
{
    if (parent.isValid()) {
        const auto *item = static_cast<const AuthorGroupItem *>(parent.internalPointer());
        while (item && item->type() != AuthorGroupItem::Author) {
            item = item->parent();
        }
        return item;
    }

    // QTreeView reports drops between top-level rows as an insertion row with
    // an invalid parent. Treat that position as a drop on the nearest author.
    const int authorCount = model->rowCount(QModelIndex());
    if (row < 0 || authorCount == 0) {
        return nullptr;
    }
    const QModelIndex candidate = model->index(qMin(row, authorCount - 1), 0);
    return isAuthorItem(candidate) ? static_cast<const AuthorGroupItem *>(candidate.internalPointer()) : nullptr;
}

bool AuthorGroupProxyModel::isAuthorGroup(const QModelIndex &index) const
{
    return index.model() == this && isAuthorItem(index);
}

static const AnnotationModel *annotationModelFor(const QAbstractItemModel *model)
{
    while (const auto proxyModel = qobject_cast<const QAbstractProxyModel *>(model)) {
        model = proxyModel->sourceModel();
    }
    return qobject_cast<const AnnotationModel *>(model);
}

static QModelIndex annotationIndexFor(const AnnotationModel *model, int page, const QString &uniqueName)
{
    if (!model || page < 0 || uniqueName.isEmpty()) {
        return {};
    }

    for (int pageRow = 0; pageRow < model->rowCount(); ++pageRow) {
        const QModelIndex pageIndex = model->index(pageRow, 0);
        if (model->data(pageIndex, AnnotationModel::PageRole).toInt() != page) {
            continue;
        }
        for (int annotationRow = 0; annotationRow < model->rowCount(pageIndex); ++annotationRow) {
            const QModelIndex annotationIndex = model->index(annotationRow, 0, pageIndex);
            const Okular::Annotation *annotation = model->annotationForIndex(annotationIndex);
            if (annotation && annotation->uniqueName() == uniqueName) {
                return annotationIndex;
            }
        }
        break;
    }
    return {};
}

static QModelIndex annotationIndexInBaseModel(const QAbstractItemModel *source, QModelIndex index)
{
    while (const auto proxyModel = qobject_cast<const QAbstractProxyModel *>(source)) {
        index = proxyModel->mapToSource(index);
        source = proxyModel->sourceModel();
    }
    return qobject_cast<const AnnotationModel *>(source) ? index : QModelIndex();
}

static const QString annotationAuthorMimeType = QStringLiteral("application/x-okular-annotation-author");

static bool decodeAnnotationAuthorMimeData(const QMimeData *mimeData, const QUuid &dragToken, int *page, QString *sourceAuthor, QStringList *uniqueNames)
{
    if (!mimeData || !mimeData->hasFormat(annotationAuthorMimeType)) {
        return false;
    }

    QDataStream stream(mimeData->data(annotationAuthorMimeType));
    QUuid token;
    stream >> token >> *page >> *sourceAuthor >> *uniqueNames;
    if (stream.status() != QDataStream::Ok || token != dragToken || *page < 0 || uniqueNames->size() != 1 || uniqueNames->first().isEmpty()) {
        return false;
    }
    return true;
}

QStringList AuthorGroupProxyModel::mimeTypes() const
{
    return {annotationAuthorMimeType};
}

QMimeData *AuthorGroupProxyModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mimeData = new QMimeData;
    if (indexes.isEmpty()) {
        return mimeData;
    }

    const QModelIndex proxyIndex = indexes.first();
    const auto *item = static_cast<const AuthorGroupItem *>(proxyIndex.internalPointer());
    const AnnotationModel *model = annotationModelFor(sourceModel());
    int page = -1;
    QString sourceAuthor;
    QStringList uniqueNames;
    if (item->type() != AuthorGroupItem::Annotation) {
        return mimeData;
    }

    const QModelIndex annotationIndex = annotationIndexInBaseModel(sourceModel(), item->index());
    const Okular::Annotation *annotation = model ? model->annotationForIndex(annotationIndex) : nullptr;
    if (!annotation || !model->canChangeAnnotationAuthor(annotationIndex)) {
        return mimeData;
    }
    page = model->data(annotationIndex, AnnotationModel::PageRole).toInt();
    sourceAuthor = model->data(annotationIndex, AnnotationModel::AuthorRole).toString();
    uniqueNames.append(annotation->uniqueName());

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << m_dragToken << page << sourceAuthor << uniqueNames;
    if (stream.status() == QDataStream::Ok) {
        mimeData->setData(annotationAuthorMimeType, payload);
    }
    return mimeData;
}

bool AuthorGroupProxyModel::canDropMimeData(const QMimeData *mimeData, Qt::DropAction action, int row, int column, const QModelIndex &parent) const
{
    if (action != Qt::MoveAction || row < -1 || column > 0) {
        return false;
    }

    int page = -1;
    QString sourceAuthor;
    QStringList uniqueNames;
    if (!decodeAnnotationAuthorMimeData(mimeData, m_dragToken, &page, &sourceAuthor, &uniqueNames)) {
        return false;
    }

    const AuthorGroupItem *target = authorDropTarget(this, row, parent);
    const AnnotationModel *model = annotationModelFor(sourceModel());
    if (!model || !target || sourceAuthor == target->author()) {
        return false;
    }

    QModelIndexList annotationIndexes;
    for (const QString &uniqueName : std::as_const(uniqueNames)) {
        const QModelIndex annotationIndex = annotationIndexFor(model, page, uniqueName);
        const Okular::Annotation *annotation = model->annotationForIndex(annotationIndex);
        if (!annotation || model->data(annotationIndex, AnnotationModel::AuthorRole).toString() != sourceAuthor) {
            return false;
        }
        annotationIndexes.append(annotationIndex);
    }
    return annotationIndexes.size() == 1 && model->canChangeAnnotationAuthor(annotationIndexes.first());
}

bool AuthorGroupProxyModel::dropMimeData(const QMimeData *mimeData, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (!canDropMimeData(mimeData, action, row, column, parent)) {
        return false;
    }

    int page = -1;
    QString sourceAuthor;
    QStringList uniqueNames;
    if (!decodeAnnotationAuthorMimeData(mimeData, m_dragToken, &page, &sourceAuthor, &uniqueNames)) {
        return false;
    }

    const AuthorGroupItem *target = authorDropTarget(this, row, parent);
    if (!target) {
        return false;
    }
    const QString targetAuthor = target->author();
    const AnnotationModel *model = annotationModelFor(sourceModel());
    if (!model) {
        return false;
    }

    QModelIndexList annotationIndexes;
    for (const QString &uniqueName : std::as_const(uniqueNames)) {
        annotationIndexes.append(annotationIndexFor(model, page, uniqueName));
    }
    return annotationIndexes.size() == 1 && const_cast<AnnotationModel *>(model)->changeAnnotationAuthor(annotationIndexes.first(), targetAuthor);
}

Qt::DropActions AuthorGroupProxyModel::supportedDragActions() const
{
    // QAbstractProxyModel otherwise forwards CopyAction from the source model,
    // which QAbstractItemView::InternalMove excludes before starting the drag.
    return Qt::MoveAction;
}

Qt::DropActions AuthorGroupProxyModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QItemSelection AuthorGroupProxyModel::mapSelectionToSource(const QItemSelection &selection) const
{
    const QModelIndexList proxyIndexes = selection.indexes();
    QItemSelection sourceSelection;
    for (const QModelIndex &proxyIndex : proxyIndexes) {
        if (!isAuthorItem(proxyIndex)) {
            sourceSelection << QItemSelectionRange(mapToSource(proxyIndex));
        }
    }

    return sourceSelection;
}

QItemSelection AuthorGroupProxyModel::mapSelectionFromSource(const QItemSelection &selection) const
{
    return QAbstractProxyModel::mapSelectionFromSource(selection);
}

QVariant AuthorGroupProxyModel::data(const QModelIndex &proxyIndex, int role) const
{
    if (isAuthorItem(proxyIndex)) {
        const AuthorGroupItem *item = static_cast<AuthorGroupItem *>(proxyIndex.internalPointer());
        if (role == Qt::DisplayRole) {
            return item->author();
        } else if (role == Qt::DecorationRole) {
            return QIcon::fromTheme(item->author().isEmpty() ? QStringLiteral("user-away") : QStringLiteral("user-identity"));
        } else {
            return QVariant();
        }
    } else {
        return QAbstractProxyModel::data(proxyIndex, role);
    }
}

QMap<int, QVariant> AuthorGroupProxyModel::itemData(const QModelIndex &index) const
{
    if (isAuthorItem(index)) {
        return QMap<int, QVariant>();
    } else {
        return QAbstractProxyModel::itemData(index);
    }
}

Qt::ItemFlags AuthorGroupProxyModel::flags(const QModelIndex &index) const
{
    if (isAuthorItem(index)) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    }

    Qt::ItemFlags itemFlags = QAbstractProxyModel::flags(index);
    if (index.isValid()) {
        const auto *item = static_cast<const AuthorGroupItem *>(index.internalPointer());
        if (item->type() == AuthorGroupItem::Annotation) {
            const AnnotationModel *model = annotationModelFor(sourceModel());
            const QModelIndex annotationIndex = annotationIndexInBaseModel(sourceModel(), item->index());
            if (model && model->canChangeAnnotationAuthor(annotationIndex)) {
                itemFlags |= Qt::ItemIsDragEnabled;
            }
        }
    }
    return itemFlags;
}

void AuthorGroupProxyModel::doRebuildIndexes()
{
    delete d->mRoot;
    d->mRoot = new AuthorGroupItem(nullptr);

    QMap<QString, AuthorGroupItem *> authorMap;

    const auto appendAnnotation = [this, &authorMap](const QModelIndex &annotationIndex) {
        const QString author = sourceModel()->data(annotationIndex, AnnotationModel::AuthorRole).toString();
        AuthorGroupItem *authorItem = authorMap.value(author, nullptr);
        if (!authorItem) {
            authorItem = new AuthorGroupItem(d->mRoot, AuthorGroupItem::Author);
            authorItem->setAuthor(author);
            d->mRoot->appendChild(authorItem);
            authorMap.insert(author, authorItem);
        }

        authorItem->appendChild(new AuthorGroupItem(authorItem, AuthorGroupItem::Annotation, annotationIndex));
    };

    for (int row = 0; row < sourceModel()->rowCount(); ++row) {
        const QModelIndex idx = sourceModel()->index(row, 0);
        appendAnnotation(idx);
    }
}

void AuthorGroupProxyModel::aboutToRebuild()
{
    beginResetModel();
}

void AuthorGroupProxyModel::rebuild()
{
    doRebuildIndexes();
    endResetModel();
}

void AuthorGroupProxyModel::rebuildIndexes()
{
    aboutToRebuild();
    rebuild();
}

void AuthorGroupProxyModel::sourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    if (roles.isEmpty() || roles.contains(AnnotationModel::AuthorRole)) {
        rebuildIndexes();
        return;
    }
    Q_EMIT dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
}

#include "moc_annotationproxymodels.cpp"
