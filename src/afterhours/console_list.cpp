// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QLocale>
#include <QPainter>
#include <QStandardItemModel>

#include "afterhours/console_list.h"
#include "afterhours/theme.h"

namespace AfterHours {

namespace {
enum class Kind { All, Header, Console };
constexpr int KindRole = Qt::UserRole + 1;
constexpr int IdRole = Qt::UserRole + 2;
constexpr int CountRole = Qt::UserRole + 3;
constexpr int LogoRole = Qt::UserRole + 4;
constexpr QSize LogoBox{176, 46};
/// The area every logo is sized to (a 2.6:1 wordmark is about 97 x 37).
constexpr int LogoArea = 3600;
} // Anonymous namespace

QSize ConsoleListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    const auto kind = static_cast<Kind>(index.data(KindRole).toInt());
    if (kind == Kind::Header) {
        // Room above for the break between makers.
        return {option.rect.width(), index.row() <= 1 ? 40 : 62};
    }
    // Consoles with a logo show it large with the name beneath.
    return {option.rect.width(), index.data(LogoRole).toString().isEmpty() ? 58 : 92};
}

void ConsoleListDelegate::ClearLogos() {
    logos.clear();
}

QImage ConsoleListDelegate::Logo(const QString& file, QSize size, qreal ratio) const {
    if (file.isEmpty()) {
        return {};
    }
    const QString cache_key = file + QLatin1Char('@') + QString::number(size.width());
    if (const auto it = logos.find(cache_key); it != logos.end()) {
        return it->second;
    }
    // Rendered at the box's size (vector logos stay sharp), in their own colours.
    QImage image = Theme::ReadLogo(file, size, ratio, LogoArea);
    logos.emplace(cache_key, image);
    return image;
}

void ConsoleListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const auto kind = static_cast<Kind>(index.data(KindRole).toInt());
    const QRect rect = option.rect.adjusted(4, 2, -8, -2);

    if (kind == Kind::Header) {
        // A rule across the sidebar, then the maker's name.
        if (index.row() > 1) {
            painter->fillRect(QRectF(rect.left() + 12, rect.top() + 14, rect.width() - 24, 1),
                              Theme::Alpha(Qt::white, 22));
        }
        painter->setFont(Theme::EyebrowFont(11));
        painter->setPen(Theme::Primary);
        painter->drawText(rect.adjusted(12, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;
    const bool hot = option.state & QStyle::State_MouseOver;
    if (selected || hot) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? Theme::Alpha(Theme::Primary, 0x17)
                                   : Theme::Alpha(Qt::white, 8));
        painter->drawRoundedRect(rect, 9, 9);
    }
    if (selected) {
        painter->setBrush(Theme::Primary);
        painter->drawRoundedRect(QRectF(rect.left(), rect.top() + 10, 3, rect.height() - 20), 1.5,
                                 1.5);
    }
    const QColor ink = selected ? Theme::Primary : hot ? Theme::Foreground
                                                       : Theme::Alpha(Theme::Foreground, 225);
    const QColor soft = selected ? Theme::Alpha(Theme::Primary, 210) : QColor(0xc4, 0xc4, 0xd0);

    // The game count as a small pill on the right.
    const QString count = QLocale{}.toString(index.data(CountRole).toInt());
    painter->setFont(Theme::Font(12, QFont::DemiBold));
    const int count_width = painter->fontMetrics().horizontalAdvance(count) + 16;
    const QRectF pill{static_cast<qreal>(rect.right() - 10 - count_width),
                      rect.center().y() - 11.0, static_cast<qreal>(count_width), 22.0};
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? Theme::Alpha(Theme::Primary, 40) : Theme::Alpha(Qt::white, 14));
    painter->drawRoundedRect(pill, 11, 11);
    painter->setPen(selected ? Theme::Primary : soft);
    painter->drawText(pill, Qt::AlignCenter, count);

    const int left = rect.left() + 14;
    const int text_right = static_cast<int>(pill.left()) - 10;
    const QString name = index.data(Qt::DisplayRole).toString();
    const qreal ratio = option.widget ? option.widget->devicePixelRatioF() : 1.0;
    const QImage logo = kind == Kind::Console
                            ? Logo(index.data(LogoRole).toString(),
                                   QSize{std::min(LogoBox.width(), text_right - left),
                                         LogoBox.height()},
                                   ratio)
                            : QImage{};

    if (!logo.isNull()) {
        // The logo in one colour, with the console's name under it.
        const QSizeF size = logo.deviceIndependentSize();
        const QRectF logo_area{static_cast<qreal>(left), rect.top() + 10.0,
                               static_cast<qreal>(LogoBox.width()),
                               static_cast<qreal>(LogoBox.height())};
        painter->drawImage(QPointF(left, logo_area.center().y() - size.height() / 2), logo);
        painter->setFont(Theme::Font(12, QFont::Medium));
        painter->setPen(soft);
        const QRect name_rect{left, static_cast<int>(logo_area.bottom()) + 4, text_right - left,
                              22};
        painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(name, Qt::ElideRight,
                                                            name_rect.width()));
    } else {
        int text_left = left;
        if (kind == Kind::All) {
            QFont icon = ConsoleWidgets::IconFamily().isEmpty()
                             ? Theme::Font(16)
                             : QFont{ConsoleWidgets::IconFamily()};
            icon.setPixelSize(17);
            painter->setFont(icon);
            painter->setPen(ink);
            painter->drawText(QRect(left, rect.top(), 22, rect.height()), Qt::AlignCenter,
                              Theme::Icons::Library());
            text_left += 32;
        }
        painter->setFont(Theme::Font(15, QFont::DemiBold));
        painter->setPen(ink);
        const QRect name_rect{text_left, rect.top(), text_right - text_left, rect.height()};
        painter->drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(name, Qt::ElideRight,
                                                            name_rect.width()));
    }
    painter->restore();
}

