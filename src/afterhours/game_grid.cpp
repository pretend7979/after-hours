// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextLayout>
#include <QTimer>

#include "afterhours/game_grid.h"
#include "afterhours/metadata_store.h"
#include "afterhours/theme.h"

namespace AfterHours {

namespace {
constexpr int Gap = 22;
constexpr int BodyHeight = 168;
constexpr int MinCardWidth = 240;
/// Wide windows get larger cards instead of more of them.
constexpr int MaxColumns = 5;
constexpr double ArtAspect = 0.75;
constexpr double Radius = 14.0;
constexpr int DetailsWidth = 78;
constexpr std::size_t MaxCachedArt = 600;

u64 Key(const QModelIndex& index) {
    return index.data(GameRoles::Key).toULongLong();
}

/// Up to `lines` lines of `text` wrapped to `width`, the last one elided.
QStringList WrapLines(const QString& text, const QFont& font, qreal width, int lines) {
    QTextLayout layout{text, font};
    layout.beginLayout();
    QStringList result;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        if (result.size() == lines - 1) {
            const QString rest = text.mid(line.textStart()).trimmed();
            result.append(QFontMetrics{font}.elidedText(rest, Qt::ElideRight,
                                                        static_cast<int>(width)));
            break;
        }
        result.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return result;
}

/// A colour light enough to read on the dark cards.
QColor Readable(QColor color) {
    if (!color.isValid()) {
        return Theme::Primary;
    }
    while (color.lightness() < 150) {
        color = color.lighter(125);
        if (color.lightness() == 0) {
            return Theme::Primary;
        }
    }
    return color;
}
} // Anonymous namespace

GameCardDelegate::GameCardDelegate(GameMetadataStore& store_, QObject* parent)
    : QStyledItemDelegate{parent}, store{store_} {}

void GameCardDelegate::SetCardWidth(int width) {
    if (width != card_width) {
        card_width = width;
        art_cache.clear();
    }
}

double GameCardDelegate::Scale() const {
    return std::clamp(card_width / 260.0, 1.0, 1.6);
}

QSize GameCardDelegate::CellSize() const {
    const double height = card_width * ArtAspect + BodyHeight * Scale();
    return {card_width + Gap, static_cast<int>(std::lround(height)) + Gap};
}

QSize GameCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return CellSize();
}

void GameCardDelegate::Invalidate(u64 key) {
    art_cache.erase(key);
}

void GameCardDelegate::InvalidateAll() {
    art_cache.clear();
}

CardLayout GameCardDelegate::Layout(const QRect& cell) const {
    CardLayout layout;
    layout.card = QRectF(cell).adjusted(Gap / 2.0, Gap / 2.0, -Gap / 2.0, -Gap / 2.0);
    const QRectF& card = layout.card;
    layout.art = QRectF(card.left(), card.top(), card.width(), std::round(card.width() * ArtAspect));
    const double s = Scale();
    const double pad = 16 * s;
    layout.meta =
        QRectF(card.left() + pad, layout.art.bottom() + 14 * s, card.width() - 2 * pad, 20 * s);
    layout.title =
        QRectF(layout.meta.left(), layout.meta.bottom() + 8 * s, layout.meta.width(), 54 * s);
    const double buttons_top = layout.title.bottom() + 10 * s;
    layout.details =
        QRectF(card.right() - pad - DetailsWidth * s, buttons_top, DetailsWidth * s, 34 * s);
    layout.play = QRectF(layout.meta.left(), buttons_top,
                         layout.details.left() - 6 * s - layout.meta.left(), 34 * s);
    return layout;
}

