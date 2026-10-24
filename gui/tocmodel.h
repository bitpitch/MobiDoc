/*
    SPDX-FileCopyrightText: 2007 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TOCMODEL_H
#define TOCMODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <QUuid>

namespace Okular
{
class Document;
class DocumentSynopsis;
class DocumentViewport;
}

class TOCModelPrivate;

class TOCModel : public QAbstractItemModel
{
    Q_OBJECT
    /**
     * How many items are in this model, useful for QML
     */
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { PageRole = 0x000f0001, PageLabelRole, HighlightRole, HighlightedParentRole, BookmarkIdRole };

    explicit TOCModel(Okular::Document *document, QObject *parent = nullptr);
    ~TOCModel() override;

    // reimplementations from QAbstractItemModel
    QHash<int, QByteArray> roleNames() const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction;
    }
    QModelIndex indexForBookmarkId(const QString &id, const QModelIndex &parent = QModelIndex()) const;

    void fill(const Okular::DocumentSynopsis *toc);
    void clear();
    void setCurrentViewport(const Okular::DocumentViewport &viewport);

    bool isEmpty() const;
    bool equals(const TOCModel *model) const;
    void setOldModelData(TOCModel *model, const QList<QModelIndex> &list);
    bool hasOldModelData() const;
    TOCModel *clearOldModelData() const;

    QString externalFileNameForIndex(const QModelIndex &index) const;
    Okular::DocumentViewport viewportForIndex(const QModelIndex &index) const;
    QString urlForIndex(const QModelIndex &index) const;

    int count() const
    {
        return rowCount();
    }

Q_SIGNALS:
    void countChanged();

private:
    QUuid m_dragToken = QUuid::createUuid();
    QString draggedBookmark(const QMimeData *data) const;
    // storage
    friend class TOCModelPrivate;
    TOCModelPrivate *const d;
    bool checkequality(const TOCModel *model, const QModelIndex &parentA = QModelIndex(), const QModelIndex &parentB = QModelIndex()) const;
};

#endif
