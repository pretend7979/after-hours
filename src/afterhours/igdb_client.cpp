// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include "afterhours/igdb_client.h"
#include "afterhours/metadata_store.h"

namespace {
/// The metadata API (server/pretendulator_api.py); it holds the IGDB account.
const QString ServerUrl = QStringLiteral(AFTERHOURS_METADATA_URL);

QUrl ApiUrl(const QString& path, const QUrlQuery& query = {}) {
    QUrl url{ServerUrl + path};
    url.setQuery(query);
    return url;
}

QNetworkRequest ApiRequest(const QUrl& url) {
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("AfterHours/%1").arg(QStringLiteral(AFTERHOURS_VERSION)));
    request.setTransferTimeout(60000);
    return request;
}

/// Title names from the NACP carry marks IGDB doesn't index ("Zelda™").
QString CleanName(QString name) {
    name.remove(QRegularExpression(QStringLiteral("[™®©]")));
    name.replace(QLatin1Char('"'), QLatin1Char(' '));
    return name.simplified();
}

QString JoinIds(const std::vector<int>& ids) {
    QStringList parts;
    for (const int id : ids) {
        parts.append(QString::number(id));
    }
    return parts.join(QLatin1Char(','));
}
} // Anonymous namespace

IgdbClient::IgdbClient(GameMetadataStore& store_, QObject* parent)
    : QObject{parent}, store{store_}, network{new QNetworkAccessManager(this)} {}

bool IgdbClient::HasServer() {
    return !ServerUrl.isEmpty();
}

bool IgdbClient::NeedsLookup(const GameMetadataStore& store, u64 program_id) {
    if (!store.Has(program_id)) {
        return true;
    }
    // Entries from before key art was fetched get it once; edited ones are left alone.
    const GameMetadata metadata = store.Get(program_id);
    return !metadata.user_edited && !metadata.not_found && !metadata.art_checked &&
           !store.HasHero(program_id);
}

void IgdbClient::FetchPlatformLogos(const std::vector<int>& platforms) {
    std::vector<int> missing;
    for (const int platform : platforms) {
        if (!store.HasPlatformLogo(platform)) {
            missing.push_back(platform);
        }
    }
    if (!missing.empty()) {
        Enqueue(Job{.kind = Kind::PlatformLogos, .platforms = std::move(missing)});
    }
}

void IgdbClient::Request(u64 program_id, const QString& search_name, std::vector<int> platforms) {
    for (const Job& job : jobs) {
        if (job.kind == Kind::Lookup && job.program_id == program_id) {
            return;
        }
    }
    Enqueue(Job{.kind = Kind::Lookup,
                .program_id = program_id,
                .text = CleanName(search_name),
                .platforms = std::move(platforms)});
}

void IgdbClient::Search(const QString& query) {
    Enqueue(Job{.kind = Kind::Search, .text = CleanName(query)});
}

void IgdbClient::Apply(u64 program_id, u64 igdb_id) {
    Enqueue(Job{.kind = Kind::Apply, .program_id = program_id, .igdb_id = igdb_id});
}

void IgdbClient::FetchThumbnail(const QString& image_id) {
    // Thumbnails don't wait for the queue.
    FetchImage(QStringLiteral("t_cover_small"), image_id, [this, image_id](const QByteArray& data) {
        if (!data.isEmpty()) {
            emit ThumbnailReady(image_id, data);
        }
    });
}

void IgdbClient::FetchImage(const QString& size, const QString& image_id,
                            std::function<void(const QByteArray&)> done) {
    const QString extension = size == QStringLiteral("t_logo_med") ? QStringLiteral("png")
                                                                    : QStringLiteral("jpg");
    QNetworkReply* reply = network->get(
        ApiRequest(ApiUrl(QStringLiteral("/img/%1/%2.%3").arg(size, image_id, extension))));
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{});
    });
}

void IgdbClient::Enqueue(Job job) {
    if (!HasServer()) {
        if (job.kind == Kind::Search) {
            emit SearchFinished(job.text, {}, tr("This build has no metadata server."));
        } else {
            emit Finished(job.program_id, false, tr("This build has no metadata server."));
        }
        return;
    }
    jobs.push_back(std::move(job));
    Next();
}

void IgdbClient::Next() {
    if (busy || jobs.empty()) {
        return;
    }
    busy = true;
    Run();
}

void IgdbClient::Run() {
    const Job& job = jobs.front();
    QUrl url;
    QUrlQuery query;
    switch (job.kind) {
    case Kind::Lookup:
        // The title's platforms first (508 and 130 are Switch 2 and Switch). Without the filter a
        // name like "Star Fox" matches the same name on other systems first.
        query.addQueryItem(QStringLiteral("name"), job.text);
        if (!job.platforms.empty()) {
            query.addQueryItem(QStringLiteral("platforms"), JoinIds(job.platforms));
        }
        url = ApiUrl(QStringLiteral("/v1/lookup"), query);
        break;
    case Kind::PlatformLogos:
        query.addQueryItem(QStringLiteral("ids"), JoinIds(job.platforms));
        url = ApiUrl(QStringLiteral("/v1/platform-logos"), query);
        break;
    case Kind::Search:
        query.addQueryItem(QStringLiteral("q"), job.text);
        url = ApiUrl(QStringLiteral("/v1/search"), query);
        break;
    case Kind::Apply:
        url = ApiUrl(QStringLiteral("/v1/game/%1").arg(job.igdb_id));
        break;
    }
    QNetworkReply* reply = network->get(ApiRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] { OnReply(reply); });
}