QImage GameCardDelegate::Art(const QModelIndex& index, QSize size, qreal ratio) const {
    const u64 key = Key(index);
    if (const auto it = art_cache.find(key); it != art_cache.end()) {
        return it->second;
    }
    if (art_cache.size() > MaxCachedArt) {
        art_cache.clear();
    }
    QImage art{size * ratio, QImage::Format_ARGB32_Premultiplied};
    art.setDevicePixelRatio(ratio);
    QPainter painter{&art};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds{QPointF{}, QSizeF{size}};
    QLinearGradient fill{bounds.topLeft(), bounds.bottomRight()};
    fill.setColorAt(0.0, QColor(0x25, 0x26, 0x30));
    fill.setColorAt(1.0, QColor(0x1a, 0x1b, 0x22));
    painter.fillRect(bounds, fill);

    const QImage cover = store.Cover(key);
    if (!cover.isNull()) {
        // The whole box, on its own soft glow, as on a shelf.
        const QRectF area = bounds.adjusted(18, 16, -18, -16);
        const QSizeF fitted = QSizeF{cover.size()}.scaled(area.size(), Qt::KeepAspectRatio);
        const QRectF target{area.center().x() - fitted.width() / 2,
                            area.center().y() - fitted.height() / 2, fitted.width(),
                            fitted.height()};
        painter.setOpacity(0.35);
        painter.drawImage(bounds, ConsoleWidgets::Blurred(cover, size));
        painter.setOpacity(1.0);
        painter.fillRect(bounds, QColor(0x16, 0x17, 0x1d, 150));
        for (int layer = 6; layer > 0; layer -= 2) {
            QPainterPath shadow;
            shadow.addRoundedRect(target.translated(0, 6).adjusted(-layer, -layer, layer, layer),
                                  6 + layer, 6 + layer);
            painter.fillPath(shadow, QColor(0, 0, 0, 30));
        }
        QPainterPath shape;
        shape.addRoundedRect(target, 5, 5);
        painter.setClipPath(shape);
        painter.drawImage(target, cover.scaled((target.size() * ratio).toSize(),
                                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        painter.setClipping(false);
    } else {
        // No art yet: the title in its console's colour.
        const QColor color = Readable(index.data(GameRoles::Color).value<QColor>());
        const GameMetadata metadata = store.Get(key);
        const QString name =
            metadata.name.isEmpty() ? index.data(GameRoles::Title).toString() : metadata.name;
        const QFont font = Theme::Font(std::max(15, size.width() / 12), QFont::DemiBold, true);
        painter.setFont(font);
        painter.setPen(color);
        const QRectF text_area = bounds.adjusted(22, 22, -22, -38);
        painter.drawText(text_area, Qt::AlignCenter | Qt::TextWordWrap,
                         WrapLines(name, font, text_area.width(), 3).join(QLatin1Char('\n')));
        painter.setFont(Theme::Font(10));
        painter.setPen(Theme::Alpha(color, 170));
        painter.drawText(bounds.adjusted(0, 0, 0, -16), Qt::AlignHCenter | Qt::AlignBottom,
                         index.data(GameRoles::SystemName).toString());
    }
    painter.end();
    art_cache.emplace(key, art);
    return art;
}

void GameCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    const auto* grid = qobject_cast<const GameGrid*>(option.widget);
    const auto hot = [&](GameGrid::Part part) { return grid && grid->IsHot(index, part); };
    const bool card_hot =
        hot(GameGrid::Part::Card) || hot(GameGrid::Part::Play) || hot(GameGrid::Part::Details);
    const bool focused = (option.state & QStyle::State_HasFocus) && grid && grid->hasFocus();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    if (card_hot) {
        painter->translate(0, -3);
    }
    const CardLayout layout = Layout(option.rect);

    QPainterPath shape;
    shape.addRoundedRect(layout.card, Radius, Radius);
    painter->fillPath(shape, Theme::Card);

    painter->save();
    painter->setClipPath(shape);
    const qreal ratio = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    painter->drawImage(layout.art.topLeft(),
                       Art(index, layout.art.size().toSize(), ratio));
    painter->restore();

    painter->setPen(QPen(card_hot || focused ? Theme::Alpha(Theme::Primary, focused ? 220 : 90)
                                             : Theme::Alpha(Qt::white, 14),
                         focused ? 2 : 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(shape);

    // Console and year.
    const double s = Scale();
    const auto scaled = [s](int size) { return static_cast<int>(std::lround(size * s)); };
    const GameMetadata metadata = store.Get(Key(index));
    painter->setFont(Theme::Font(scaled(12)));
    painter->setPen(Theme::Muted);
    const QString year = metadata.release_year > 0 ? QString::number(metadata.release_year)
                                                   : QString{};
    const int year_width = painter->fontMetrics().horizontalAdvance(year);
    painter->drawText(layout.meta, Qt::AlignRight | Qt::AlignVCenter, year);
    painter->drawText(layout.meta.adjusted(0, 0, -(year_width + 10), 0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(
                          index.data(GameRoles::SystemName).toString(), Qt::ElideRight,
                          static_cast<int>(layout.meta.width()) - year_width - 10));

    // Title, two lines at most.
    const QString name =
        metadata.name.isEmpty() ? index.data(GameRoles::Title).toString() : metadata.name;
    const QFont title_font = Theme::Font(scaled(16), QFont::DemiBold);
    painter->setFont(title_font);
    painter->setPen(Theme::Foreground);
    const QStringList lines = WrapLines(name, title_font, layout.title.width(), 2);
    const qreal line_height = QFontMetricsF{title_font}.lineSpacing();
    for (int i = 0; i < lines.size(); ++i) {
        painter->drawText(QRectF(layout.title.left(), layout.title.top() + i * line_height,
                                 layout.title.width(), line_height),
                          Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }

    // Play, then Details.
    const bool play_hot = hot(GameGrid::Part::Play);
    painter->setPen(Qt::NoPen);
    painter->setBrush(play_hot ? Theme::Primary : Theme::Secondary);
    painter->drawRoundedRect(layout.play, 9, 9);
    const QColor play_ink = play_hot ? Theme::PrimaryInk : Theme::Foreground;
    QFont icon = ConsoleWidgets::IconFamily().isEmpty() ? Theme::Font(scaled(12))
                                                        : QFont{ConsoleWidgets::IconFamily()};
    icon.setPixelSize(scaled(12));
    const QFont label_font = Theme::Font(scaled(13), QFont::DemiBold);
    const QString play_text = tr("Play");
    const qreal label_width = QFontMetricsF{label_font}.horizontalAdvance(play_text);
    const qreal start = layout.play.center().x() - (label_width + 20 * s) / 2;
    painter->setPen(play_ink);
    painter->setFont(icon);
    painter->drawText(QRectF(start, layout.play.top(), 14 * s, layout.play.height()),
                      Qt::AlignCenter, Theme::Icons::Play());
    painter->setFont(label_font);
    painter->drawText(QRectF(start + 20 * s, layout.play.top(), label_width + 2, layout.play.height()),
                      Qt::AlignVCenter | Qt::AlignLeft, play_text);

    const bool details_hot = hot(GameGrid::Part::Details);
    if (details_hot) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::Alpha(Qt::white, 18));
        painter->drawRoundedRect(layout.details, 9, 9);
    }
    painter->setPen(details_hot ? Theme::Foreground : Theme::Muted);
    painter->setFont(label_font);
    painter->drawText(layout.details, Qt::AlignCenter, tr("Details"));
    painter->restore();
}

GameGrid::GameGrid(GameMetadataStore& store, QWidget* parent)
    : QListView{parent}, delegate{new GameCardDelegate(store, this)},
      visible_timer{new QTimer(this)} {
    setItemDelegate(delegate);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSpacing(0);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "QListView{background:transparent;outline:0;}"
        "QScrollBar:vertical{background:transparent;width:10px;margin:4px 2px;}"
        "QScrollBar::handle:vertical{background:#33343d;border-radius:3px;min-height:40px;}"
        "QScrollBar::handle:vertical:hover{background:#4a4b57;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"));
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(40);
    visible_timer->setSingleShot(true);
    visible_timer->setInterval(200);
    connect(visible_timer, &QTimer::timeout, this, [this] {
        if (!model() || model()->rowCount() == 0) {
            return;
        }
        const QModelIndex first = indexAt(QPoint{Gap, Gap});
        const QSize cell = delegate->CellSize();
        const int columns = std::max(1, viewport()->width() / std::max(1, cell.width()));
        const int rows = viewport()->height() / std::max(1, cell.height()) + 2;
        const int start = first.isValid() ? first.row() : 0;
        emit VisibleRowsChanged(std::max(0, start - columns),
                                std::min(model()->rowCount() - 1, start + columns * (rows + 1)));
    });
}

void GameGrid::UpdateCardSize() {
    const int available = viewport()->width() - 2;
    const int columns = std::clamp(available / (MinCardWidth + Gap), 1, MaxColumns);
    // A few pixels of slack: Qt wraps a row that fills the viewport exactly.
    const int width = std::clamp((available - 6) / columns - Gap, 160, 720);
    delegate->SetCardWidth(width);
    setGridSize(delegate->CellSize());
}

void GameGrid::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    UpdateCardSize();
    ScheduleVisibleRows();
}

