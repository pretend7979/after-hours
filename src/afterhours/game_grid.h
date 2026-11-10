// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unordered_map>

#include <QImage>
#include <QListView>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

#include "afterhours/types.h"

class GameMetadataStore;
class QTimer;

namespace AfterHours {

/// Item data of the game models.
namespace GameRoles {
constexpr int Key = Qt::UserRole + 10;    ///< u64 metadata key
constexpr int System = Qt::UserRole + 11; ///< console id
constexpr int Path = Qt::UserRole + 12;
constexpr int Title = Qt::UserRole + 13;      ///< from the file name
constexpr int SystemName = Qt::UserRole + 14; ///< the console's short name
constexpr int Color = Qt::UserRole + 15;      ///< the console's colour
} // namespace GameRoles

/// Where a card's parts sit inside its grid cell.
struct CardLayout {
    QRectF card;
    QRectF art;
    QRectF meta;
    QRectF title;
    QRectF play;
    QRectF details;
};

/// A game as a card: box art over its console, year, title and Play / Details buttons.
class GameCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    GameCardDelegate(GameMetadataStore& store, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void SetCardWidth(int width);
    [[nodiscard]] int CardWidth() const {
        return card_width;
    }
    [[nodiscard]] QSize CellSize() const;
    /// How much larger than the smallest card this one is drawn: text and buttons grow with it.
    [[nodiscard]] double Scale() const;
    [[nodiscard]] CardLayout Layout(const QRect& cell) const;
    void Invalidate(u64 key);
    void InvalidateAll();

private:
    [[nodiscard]] QImage Art(const QModelIndex& index, QSize size, qreal ratio) const;

    GameMetadataStore& store;
    int card_width = 230;
    mutable std::unordered_map<u64, QImage> art_cache;
};

/// The wrapping grid of game cards. Clicking a card opens its details; its Play button starts it.
class GameGrid : public QListView {
    Q_OBJECT

public:
    enum class Part { None, Card, Play, Details };

    GameGrid(GameMetadataStore& store, QWidget* parent = nullptr);

    [[nodiscard]] GameCardDelegate* Delegate() const {
        return delegate;
    }
    [[nodiscard]] bool IsHot(const QModelIndex& index, Part part) const {
        return index == hover_index && part == hover_part;
    }

signals:
    void PlayRequested(const QModelIndex& index);
    void DetailsRequested(const QModelIndex& index);
    /// Rows now on screen (with a margin), for fetching their details.
    void VisibleRowsChanged(int first, int last);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void rowsInserted(const QModelIndex& parent, int start, int end) override;
    void reset() override;

private:
    [[nodiscard]] Part PartAt(const QPoint& point, QModelIndex* index) const;
    void UpdateCardSize();
    void ScheduleVisibleRows();

    GameCardDelegate* delegate;
    QPersistentModelIndex hover_index;
    Part hover_part = Part::None;
    QPersistentModelIndex pressed_index;
    Part pressed_part = Part::None;
    QTimer* visible_timer;
};

} // namespace AfterHours