void IgdbClient::OnReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        Finish(false, tr("Game details are unavailable: %1").arg(reply->errorString()));
        return;
    }
    Job& job = jobs.front();
    const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
    switch (job.kind) {
    case Kind::Search: {
        std::vector<IgdbCandidate> candidates;
        for (const auto& value : answer.value(QStringLiteral("results")).toArray()) {
            const QJsonObject game = value.toObject();
            candidates.push_back(IgdbCandidate{
                .id = static_cast<u64>(game.value(QStringLiteral("id")).toDouble()),
                .name = game.value(QStringLiteral("name")).toString(),
                .year = game.value(QStringLiteral("year")).toInt(),
                .platforms = game.value(QStringLiteral("platforms")).toString(),
                .cover_image_id = game.value(QStringLiteral("cover")).toString(),
            });
        }
        const QString query = job.text;
        jobs.pop_front();
        busy = false;
        emit SearchFinished(query, candidates, {});
        Next();
        return;
    }
    case Kind::Lookup: {
        const QJsonObject game = answer.value(QStringLiteral("game")).toObject();
        if (game.isEmpty()) {
            GameMetadata metadata;
            metadata.name = job.text;
            metadata.not_found = true;
            store.Set(job.program_id, metadata);
            Finish(false, tr("IGDB has no match for \"%1\".").arg(job.text));
            return;
        }
        Save(game, false);
        return;
    }
    case Kind::PlatformLogos: {
        std::vector<std::pair<int, QString>> logos;
        const QJsonObject found = answer.value(QStringLiteral("logos")).toObject();
        for (auto it = found.begin(); it != found.end(); ++it) {
            logos.emplace_back(it.key().toInt(), it.value().toString());
        }
        FetchLogos(std::move(logos));
        return;
    }
    case Kind::Apply: {
        const QJsonObject game = answer.value(QStringLiteral("game")).toObject();
        if (game.isEmpty()) {
            Finish(false, tr("IGDB no longer has that game."));
            return;
        }
        Save(game, true);
        return;
    }
    }
}

void IgdbClient::Save(const QJsonObject& game, bool chosen) {
    const u64 program_id = jobs.front().program_id;
    GameMetadata metadata;
    metadata.igdb_id = static_cast<u64>(game.value(QStringLiteral("id")).toDouble());
    metadata.name = game.value(QStringLiteral("name")).toString();
    metadata.summary = game.value(QStringLiteral("summary")).toString();
    metadata.release_year = game.value(QStringLiteral("year")).toInt();
    for (const auto& genre : game.value(QStringLiteral("genres")).toArray()) {
        metadata.genres.append(genre.toString());
    }
    metadata.developer = game.value(QStringLiteral("developer")).toString();
    metadata.video_id = game.value(QStringLiteral("video_id")).toString();
    // A game the user picked stays, whatever later lookups would find.
    metadata.user_edited = chosen;
    metadata.art_checked = true;
    store.Set(program_id, metadata);

    // Key art for the background: an artwork, else a screenshot (the server picks).
    hero_image_id = game.value(QStringLiteral("hero")).toString();
    const QString cover_id = game.value(QStringLiteral("cover")).toString();
    if (cover_id.isEmpty()) {
        FetchHero();
        return;
    }
    FetchCover(cover_id);
}

void IgdbClient::FetchCover(const QString& image_id) {
    FetchImage(QStringLiteral("t_cover_big_2x"), image_id, [this](const QByteArray& data) {
        if (!data.isEmpty()) {
            store.SetCover(jobs.front().program_id, data);
        }
        FetchHero();
    });
}

void IgdbClient::FetchHero() {
    if (hero_image_id.isEmpty()) {
        Finish(true, {});
        return;
    }
    FetchImage(QStringLiteral("t_1080p"), hero_image_id, [this](const QByteArray& data) {
        if (!data.isEmpty()) {
            store.SetHero(jobs.front().program_id, data);
        }
        Finish(true, {});
    });
}

void IgdbClient::FetchLogos(std::vector<std::pair<int, QString>> logos) {
    if (logos.empty()) {
        Finish(true, {});
        return;
    }
    const auto [platform, image] = logos.back();
    logos.pop_back();
    FetchImage(QStringLiteral("t_logo_med"), image,
               [this, platform, logos = std::move(logos)](const QByteArray& data) mutable {
                   if (!data.isEmpty()) {
                       store.SetPlatformLogo(platform, data);
                   }
                   FetchLogos(std::move(logos));
               });
}

void IgdbClient::Finish(bool ok, const QString& message) {
    const Job job = jobs.front();
    jobs.pop_front();
    busy = false;
    if (job.kind == Kind::Search) {
        emit SearchFinished(job.text, {}, message);
    } else {
        emit Finished(job.program_id, ok, message);
    }
    Next();
}
