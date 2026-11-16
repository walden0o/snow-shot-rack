// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackcontentmodel.h"

#include <QCryptographicHash>
#include <QMimeData>
#include <QUuid>
#include <algorithm>

namespace snow_shot::presentation {
bool clipboardExcludedFromHistory(const QMimeData& data) {
    const auto formats = data.formats();
    for (const QString& format : formats) {
        // Windows registered formats surface as application/x-qt-windows-mime;value="...".
        if (format.contains(QStringLiteral("ExcludeClipboardContentFromMonitorProcessing")))
            return true;
        if (format.contains(QStringLiteral("CanIncludeInClipboardHistory"))) {
            const QByteArray value = data.data(format);
            if (!value.isEmpty() && value.count('\0') == value.size())
                return true;
        }
        if (format == QStringLiteral("x-kde-passwordManagerHint") &&
            data.data(format) == QByteArrayLiteral("secret"))
            return true;
    }
    return false;
}

qint64 RackEntry::byteSize() const {
    return image.sizeInBytes() + text.size() * qint64(sizeof(QChar));
}

RackContentModel::RackContentModel(int limit, QObject* parent)
    : QObject(parent), m_limit(std::clamp(limit, 1, 500)) {}

const QVector<RackEntry>& RackContentModel::entries() const {
    return m_entries;
}

QVector<RackEntry> RackContentModel::search(const QString& query) const {
    QVector<RackEntry> result;
    for (const auto& entry : m_entries) {
        if (query.trimmed().isEmpty() || entry.text.contains(query.trimmed(), Qt::CaseInsensitive))
            result.push_back(entry);
    }
    return result;
}

bool RackContentModel::add(QString text, QImage image) {
    if (image.isNull() && text.trimmed().isEmpty())
        return false;
    if (text.size() > 1024 * 1024 || image.sizeInBytes() > MaximumEntryBytes ||
        qint64(image.width()) * image.height() * 4 > MaximumEntryBytes)
        return false;
    if (!image.isNull()) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
        image.setDevicePixelRatio(1.0);
        text.clear();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (image.isNull()) {
        hash.addData(text.toUtf8());
    } else {
        hash.addData(QByteArray::number(image.width()) + ':' + QByteArray::number(image.height()));
        for (int row = 0; row < image.height(); ++row)
            hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(row)),
                                        qsizetype(image.width()) * 4));
    }
    const QByteArray digest = hash.result();
    const auto duplicate = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& entry) {
        return entry.digest == digest && entry.image.isNull() == image.isNull();
    });
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (duplicate != m_entries.end()) {
        id = duplicate->id;
        m_entries.erase(duplicate);
    }
    m_entries.prepend(
        {id, std::move(text), std::move(image), digest, QDateTime::currentDateTimeUtc()});
    trim();
    emit changed();
    return true;
}

int RackContentModel::limit() const {
    return m_limit;
}

bool RackContentModel::contains(const QString& id) const {
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&](const auto& entry) { return entry.id == id; });
}

void RackContentModel::setLimit(int limit) {
    limit = std::clamp(limit, 1, 500);
    if (limit == m_limit)
        return;
    m_limit = limit;
    trim();
    emit changed();
}

void RackContentModel::trim() {
    qint64 bytes = 0;
    qsizetype keep = 0;
    while (keep < m_entries.size() && keep < m_limit) {
        bytes += m_entries[keep].byteSize();
        if (bytes > MaximumBytes)
            break;
        ++keep;
    }
    m_entries.resize(keep);
}

void RackContentModel::remove(const QString& id) {
    const auto removed = m_entries.removeIf([&](const auto& entry) { return entry.id == id; });
    if (removed)
        emit changed();
}

void RackContentModel::clear() {
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    emit changed();
}
} // namespace snow_shot::presentation
