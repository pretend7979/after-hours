// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include "afterhours/igdb_client.h"
#include "afterhours/metadata_dialog.h"
#include "afterhours/metadata_store.h"

namespace {
constexpr QSize CoverPreview{180, 255};
constexpr QSize ThumbnailSize{45, 64};
constexpr int IgdbIdRole = Qt::UserRole + 1;
constexpr int ImageIdRole = Qt::UserRole + 2;

/// Accepts a bare YouTube id or a youtube.com / youtu.be link.
QString VideoIdFromInput(const QString& input) {
    const QString text = input.trimmed();
    static const QRegularExpression link{
        QStringLiteral(R"((?:v=|youtu\.be/|embed/)([A-Za-z0-9_-]{6,}))")};
    if (const auto match = link.match(text); match.hasMatch()) {
        return match.captured(1);
    }
    return text;
}
} // Anonymous namespace

MetadataDialog::MetadataDialog(u64 program_id_, const QString& title_name,
                               GameMetadataStore& store_, IgdbClient& igdb_, QWidget* parent)
    : QDialog{parent}, program_id{program_id_}, store{store_}, igdb{igdb_} {
    setWindowTitle(tr("Edit Game Info"));
    setMinimumSize(820, 640);

    // Search IGDB and pick the right game.
    const GameMetadata current = store.Get(program_id);
    search = new QLineEdit(current.name.isEmpty() ? title_name : current.name);
    search->setPlaceholderText(tr("Game name"));
    auto* search_button = new QPushButton(tr("Search IGDB"));
    search_button->setDefault(true);
    auto* search_row = new QHBoxLayout;
    search_row->addWidget(search, 1);
    search_row->addWidget(search_button);

    results = new QListWidget;
    results->setIconSize(ThumbnailSize);
    results->setMinimumHeight(220);
    results->setUniformItemSizes(true);
    use_selected = new QPushButton(tr("Use Selected Game"));
    use_selected->setEnabled(false);
    status = new QLabel;
    status->setWordWrap(true);
    auto* result_row = new QHBoxLayout;
    result_row->addWidget(status, 1);
    result_row->addWidget(use_selected);

    const auto start_search = [this] {
        const QString query = search->text().trimmed();
        if (query.isEmpty()) {
            return;
        }
        pending_query = query;
        results->clear();
        use_selected->setEnabled(false);
        status->setText(tr("Searching..."));
        igdb.Search(query);
    };
    connect(search_button, &QPushButton::clicked, this, start_search);
    connect(search, &QLineEdit::returnPressed, this, start_search);
    connect(&igdb, &IgdbClient::SearchFinished, this, &MetadataDialog::ShowResults);
    connect(&igdb, &IgdbClient::ThumbnailReady, this,
            [this](const QString& image_id, const QByteArray& image) {
                QPixmap pixmap;
                if (!pixmap.loadFromData(image)) {
                    return;
                }
                for (int row = 0; row < results->count(); ++row) {
                    QListWidgetItem* item = results->item(row);
                    if (item->data(ImageIdRole).toString() == image_id) {
                        item->setIcon(pixmap.scaled(ThumbnailSize, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));
                    }
                }
            });
    connect(results, &QListWidget::currentRowChanged, this,
            [this](int row) { use_selected->setEnabled(row >= 0); });
    connect(results, &QListWidget::itemDoubleClicked, this, &MetadataDialog::UseSelected);
    connect(use_selected, &QPushButton::clicked, this, &MetadataDialog::UseSelected);
    connect(&igdb, &IgdbClient::Finished, this,
            [this](u64 finished_id, bool ok, const QString& message) {
                if (finished_id != program_id) {
                    return;
                }
                status->setText(ok ? tr("Using \"%1\".").arg(store.Get(program_id).name)
                                   : message);
                LoadFields();
            });

    // The details, editable by hand.
    cover = new QLabel;
    cover->setFixedSize(CoverPreview);
    cover->setAlignment(Qt::AlignCenter);
    cover->setStyleSheet(QStringLiteral("background:#11151f;border-radius:8px;color:#8891a5;"));
    auto* choose_cover = new QPushButton(tr("Choose Cover..."));
    connect(choose_cover, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose Cover"), {}, tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!path.isEmpty()) {
            store.SetCoverFromFile(program_id, path);
            UpdateCover();
        }
    });
    auto* cover_column = new QVBoxLayout;
    cover_column->addWidget(cover);
    cover_column->addWidget(choose_cover);
    cover_column->addStretch();

    name = new QLineEdit;
    year = new QSpinBox;
    year->setRange(0, 2100);
    year->setSpecialValueText(tr("Unknown"));
    developer = new QLineEdit;
    genres = new QLineEdit;
    genres->setPlaceholderText(tr("Comma separated"));
    video = new QLineEdit;
    video->setPlaceholderText(tr("YouTube link or id"));
    summary = new QPlainTextEdit;
    summary->setMinimumHeight(100);
    auto* fields = new QFormLayout;
    fields->addRow(tr("Name"), name);
    fields->addRow(tr("Release year"), year);
    fields->addRow(tr("Developer"), developer);
    fields->addRow(tr("Genres"), genres);
    fields->addRow(tr("Trailer"), video);
    fields->addRow(tr("Summary"), summary);
    auto* details = new QHBoxLayout;
    details->addLayout(cover_column);
    details->addLayout(fields, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        Save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(search_row);
    layout->addWidget(results);
    layout->addLayout(result_row);
    layout->addSpacing(8);
    layout->addLayout(details);
    layout->addWidget(buttons);

    if (!IgdbClient::HasServer()) {
        search_button->setEnabled(false);
        status->setText(tr("This build has no metadata server; details can be entered by hand."));
    }
    LoadFields();
}

