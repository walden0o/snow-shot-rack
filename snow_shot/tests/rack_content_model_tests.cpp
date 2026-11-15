// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackcontentmodel.h"
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

QImage solidImage(QColor color, int size = 8) {
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

void addsNewestFirstAndDeduplicates() {
    using snow_shot::presentation::RackContentModel;
    RackContentModel model(10);
    QSignalSpy changed(&model, &RackContentModel::changed);
    require(model.add(QStringLiteral("alpha")), "text entries are accepted");
    require(model.add({}, solidImage(Qt::red)), "image entries are accepted");
    require(model.entries().size() == 2 && !model.entries().first().image.isNull(),
            "the newest entry is listed first");
    require(model.add(QStringLiteral("alpha")) && model.entries().size() == 2 &&
                model.entries().first().text == QStringLiteral("alpha"),
            "re-adding identical content moves it to the front without duplicating it");
    require(!model.add(QString()), "empty content is ignored");
    require(changed.count() == 3, "every accepted add notifies the view once");
    require(model.contains(model.entries().first().id), "entries are addressable by id");
}

void enforcesLimits() {
    using snow_shot::presentation::RackContentModel;
    RackContentModel model(3);
    for (int index = 0; index < 5; ++index)
        model.add(QString::number(index));
    require(model.entries().size() == 3 && model.entries().first().text == QStringLiteral("4") &&
                model.entries().last().text == QStringLiteral("2"),
            "the oldest entries are dropped beyond the count limit");
    model.setLimit(1);
    require(model.limit() == 1 && model.entries().size() == 1, "lowering the limit trims entries");
    model.setLimit(0);
    require(model.limit() == 1, "the limit never drops below one entry");
    require(!model.add({}, solidImage(Qt::blue, 4000)),
            "oversized images are rejected instead of exhausting memory");
}

void searchesRemovesAndClears() {
    using snow_shot::presentation::RackContentModel;
    RackContentModel model(10);
    model.add(QStringLiteral("Hello Rack"));
    model.add(QStringLiteral("clipboard history"));
    model.add({}, solidImage(Qt::green));
    require(model.search(QStringLiteral("rack")).size() == 1,
            "search matches text case-insensitively");
    require(model.search({}).size() == 3, "an empty query lists everything");
    const QString id = model.entries().first().id;
    model.remove(id);
    require(!model.contains(id) && model.entries().size() == 2, "remove deletes a single entry");
    model.clear();
    require(model.entries().isEmpty(), "clear empties the session history");
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    addsNewestFirstAndDeduplicates();
    enforcesLimits();
    searchesRemovesAndClears();
    return 0;
}
