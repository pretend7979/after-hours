// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QDialog>

#include "afterhours/types.h"

class GameMetadataStore;
class IgdbClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
struct IgdbCandidate;

/// Edits a title's library details and cover. "Search IGDB" lists matches; the chosen one replaces
/// the details, cover and key art.
class MetadataDialog : public QDialog {
    Q_OBJECT

public:
    MetadataDialog(u64 program_id, const QString& title_name, GameMetadataStore& store,
                   IgdbClient& igdb, QWidget* parent = nullptr);

private:
    void ShowResults(const QString& query, const std::vector<IgdbCandidate>& results,
                     const QString& error);
    void UseSelected();
    void LoadFields();
    void UpdateCover();
    void Save();

    u64 program_id;
    GameMetadataStore& store;
    IgdbClient& igdb;
    QString pending_query;

    QLabel* cover{};
    QLabel* status{};
    QLineEdit* search{};
    QListWidget* results{};
    QPushButton* use_selected{};
    QLineEdit* name{};
    QSpinBox* year{};
    QLineEdit* developer{};
    QLineEdit* genres{};
    QLineEdit* video{};
    QPlainTextEdit* summary{};
};
