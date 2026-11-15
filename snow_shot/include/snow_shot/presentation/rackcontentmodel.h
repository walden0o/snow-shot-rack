// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PRESENTATION_RACKCONTENTMODEL_H
#define SNOW_SHOT_PRESENTATION_RACKCONTENTMODEL_H

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QVector>

namespace snow_shot::presentation {
struct RackEntry {
    QString id;
    QString text;
    QImage image;
    QByteArray digest;
    QDateTime createdUtc;

    [[nodiscard]] qint64 byteSize() const;
};

class RackContentModel final : public QObject {
    Q_OBJECT
  public:
    static constexpr qint64 MaximumEntryBytes = 32 * 1024 * 1024;
    static constexpr qint64 MaximumBytes = 128 * 1024 * 1024;
    explicit RackContentModel(int limit = 100, QObject* parent = nullptr);
    [[nodiscard]] const QVector<RackEntry>& entries() const;
    [[nodiscard]] QVector<RackEntry> search(const QString& query) const;
    [[nodiscard]] int limit() const;
    [[nodiscard]] bool contains(const QString& id) const;
    bool add(QString text, QImage image = {});
    void setLimit(int limit);
    void remove(const QString& id);
    void clear();

  signals:
    void changed();

  private:
    void trim();
    QVector<RackEntry> m_entries;
    int m_limit;
};
} // namespace snow_shot::presentation
#endif