void GameGrid::ScheduleVisibleRows() {
    visible_timer->start();
}

void GameGrid::scrollContentsBy(int dx, int dy) {
    QListView::scrollContentsBy(dx, dy);
    ScheduleVisibleRows();
}

void GameGrid::rowsInserted(const QModelIndex& parent, int start, int end) {
    QListView::rowsInserted(parent, start, end);
    ScheduleVisibleRows();
}

void GameGrid::reset() {
    QListView::reset();
    hover_index = QPersistentModelIndex{};
    hover_part = Part::None;
    ScheduleVisibleRows();
}

GameGrid::Part GameGrid::PartAt(const QPoint& point, QModelIndex* index) const {
    const QModelIndex at = indexAt(point);
    if (index) {
        *index = at;
    }
    if (!at.isValid()) {
        return Part::None;
    }
    const CardLayout layout = delegate->Layout(visualRect(at));
    // Hovered cards are drawn a little higher; the point is tested against the drawn card.
    const QPointF p = QPointF(point) + QPointF(0, at == hover_index ? 3 : 0);
    if (layout.play.contains(p)) {
        return Part::Play;
    }
    if (layout.details.contains(p)) {
        return Part::Details;
    }
    return layout.card.contains(p) ? Part::Card : Part::None;
}

void GameGrid::mouseMoveEvent(QMouseEvent* event) {
    QModelIndex index;
    const Part part = PartAt(event->position().toPoint(), &index);
    if (index != hover_index || part != hover_part) {
        const QModelIndex old = hover_index;
        hover_index = part == Part::None ? QModelIndex{} : index;
        hover_part = part;
        viewport()->setCursor(part == Part::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        if (old.isValid()) {
            viewport()->update(visualRect(old).adjusted(0, -4, 0, 4));
        }
        if (hover_index.isValid()) {
            viewport()->update(visualRect(hover_index).adjusted(0, -4, 0, 4));
        }
    }
    QListView::mouseMoveEvent(event);
}

void GameGrid::leaveEvent(QEvent* event) {
    if (hover_index.isValid()) {
        viewport()->update(visualRect(hover_index).adjusted(0, -4, 0, 4));
    }
    hover_index = QPersistentModelIndex{};
    hover_part = Part::None;
    QListView::leaveEvent(event);
}

void GameGrid::mousePressEvent(QMouseEvent* event) {
    QModelIndex index;
    pressed_part = PartAt(event->position().toPoint(), &index);
    pressed_index = index;
    QListView::mousePressEvent(event);
}

void GameGrid::mouseReleaseEvent(QMouseEvent* event) {
    QListView::mouseReleaseEvent(event);
    if (event->button() != Qt::LeftButton) {
        return;
    }
    QModelIndex index;
    const Part part = PartAt(event->position().toPoint(), &index);
    if (part == Part::None || index != pressed_index || part != pressed_part) {
        return;
    }
    if (part == Part::Play) {
        emit PlayRequested(index);
    } else {
        emit DetailsRequested(index);
    }
}

void GameGrid::mouseDoubleClickEvent(QMouseEvent* event) {
    // A double click is two clicks: the first already acted.
    event->accept();
}

void GameGrid::keyPressEvent(QKeyEvent* event) {
    const QModelIndex index = currentIndex();
    if (index.isValid()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (event->modifiers() & Qt::ControlModifier) {
                emit PlayRequested(index);
            } else {
                emit DetailsRequested(index);
            }
            return;
        case Qt::Key_P:
            emit PlayRequested(index);
            return;
        default:
            break;
        }
    }
    QListView::keyPressEvent(event);
}

} // namespace AfterHours