ConsoleList::ConsoleList(QWidget* parent)
    : QListView{parent}, model{new QStandardItemModel(this)},
      delegate{new ConsoleListDelegate(this)} {
    setModel(model);
    setItemDelegate(delegate);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    viewport()->setAttribute(Qt::WA_Hover);
    setUniformItemSizes(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "QListView{background:transparent;outline:0;}"
        "QScrollBar:vertical{background:transparent;width:6px;margin:0;}"
        "QScrollBar::handle:vertical{background:#33343d;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"));
    viewport()->setAutoFillBackground(false);
}

void ConsoleList::SetConsoles(const std::vector<ConsoleEntry>& consoles, int total) {
    const QString selected = Selected();
    filling = true;
    model->clear();
    delegate->ClearLogos();

    auto* all = new QStandardItem(tr("All games"));
    all->setData(static_cast<int>(Kind::All), KindRole);
    all->setData(QString{}, IdRole);
    all->setData(total, CountRole);
    all->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    model->appendRow(all);

    // Makers in the order their first console appears.
    QStringList makers;
    for (const ConsoleEntry& console : consoles) {
        if (!makers.contains(console.maker)) {
            makers.append(console.maker);
        }
    }
    for (const QString& maker : makers) {
        auto* header = new QStandardItem(maker.isEmpty() ? tr("Other") : maker);
        header->setData(static_cast<int>(Kind::Header), KindRole);
        header->setFlags(Qt::NoItemFlags);
        model->appendRow(header);
        for (const ConsoleEntry& console : consoles) {
            if (console.maker != maker) {
                continue;
            }
            auto* item = new QStandardItem(console.name);
            item->setData(static_cast<int>(Kind::Console), KindRole);
            item->setData(console.id, IdRole);
            item->setData(console.count, CountRole);
            item->setData(console.logo_file, LogoRole);
            item->setToolTip(console.name);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            model->appendRow(item);
        }
    }
    Select(selected);
    filling = false;
}

void ConsoleList::Select(const QString& system_id) {
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (static_cast<Kind>(index.data(KindRole).toInt()) != Kind::Header &&
            index.data(IdRole).toString() == system_id) {
            setCurrentIndex(index);
            return;
        }
    }
    setCurrentIndex(model->index(0, 0));
}

QString ConsoleList::Selected() const {
    return currentIndex().data(IdRole).toString();
}

void ConsoleList::currentChanged(const QModelIndex& current, const QModelIndex& previous) {
    QListView::currentChanged(current, previous);
    if (!filling && current.isValid()) {
        emit ConsoleChosen(current.data(IdRole).toString());
    }
}

} // namespace AfterHours
