// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "afterhours/paths.h"
#include "afterhours/metadata_store.h"

namespace {
// Covers are painted at most ~420 px tall; keeping them smaller saves memory for big libraries.
constexpr int MaxCoverHeight = 720;
} // Anonymous namespace

GameMetadataStore::GameMetadataStore(QObject* parent) : QObject{parent} {
    directory = AfterHours::Paths::ConfigDir() + QStringLiteral("/metadata");
    QDir{}.mkpath(directory);
}

QString GameMetadataStore::JsonPath(u64 program_id) const {
    return QStringLiteral("%1/%2.json").arg(directory).arg(program_id, 16, 16, QLatin1Char('0'));
}

QString GameMetadataStore::CoverPath(u64 program_id) const {
    return QStringLiteral("%1/%2_cover.jpg")
        .arg(directory)
        .arg(program_id, 16, 16, QLatin1Char('0'));
}

QString GameMetadataStore::HeroPath(u64 program_id) const {
    return QStringLiteral("%1/%2_hero.jpg")
        .arg(directory)
        .arg(program_id, 16, 16, QLatin1Char('0'));
}

QImage GameMetadataStore::Hero(u64 program_id) const {
    if (const auto it = heroes.find(program_id); it != heroes.end()) {
        return it->second;
    }
    QImage image{HeroPath(program_id)};
    heroes[program_id] = image;
    return image;
}

bool GameMetadataStore::HasHero(u64 program_id) const {
    return QFile::exists(HeroPath(program_id));
}

void GameMetadataStore::SetHero(u64 program_id, const QByteArray& encoded_image) {
    QImage image;
    if (!image.loadFromData(encoded_image)) {
        return;
    }
    if (image.width() > 1920) {
        image = image.scaledToWidth(1920, Qt::SmoothTransformation);
    }
    image.save(HeroPath(program_id), "JPG", 90);
    heroes.erase(program_id);
    emit Changed(program_id);
}

QImage GameMetadataStore::PlatformLogo(int platform) const {
    if (const auto it = logos.find(platform); it != logos.end()) {
        return it->second;
    }
    QImage image{QStringLiteral("%1/platform_%2.png").arg(directory).arg(platform)};
    logos[platform] = image;
    return image;
}

bool GameMetadataStore::HasPlatformLogo(int platform) const {
    return QFile::exists(QStringLiteral("%1/platform_%2.png").arg(directory).arg(platform));
}

void GameMetadataStore::SetPlatformLogo(int platform, const QByteArray& encoded_image) {
    QImage image;
    if (!image.loadFromData(encoded_image)) {
        return;
    }
    image.save(QStringLiteral("%1/platform_%2.png").arg(directory).arg(platform), "PNG");
    logos.erase(platform);
    emit PlatformLogoChanged(platform);
}

void GameMetadataStore::Load(u64 program_id) const {
    if (loaded[program_id]) {
        return;
    }
    loaded[program_id] = true;
    QFile file{JsonPath(program_id)};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    GameMetadata metadata;
    metadata.name = object.value(QStringLiteral("name")).toString();
    metadata.summary = object.value(QStringLiteral("summary")).toString();
    for (const auto& genre : object.value(QStringLiteral("genres")).toArray()) {
        metadata.genres.append(genre.toString());
    }
    metadata.developer = object.value(QStringLiteral("developer")).toString();
    metadata.release_year = object.value(QStringLiteral("release_year")).toInt();
    metadata.video_id = object.value(QStringLiteral("video_id")).toString();
    metadata.igdb_id = object.value(QStringLiteral("igdb_id")).toString().toULongLong();
    metadata.user_edited = object.value(QStringLiteral("user_edited")).toBool();
    metadata.not_found = object.value(QStringLiteral("not_found")).toBool();
    metadata.art_checked = object.value(QStringLiteral("art_checked")).toBool();
    entries[program_id] = std::move(metadata);
}

bool GameMetadataStore::Has(u64 program_id) const {
    Load(program_id);
    return entries.contains(program_id);
}

GameMetadata GameMetadataStore::Get(u64 program_id) const {
    Load(program_id);
    const auto it = entries.find(program_id);
    return it != entries.end() ? it->second : GameMetadata{};
}

void GameMetadataStore::Set(u64 program_id, const GameMetadata& metadata) {
    loaded[program_id] = true;
    entries[program_id] = metadata;
    QJsonArray genres;
    for (const auto& genre : metadata.genres) {
        genres.append(genre);
    }
    const QJsonObject object{
        {QStringLiteral("name"), metadata.name},
        {QStringLiteral("summary"), metadata.summary},
        {QStringLiteral("genres"), genres},
        {QStringLiteral("developer"), metadata.developer},
        {QStringLiteral("release_year"), metadata.release_year},
        {QStringLiteral("video_id"), metadata.video_id},
        {QStringLiteral("igdb_id"), QString::number(metadata.igdb_id)},
        {QStringLiteral("user_edited"), metadata.user_edited},
        {QStringLiteral("not_found"), metadata.not_found},
        {QStringLiteral("art_checked"), metadata.art_checked},
    };
    QFile file{JsonPath(program_id)};
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument{object}.toJson());
    }
    emit Changed(program_id);
}

QImage GameMetadataStore::Cover(u64 program_id) const {
    if (const auto it = covers.find(program_id); it != covers.end()) {
        return it->second;
    }
    QImage image{CoverPath(program_id)};
    if (!image.isNull() && image.height() > MaxCoverHeight) {
        image = image.scaledToHeight(MaxCoverHeight, Qt::SmoothTransformation);
    }
    covers[program_id] = image;
    return image;
}

void GameMetadataStore::SetCover(u64 program_id, const QByteArray& encoded_image) {
    QImage image;
    if (!image.loadFromData(encoded_image)) {
        return;
    }
    image.save(CoverPath(program_id), "JPG", 92);
    covers.erase(program_id);
    emit Changed(program_id);
}

void GameMetadataStore::SetCoverFromFile(u64 program_id, const QString& path) {
    QFile file{path};
    if (file.open(QIODevice::ReadOnly)) {
        SetCover(program_id, file.readAll());
    }
}
