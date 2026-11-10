// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

struct TrailerViewImpl;

/// Plays a YouTube trailer inside the window through the system's WebView2 (Edge) runtime, using
/// YouTube's own embedded player. Without WebView2 (other systems, or the runtime missing),
/// IsAvailable() is false and callers open the browser instead.
class TrailerView : public QWidget {
    Q_OBJECT

public:
    explicit TrailerView(QWidget* parent = nullptr);
    ~TrailerView() override;

    [[nodiscard]] static bool IsAvailable();

    /// Starts `video_id`; `muted` for a background preview.
    void Play(const QString& video_id, bool muted);
    void Stop();

signals:
    /// The web view could not be created; callers fall back to the browser.
    void Failed();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void EnsureCreated();
    void Navigate();

    TrailerViewImpl* impl{};
    QString video_id;
    bool muted{};
};
