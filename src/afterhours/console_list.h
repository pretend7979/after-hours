// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <vector>

#include <QImage>
#include <QListView>
#include <QStyledItemDelegate>

class QStandardItemModel;

namespace AfterHours {

/// One sidebar entry: a console with games, or "All games" (empty id).
struct ConsoleEntry {
    QString id;
    QString name;
    QString maker;
    QString logo_file;
    int count{};
};

class ConsoleListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void ClearLogos();

private:
    [[nodiscard]] QImage Logo(const QString& file, QSize size, qreal ratio) const;
    mutable std::map<QString, QImage> logos;
};

/// The sidebar: "All games", then each console with games, grouped by maker.
class ConsoleList : public QListView {
    Q_OBJECT

public:
    explicit ConsoleList(QWidget* parent = nullptr);

    void SetConsoles(const std::vector<ConsoleEntry>& consoles, int total);
    void Select(const QString& system_id);
    [[nodiscard]] QString Selected() const;

signals:
    /// A console was chosen; empty for all games.
    void ConsoleChosen(const QString& system_id);

protected:
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;

private:
    QStandardItemModel* model;
    ConsoleListDelegate* delegate;
    bool filling{};
};

} // namespace AfterHours
