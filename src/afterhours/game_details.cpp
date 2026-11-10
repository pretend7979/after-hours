// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "afterhours/game_details.h"
#include "afterhours/game_grid.h"
#include "afterhours/metadata_store.h"
#include "afterhours/theme.h"
#include "afterhours/trailer_view.h"

namespace AfterHours {

/// The game's box art, whole, on a soft copy of itself.
class CoverView : public QWidget {
public:
    explicit CoverView(QWidget* parent) : QWidget{parent} {
        setMinimumHeight(220);
    }

    void SetCover(const QImage& cover_, const QString& fallback_, const QColor& color_) {
        cover = cover_;
        fallback = fallback_;
        color = color_;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF bounds = rect();
        QPainterPath shape;
        shape.addRoundedRect(bounds, 14, 14);
        painter.setClipPath(shape);
        QLinearGradient fill{bounds.topLeft(), bounds.bottomRight()};
        fill.setColorAt(0.0, QColor(0x25, 0x26, 0x30));
        fill.setColorAt(1.0, QColor(0x1a, 0x1b, 0x22));
        painter.fillRect(bounds, fill);
        if (cover.isNull()) {
            painter.setPen(color.isValid() ? color.lighter(150) : Theme::Primary);
            painter.setFont(Theme::Font(22, QFont::DemiBold, true));
            painter.drawText(bounds.adjusted(28, 28, -28, -28), Qt::AlignCenter | Qt::TextWordWrap,
                             fallback);
            return;
        }
        painter.setOpacity(0.4);
        painter.drawImage(bounds, ConsoleWidgets::Blurred(cover, size()));
        painter.setOpacity(1.0);
        painter.fillRect(bounds, QColor(0x16, 0x17, 0x1d, 140));
        const QRectF area = bounds.adjusted(24, 22, -24, -22);
        const QSizeF fitted = QSizeF{cover.size()}.scaled(area.size(), Qt::KeepAspectRatio);
        const QRectF target{area.center().x() - fitted.width() / 2,
                            area.center().y() - fitted.height() / 2, fitted.width(),
                            fitted.height()};
        QPainterPath art;
        art.addRoundedRect(target, 6, 6);
        painter.setClipPath(art);
        painter.drawImage(target, cover);
    }

private:
    QImage cover;
    QString fallback;
    QColor color;
};

namespace {
QLabel* MakeLabel(const QFont& font, const QColor& color, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
    return label;
}
} // Anonymous namespace

GameDetails::GameDetails(GameMetadataStore& store_, QWidget* parent)
    : QWidget{parent}, store{store_} {
    hide();
    setFocusPolicy(Qt::StrongFocus);
    panel = new QWidget(this);

    eyebrow = MakeLabel(Theme::EyebrowFont(11), Theme::Primary, panel);
    auto* close = new Theme::Button(Theme::Button::Kind::Icon, Theme::Icons::Close(), tr("Close"),
                                    panel);
    title = MakeLabel(Theme::Font(34, QFont::DemiBold, true), Theme::Foreground, panel);
    title->setWordWrap(true);
    subtitle = MakeLabel(Theme::Font(15), Theme::Muted, panel);
    subtitle->setWordWrap(true);

    auto* heading = new QHBoxLayout;
    heading->addWidget(eyebrow, 1);
    heading->addWidget(close, 0, Qt::AlignTop);

    // Media: the box art and, when there is one, the trailer.
    cover = new CoverView(panel);
    video_label = MakeLabel(Theme::EyebrowFont(10), Theme::Muted, panel);
    video_label->setText(tr("A look at the game"));
    trailer = new TrailerView(panel);
    trailer->setMinimumHeight(160);
    auto* media = new QVBoxLayout;
    media->setSpacing(12);
    media->addWidget(cover, 5);
    media->addSpacing(6);
    media->addWidget(video_label);
    media->addWidget(trailer, 4);
    media->addStretch(0);

    // Information: actions, how it runs, the story and the facts.
    play = new Theme::Button(Theme::Button::Kind::Primary, Theme::Icons::Play(), tr("Play game"),
                             panel);
    static_cast<Theme::Button*>(play)->SetLarge(true);
    game_settings = new Theme::Button(Theme::Button::Kind::Outline, Theme::Icons::Settings(),
                                      tr("Game settings"), panel);
    static_cast<Theme::Button*>(game_settings)->SetLarge(true);
    edit = new Theme::Button(Theme::Button::Kind::Outline, Theme::Icons::Edit(), tr("Edit info"),
                             panel);
    static_cast<Theme::Button*>(edit)->SetLarge(true);
    folder = new Theme::Button(Theme::Button::Kind::Ghost, Theme::Icons::Folder(),
                               tr("Show file"), panel);
    static_cast<Theme::Button*>(folder)->SetLarge(true);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addWidget(play);
    actions->addWidget(game_settings);
    actions->addWidget(edit);
    actions->addWidget(folder);
    actions->addStretch();

    status = MakeLabel(Theme::Font(13), Theme::Muted, panel);
    status->setTextFormat(Qt::RichText);
    settings = new Theme::Button(Theme::Button::Kind::Ghost, Theme::Icons::Settings(),
                                 tr("Console settings"), panel);
    get_emulator = new Theme::Button(Theme::Button::Kind::Primary, Theme::Icons::Download(),
                                     tr("Download"), panel);
    get_emulator->hide();
    plays_with = new QComboBox(panel);
    plays_with->setToolTip(tr("The emulator this game starts in"));
    plays_with->setMinimumWidth(240);
    auto* plays_label = MakeLabel(Theme::EyebrowFont(10), Theme::Muted, panel);
    plays_label->setText(tr("Plays with"));
    auto* status_row = new QHBoxLayout;
    status_row->setSpacing(10);
    status_row->addWidget(plays_label);
    status_row->addWidget(plays_with);
    status_row->addWidget(get_emulator);
    status_row->addWidget(settings);
    status_row->addStretch();
    status->setWordWrap(true);

    auto* about = MakeLabel(Theme::Font(17, QFont::DemiBold, true), Theme::Foreground, panel);
    about->setText(tr("About the game"));
    summary = MakeLabel(Theme::Font(16), QColor(0xd9, 0xd7, 0xe0), panel);
    summary->setWordWrap(true);
    summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* summary_scroll = new QScrollArea(panel);
    summary_scroll->setWidget(summary);
    summary_scroll->setWidgetResizable(true);
    summary_scroll->setFrameShape(QFrame::NoFrame);
    summary_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    summary_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"
        "QScrollBar:vertical{background:transparent;width:6px;}"
        "QScrollBar::handle:vertical{background:#33343d;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"));

    facts = new QGridLayout;
    facts->setHorizontalSpacing(28);
    facts->setVerticalSpacing(12);

    auto* information = new QVBoxLayout;
    information->setSpacing(14);
    information->addLayout(actions);
    information->addLayout(status_row);
    information->addWidget(status);
    information->addSpacing(8);
    information->addWidget(about);
    information->addWidget(summary_scroll, 1);
    information->addSpacing(6);
    information->addLayout(facts);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(32);
    columns->addLayout(media, 9);
    columns->addLayout(information, 11);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(34, 28, 30, 30);
    layout->setSpacing(6);
    layout->addLayout(heading);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(18);
    layout->addLayout(columns, 1);

    connect(close, &QAbstractButton::clicked, this, &GameDetails::Close);
    connect(play, &QAbstractButton::clicked, this, [this] {
        if (index.isValid()) {
            emit PlayRequested(index);
        }
    });
    connect(game_settings, &QAbstractButton::clicked, this, [this] {
        if (index.isValid()) {
            emit GameSettingsRequested(index);
        }
    });
    connect(edit, &QAbstractButton::clicked, this, [this] {
        if (index.isValid()) {
            emit EditRequested(index);
        }
    });
    connect(folder, &QAbstractButton::clicked, this, [this] {
        const QFileInfo file{index.data(GameRoles::Path).toString()};
        QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
    });
    connect(settings, &QAbstractButton::clicked, this, [this] {
        emit SettingsRequested(index.data(GameRoles::System).toString());
    });
    connect(get_emulator, &QAbstractButton::clicked, this, [this] {
        if (!emulator_id.isEmpty()) {
            emit DownloadRequested(emulator_id);
        }
    });
    connect(plays_with, &QComboBox::activated, this, [this](int row) {
        if (index.isValid()) {
            emit EmulatorChosen(index, plays_with->itemData(row).toString());
        }
    });
    connect(trailer, &TrailerView::Failed, this, [this] {
        trailer->hide();
        video_label->hide();
    });
}

