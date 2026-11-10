// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <functional>
#include <vector>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "afterhours/types.h"

class GameMetadataStore;
class QNetworkAccessManager;
class QNetworkReply;

/// One IGDB search result offered to the user.
struct IgdbCandidate {
    u64 id{};
    QString name;
    int year{};
    QString platforms;
    QString cover_image_id;
};

/// Looks titles up through the After Hours metadata API (AFTERHOURS_METADATA_URL), which asks
/// IGDB and keeps the answers, and saves what it finds, cover and key art included, in the metadata
/// store. Each title is looked up once; afterwards everything comes from the store. Requests run
/// one at a time, in order.
class IgdbClient : public QObject {
    Q_OBJECT

public:
    IgdbClient(GameMetadataStore& store, QObject* parent = nullptr);

    /// Whether this build has a metadata API address.
    [[nodiscard]] static bool HasServer();
    /// Whether a title has no details yet, or was looked up before key art was fetched.
    [[nodiscard]] static bool NeedsLookup(const GameMetadataStore& store, u64 program_id);

    /// Looks `search_name` up and keeps the newest close match.
    void Request(u64 program_id, const QString& search_name,
                 std::vector<int> platforms = {508, 130});
    /// Downloads the platforms' logos into the store (PlatformLogo).
    void FetchPlatformLogos(const std::vector<int>& platforms);
    /// Lists matches for `query` (SearchFinished).
    void Search(const QString& query);
    /// Saves the chosen IGDB game for a title.
    void Apply(u64 program_id, u64 igdb_id);
    /// Downloads a small cover for a search result (ThumbnailReady).
    void FetchThumbnail(const QString& image_id);

signals:
    /// A lookup or Apply ended; `message` says what went wrong when `ok` is false.
    void Finished(u64 program_id, bool ok, const QString& message);
    void SearchFinished(const QString& query, const std::vector<IgdbCandidate>& results,
                        const QString& error);
    void ThumbnailReady(const QString& image_id, const QByteArray& image);

private:
    enum class Kind { Lookup, Search, Apply, PlatformLogos };
    struct Job {
        Kind kind;
        u64 program_id{};
        QString text;
        u64 igdb_id{};
        /// Lookups prefer these platforms' releases; the server falls back to every platform.
        std::vector<int> platforms;
    };

    void Enqueue(Job job);
    void Next();
    void Run();
    void OnReply(QNetworkReply* reply);
    void Save(const QJsonObject& game, bool chosen);
    void FetchImage(const QString& size, const QString& image_id,
                    std::function<void(const QByteArray&)> done);
    void FetchCover(const QString& image_id);
    void FetchHero();
    void FetchLogos(std::vector<std::pair<int, QString>> logos);
    void Finish(bool ok, const QString& message);

    GameMetadataStore& store;
    QNetworkAccessManager* network;
    std::deque<Job> jobs;
    bool busy{};
    QString hero_image_id;
};
