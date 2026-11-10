// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unordered_map>

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include "afterhours/types.h"

/// Library details shown by the console view: fetched from IGDB or entered by the user.
struct GameMetadata {
    QString name;
    QString summary;
    QStringList genres;
    QString developer;
    int release_year{};
    /// YouTube id of the first IGDB video, empty without one.
    QString video_id;
    u64 igdb_id{};
    /// True once the user saved the entry: automatic lookups leave it alone.
    bool user_edited{};
    /// IGDB found nothing for this title (lookups are not retried every start).
    bool not_found{};
    /// The lookup that saved this entry also looked for key art (none may exist).
    bool art_checked{};
};

/// Keeps one JSON file and one cover image per title in <config>/metadata. Lives on the GUI
/// thread; loaded covers are cached as scaled-down images for painting.
class GameMetadataStore : public QObject {
    Q_OBJECT

public:
    explicit GameMetadataStore(QObject* parent = nullptr);

    [[nodiscard]] bool Has(u64 program_id) const;
    [[nodiscard]] GameMetadata Get(u64 program_id) const;
    void Set(u64 program_id, const GameMetadata& metadata);

    /// The stored cover, or a null image. Cached after the first load.
    [[nodiscard]] QImage Cover(u64 program_id) const;
    void SetCover(u64 program_id, const QByteArray& encoded_image);
    void SetCoverFromFile(u64 program_id, const QString& path);

    /// Wide key art for the console view's background, or a null image.
    [[nodiscard]] QImage Hero(u64 program_id) const;
    [[nodiscard]] bool HasHero(u64 program_id) const;
    void SetHero(u64 program_id, const QByteArray& encoded_image);

    /// An IGDB platform's logo, or a null image.
    [[nodiscard]] QImage PlatformLogo(int platform) const;
    [[nodiscard]] bool HasPlatformLogo(int platform) const;
    void SetPlatformLogo(int platform, const QByteArray& encoded_image);

signals:
    void PlatformLogoChanged(int platform);

    void Changed(u64 program_id);

private:
    [[nodiscard]] QString JsonPath(u64 program_id) const;
    [[nodiscard]] QString CoverPath(u64 program_id) const;
    [[nodiscard]] QString HeroPath(u64 program_id) const;
    void Load(u64 program_id) const;

    QString directory;
    mutable std::unordered_map<u64, GameMetadata> entries;
    mutable std::unordered_map<u64, bool> loaded;
    mutable std::unordered_map<u64, QImage> covers;
    mutable std::unordered_map<u64, QImage> heroes;
    mutable std::unordered_map<int, QImage> logos;
};