void GameDetails::Open(const QModelIndex& index_, const LaunchInfo& launch_) {
    index = index_;
    launch = launch_;
    video_id.clear();
    Fill();
    setGeometry(parentWidget()->rect());
    LayoutPanel();
    show();
    raise();
    play->setFocus();
}

void GameDetails::SetLaunch(const LaunchInfo& launch_) {
    launch = launch_;
    if (isVisible()) {
        Fill();
    }
}

void GameDetails::Close() {
    trailer->Stop();
    hide();
    index = QPersistentModelIndex{};
    if (parentWidget()) {
        parentWidget()->setFocus();
    }
}

void GameDetails::Refresh(u64 key) {
    if (isVisible() && index.isValid() && index.data(GameRoles::Key).toULongLong() == key) {
        Fill();
    }
}

void GameDetails::Fill() {
    const u64 key = index.data(GameRoles::Key).toULongLong();
    const GameMetadata metadata = store.Get(key);
    const QString name =
        metadata.name.isEmpty() ? index.data(GameRoles::Title).toString() : metadata.name;
    eyebrow->setText(tr("%1  /  Game details").arg(launch.console));
    title->setText(name);
    QStringList parts;
    if (metadata.release_year > 0) {
        parts.append(QString::number(metadata.release_year));
    }
    if (!metadata.genres.isEmpty()) {
        parts.append(metadata.genres.mid(0, 3).join(QStringLiteral(", ")));
    }
    if (!metadata.developer.isEmpty()) {
        parts.append(metadata.developer);
    }
    subtitle->setText(parts.isEmpty() ? tr("Rediscover a classic.")
                                      : parts.join(QStringLiteral("  ·  ")));
    cover->SetCover(store.Cover(key), name, index.data(GameRoles::Color).value<QColor>());
    summary->setText(metadata.summary.isEmpty()
                         ? tr("No description yet. Choose Edit info to look this game up or "
                              "write your own.")
                         : metadata.summary);

    if (launch.emulator.isEmpty()) {
        status->setText(tr("No emulator is set for this console."));
    } else if (launch.installed) {
        status->setText(tr("<span style='color:#d8f077'>●</span>&nbsp; Plays in %1")
                            .arg(launch.emulator.toHtmlEscaped()));
    } else {
        status->setText(tr("<span style='color:#ff8791'>●</span>&nbsp; %1 isn't installed yet")
                            .arg(launch.emulator.toHtmlEscaped()));
    }
    play->setEnabled(launch.installed);
    emulator_id = launch.emulator_id;
    const bool can_get = !launch.installed && (launch.downloadable || launch.has_website);
    get_emulator->setText(launch.downloadable ? tr("Download %1").arg(launch.emulator)
                                              : tr("%1 website").arg(launch.emulator));
    get_emulator->setVisible(can_get);
    game_settings->setVisible(launch.has_game_settings);
    game_settings->setEnabled(launch.installed);

    QSignalBlocker block{plays_with};
    plays_with->clear();
    plays_with->addItem(tr("Console's choice (%1)").arg(launch.console_emulator), QString{});
    for (const EmulatorOption& option : launch.options) {
        plays_with->addItem(option.installed ? option.name : tr("%1 (not installed)").arg(option.name),
                            option.id);
    }
    plays_with->setCurrentIndex(std::max(0, plays_with->findData(launch.game_choice)));

    while (QLayoutItem* item = facts->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto add_fact = [this](const QString& label, const QString& value) {
        if (value.isEmpty()) {
            return;
        }
        const int position = facts->count() / 2;
        auto* term = MakeLabel(Theme::EyebrowFont(10), Theme::Muted, panel);
        term->setText(label);
        auto* detail = MakeLabel(Theme::Font(14, QFont::Medium), Theme::Foreground, panel);
        detail->setText(value);
        detail->setWordWrap(true);
        facts->addWidget(term, (position / 2) * 2, position % 2);
        facts->addWidget(detail, (position / 2) * 2 + 1, position % 2);
    };
    add_fact(tr("Developer"), metadata.developer);
    add_fact(tr("Released"),
             metadata.release_year > 0 ? QString::number(metadata.release_year) : QString{});
    add_fact(tr("Genres"), metadata.genres.join(QStringLiteral(", ")));
    add_fact(tr("Console"), launch.console);
    add_fact(tr("File"), QFileInfo{index.data(GameRoles::Path).toString()}.fileName());

    const bool has_video = !metadata.video_id.isEmpty() && TrailerView::IsAvailable();
    video_label->setVisible(has_video);
    trailer->setVisible(has_video);
    if (has_video && metadata.video_id != video_id) {
        video_id = metadata.video_id;
        trailer->Play(video_id, true);
    } else if (!has_video) {
        trailer->Stop();
    }
}

void GameDetails::LayoutPanel() {
    // Most of the window on a big screen, never smaller than 1180x760 when it fits.
    const int w = std::min(width() - 96, std::max(1180, width() * 7 / 10));
    const int h = std::min(height() - 72, std::max(760, height() * 8 / 10));
    panel->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
}

void GameDetails::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    LayoutPanel();
}

void GameDetails::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(6, 7, 10, 200));
    const QRectF bounds = QRectF(panel->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);
    for (int layer = 18; layer > 0; layer -= 6) {
        QPainterPath shadow;
        shadow.addRoundedRect(bounds.adjusted(-layer, -layer + 8, layer, layer + 8), 20 + layer,
                              20 + layer);
        painter.fillPath(shadow, QColor(0, 0, 0, 22));
    }
    QPainterPath shape;
    shape.addRoundedRect(bounds, 20, 20);
    painter.fillPath(shape, QColor(0x19, 0x1a, 0x20));
    painter.setPen(QPen(Theme::Alpha(Qt::white, 16), 1));
    painter.drawPath(shape);
}

void GameDetails::mousePressEvent(QMouseEvent* event) {
    // A click beside the panel closes it.
    if (!panel->geometry().contains(event->position().toPoint())) {
        Close();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GameDetails::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
        Close();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace AfterHours