void MetadataDialog::ShowResults(const QString& query, const std::vector<IgdbCandidate>& found,
                                 const QString& error) {
    // Searches come only from this dialog; IgdbClient cleans the query, so it isn't compared.
    if (pending_query.isEmpty()) {
        return;
    }
    results->clear();
    if (!error.isEmpty()) {
        status->setText(error);
        return;
    }
    status->setText(found.empty() ? tr("No games found.")
                                  : tr("%n result(s). Double-click one to use it.", "",
                                       static_cast<int>(found.size())));
    for (const IgdbCandidate& candidate : found) {
        QString label = candidate.name;
        if (candidate.year > 0) {
            label += QStringLiteral("  (%1)").arg(candidate.year);
        }
        if (!candidate.platforms.isEmpty()) {
            label += QStringLiteral("\n") + candidate.platforms;
        }
        auto* item = new QListWidgetItem(label, results);
        item->setData(IgdbIdRole, QVariant::fromValue<qulonglong>(candidate.id));
        item->setData(ImageIdRole, candidate.cover_image_id);
        item->setSizeHint(QSize{0, ThumbnailSize.height() + 8});
        if (!candidate.cover_image_id.isEmpty()) {
            igdb.FetchThumbnail(candidate.cover_image_id);
        }
    }
}

void MetadataDialog::UseSelected() {
    QListWidgetItem* item = results->currentItem();
    if (!item) {
        return;
    }
    status->setText(tr("Downloading details and art..."));
    igdb.Apply(program_id, item->data(IgdbIdRole).toULongLong());
}

void MetadataDialog::LoadFields() {
    const GameMetadata metadata = store.Get(program_id);
    name->setText(metadata.name);
    year->setValue(metadata.release_year);
    developer->setText(metadata.developer);
    genres->setText(metadata.genres.join(QStringLiteral(", ")));
    video->setText(metadata.video_id);
    summary->setPlainText(metadata.summary);
    UpdateCover();
}

void MetadataDialog::UpdateCover() {
    const QImage image = store.Cover(program_id);
    if (image.isNull()) {
        cover->setPixmap({});
        cover->setText(tr("No cover"));
        return;
    }
    cover->setPixmap(QPixmap::fromImage(image).scaled(CoverPreview, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
}

void MetadataDialog::Save() {
    GameMetadata metadata = store.Get(program_id);
    metadata.name = name->text().trimmed();
    metadata.release_year = year->value();
    metadata.developer = developer->text().trimmed();
    metadata.genres.clear();
    for (const QString& genre : genres->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        metadata.genres.append(genre.trimmed());
    }
    metadata.video_id = VideoIdFromInput(video->text());
    metadata.summary = summary->toPlainText().trimmed();
    metadata.user_edited = true;
    metadata.not_found = false;
    store.Set(program_id, metadata);
}
