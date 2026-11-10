// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace Library {

struct EmulatorDef;

/// Downloads an emulator's latest Windows release from its official GitHub repository and unpacks
/// it into <folder>/<emulator id>. One download at a time.
class EmulatorDownloader : public QObject {
    Q_OBJECT

public:
    explicit EmulatorDownloader(QObject* parent = nullptr);

    [[nodiscard]] bool Busy() const {
        return busy;
    }
    void Download(const EmulatorDef& emulator, const QString& folder);
    void Cancel();
    /// Looks up the newest release with a matching Windows build (LatestRelease), without
    /// downloading. Runs alongside a download.
    void CheckLatest(const EmulatorDef& emulator);

    /// The release a download into `folder` installed (empty when unknown: not downloaded here).
    [[nodiscard]] static QString InstalledRelease(const QString& folder);
    /// The release marker in `folder` (also the name used before the rename); empty when none.
    [[nodiscard]] static QString ReleaseMarker(const QString& folder);

signals:
    void Progress(const QString& emulator_id, qint64 received, qint64 total);
    void Finished(const QString& emulator_id, bool ok, const QString& message);
    /// `tag` is empty when the check failed or no release has a Windows build.
    void LatestRelease(const QString& emulator_id, const QString& tag);

private:
    void FetchAsset(const QString& url, const QString& name);
    void Extract(const QString& archive);
    void Done(bool ok, const QString& message);

    QNetworkAccessManager* network;
    QNetworkReply* active{};
    QString emulator_id;
    QString target;
    QString asset_pattern;
    QString release_tag;
    QString repo;
    bool busy{};
};

} // namespace Library
