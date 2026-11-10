// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "afterhours/emulator_downloader.h"
#include "afterhours/game_library.h"

namespace Library {

namespace {
/// Which release a downloaded emulator folder holds, for update checks. Folders downloaded
/// before the app was renamed have the legacy name.
const QString MarkerName = QStringLiteral(".afterhours-release.json");
const QString LegacyMarkerName = QStringLiteral(".pretendulator-release.json");

QNetworkRequest GitHubRequest(const QUrl& url) {
    QNetworkRequest request{url};
    // GitHub's API needs a user agent; this names the app, nothing about the user.
    request.setRawHeader("User-Agent", "AfterHours");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

struct FoundAsset {
    QString tag;
    QString url;
    QString name;
};

/// The newest release asset matching `pattern_text`. When none does (a fork naming its builds
/// differently), the newest Windows archive that isn't debug symbols or sources.
FoundAsset FindAsset(const QJsonArray& releases, const QString& pattern_text) {
    static const QRegularExpression windows{
        QStringLiteral("(windows|win64|win-x64|win_x64|msvc|mingw|x64).*\\.(zip|7z)$"),
        QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression unwanted{
        QStringLiteral("(pdb|debug|symbols|source|linux|macos|android|arm64)"),
        QRegularExpression::CaseInsensitiveOption};
    const QRegularExpression pattern{pattern_text, QRegularExpression::CaseInsensitiveOption};
    for (const bool fallback : {false, true}) {
        if (!fallback && pattern_text.isEmpty()) {
            continue;
        }
        for (const auto& release_value : releases) {
            const QJsonObject release = release_value.toObject();
            for (const auto& value : release.value(QStringLiteral("assets")).toArray()) {
                const QJsonObject asset = value.toObject();
                const QString name = asset.value(QStringLiteral("name")).toString();
                const bool match = fallback ? windows.match(name).hasMatch() &&
                                                  !unwanted.match(name).hasMatch()
                                            : pattern.match(name).hasMatch();
                if (match) {
                    return {release.value(QStringLiteral("tag_name")).toString(),
                            asset.value(QStringLiteral("browser_download_url")).toString(), name};
                }
            }
        }
    }
    return {};
}
} // Anonymous namespace

EmulatorDownloader::EmulatorDownloader(QObject* parent)
    : QObject{parent}, network{new QNetworkAccessManager(this)} {}

void EmulatorDownloader::Download(const EmulatorDef& emulator, const QString& folder) {
    if (busy) {
        return;
    }
    if (emulator.repo.isEmpty()) {
        emit Finished(emulator.id, false,
                      tr("%1 has no automatic download; get it from %2.")
                          .arg(emulator.name, emulator.homepage));
        return;
    }
    busy = true;
    emulator_id = emulator.id;
    repo = emulator.repo;
    target = QDir{folder}.filePath(emulator.id);
    asset_pattern = emulator.asset_pattern;
    const QUrl url{QStringLiteral("https://api.github.com/repos/%1/releases?per_page=5").arg(emulator.repo)};
    active = network->get(GitHubRequest(url));
    connect(active, &QNetworkReply::finished, this, [this, reply = active] {
        reply->deleteLater();
        active = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            Done(false, tr("Could not read releases: %1").arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray releases;
        if (doc.isArray()) {
            releases = doc.array();
        } else if (doc.isObject()) {
            releases.append(doc.object());
        }
        if (const FoundAsset found = FindAsset(releases, asset_pattern); !found.url.isEmpty()) {
            release_tag = found.tag;
            FetchAsset(found.url, found.name);
            return;
        }
        Done(false, tr("No Windows build was found in the recent releases of github.com/%1.")
                        .arg(repo));
    });
}

void EmulatorDownloader::FetchAsset(const QString& url, const QString& name) {
    const QString archive =
        QDir{QStandardPaths::writableLocation(QStandardPaths::TempLocation)}.filePath(
            QStringLiteral("afterhours-") + name);
    auto* file = new QFile(archive, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        Done(false, tr("Could not write %1.").arg(archive));
        return;
    }
    active = network->get(GitHubRequest(QUrl{url}));
    connect(active, &QNetworkReply::readyRead, this,
            [this, file, reply = active] { file->write(reply->readAll()); });
    connect(active, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit Progress(emulator_id, received, total); });
    connect(active, &QNetworkReply::finished, this, [this, file, archive, reply = active] {
        file->write(reply->readAll());
        file->close();
        file->deleteLater();
        reply->deleteLater();
        active = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(archive);
            Done(false, tr("Download failed: %1").arg(reply->errorString()));
            return;
        }
        Extract(archive);
    });
}

void EmulatorDownloader::Extract(const QString& archive) {
    QDir{}.mkpath(target);
    // Windows' bsdtar unpacks zip and 7z.
#ifdef _WIN32
    const QString tar = QStringLiteral("C:/Windows/System32/tar.exe");
#else
    const QString tar = QStringLiteral("tar");
#endif
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, archive](int code, QProcess::ExitStatus status) {
                process->deleteLater();
                QFile::remove(archive);
                if (status != QProcess::NormalExit || code != 0) {
                    Done(false, tr("Could not unpack the download."));
                    return;
                }
                // Which release this is, for update checks.
                QFile::remove(QDir{target}.filePath(LegacyMarkerName));
                QFile marker{QDir{target}.filePath(MarkerName)};
                if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    marker.write(QJsonDocument{QJsonObject{
                                                   {QStringLiteral("repo"), repo},
                                                   {QStringLiteral("tag"), release_tag},
                                               }}
                                     .toJson());
                }
                Done(true, tr("Installed %1 in %2.")
                               .arg(release_tag, QDir::toNativeSeparators(target)));
            });
    process->start(tar, {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target});
}

void EmulatorDownloader::CheckLatest(const EmulatorDef& emulator) {
    if (emulator.repo.isEmpty()) {
        emit LatestRelease(emulator.id, {});
        return;
    }
    const QUrl url{
        QStringLiteral("https://api.github.com/repos/%1/releases?per_page=5").arg(emulator.repo)};
    QNetworkReply* reply = network->get(GitHubRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, id = emulator.id, pattern_text = emulator.asset_pattern] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    emit LatestRelease(id, {});
                    return;
                }
                emit LatestRelease(
                    id, FindAsset(QJsonDocument::fromJson(reply->readAll()).array(), pattern_text)
                            .tag);
            });
}

QString EmulatorDownloader::InstalledRelease(const QString& folder) {
    const QString path = ReleaseMarker(folder);
    QFile marker{path};
    if (path.isEmpty() || !marker.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(marker.readAll()).object().value(QStringLiteral("tag")).toString();
}

QString EmulatorDownloader::ReleaseMarker(const QString& folder) {
    for (const QString& name : {MarkerName, LegacyMarkerName}) {
        if (const QString path = QDir{folder}.filePath(name); QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

void EmulatorDownloader::Cancel() {
    if (active) {
        active->abort();
    }
}

void EmulatorDownloader::Done(bool ok, const QString& message) {
    busy = false;
    emit Finished(emulator_id, ok, message);
}

} // namespace Library
