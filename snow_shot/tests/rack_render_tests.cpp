// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/presentation/components/racklist_p.h"
#include <QApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void rendersClothesline(bool dark) {
    snow_shot::presentation::detail::RackList list;
    QPalette colors = list.palette();
    const QColor background = dark ? QColor(20, 20, 20) : QColor(Qt::white);
    colors.setColor(QPalette::Base, background);
    colors.setColor(QPalette::Dark, dark ? QColor(200, 200, 200) : QColor(70, 70, 70));
    list.setPalette(colors);
    list.setFrameShape(QFrame::NoFrame);
    list.setViewMode(QListView::IconMode);
    list.setFlow(QListView::LeftToRight);
    list.setWrapping(false);
    list.setGridSize(QSize(168, 140));
    list.setSpacing(4);
    list.resize(600, 180);
    list.addItem(QStringLiteral("01:00:00"));
    list.addItem(QStringLiteral("02:00:00"));
    list.show();
    QApplication::processEvents();

    // Sample the otherwise empty right-hand end of the row, beyond both cards.
    const QPoint linePoint(list.viewport()->width() - 30,
                           list.visualItemRect(list.item(0)).top() + 3);
    const auto renderedPixel = [&list, linePoint] {
        QImage image(list.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        list.viewport()->render(&image);
        return image.pixelColor(linePoint);
    };
    const auto contrast = [background](const QColor& color) {
        return std::abs(color.red() - background.red()) +
               std::abs(color.green() - background.green()) +
               std::abs(color.blue() - background.blue());
    };
    require(contrast(renderedPixel()) > 120,
            "clothesline must remain visible over the list viewport background");

    list.setViewMode(QListView::ListMode);
    QApplication::processEvents();
    require(contrast(renderedPixel()) < 10, "clipboard list must not draw a clothesline");
    list.setViewMode(QListView::IconMode);
    list.clear();
    QApplication::processEvents();
    require(contrast(renderedPixel()) < 10, "empty Rack must not draw a clothesline");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    rendersClothesline(false);
    rendersClothesline(true);
    return 0;
}
