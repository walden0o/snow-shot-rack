// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QListWidget>
#include <QPainter>
#include <functional>

namespace snow_shot::presentation::detail {
inline constexpr int RackRopeOffset = 3;

class RackList final : public QListWidget {
  public:
    using QListWidget::QListWidget;
    std::function<void(const QString&)> drag;

  protected:
    void paintEvent(QPaintEvent* event) override {
        QListWidget::paintEvent(event);
        if (viewMode() != QListView::IconMode || count() == 0)
            return;

        // Paint on the viewport: its opaque background covers the parent window.
        // Use the same viewport coordinates as the card delegate's strings.
        const qreal top = visualItemRect(item(0)).top() + RackRopeOffset;
        const qreal left = 14;
        const qreal right = viewport()->width() - 14;
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Dark), 1.4));
        painter.drawLine(QPointF(left, top), QPointF(right, top));
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Dark));
        painter.drawEllipse(QPointF(left, top), 2.6, 2.6);
        painter.drawEllipse(QPointF(right, top), 2.6, 2.6);
    }

    void startDrag(Qt::DropActions) override {
        if (currentItem() && drag)
            drag(currentItem()->data(Qt::UserRole).toString());
    }
};
} // namespace snow_shot::presentation::detail
