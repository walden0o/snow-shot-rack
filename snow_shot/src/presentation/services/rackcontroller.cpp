// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackcontroller.h"
#include "snow_shot/platform/screenshotnative.h"
#include "snow_shot/presentation/rackcontentmodel.h"
#include "snow_shot/presentation/rackwindow.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/capturehistoryrepository.h"
#include "snow_shot/storage/configurationstore.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QQueue>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>
#include <chrono>
#include <future>

namespace snow_shot::presentation {
namespace {
int configuredLimit(const QString& key, int fallback) {
    bool valid = false;
    const int value =
        storage::ApplicationStorage::instance().configuration().value(key).toString().toInt(&valid);
    return valid && value > 0 ? value : fallback;
}
} // namespace

class RackController::Impl {
  public:
    explicit Impl(RackController& owner) : q(owner), model(24), clipboardModel(50) {
        completion.setInterval(30);
        QObject::connect(&completion, &QTimer::timeout, &q, [this] { finishLoad(); });
        // Qt reports pasteboard changes on macOS only after activation; poll the cheap counter.
        clipboardPoll.setInterval(500);
        QObject::connect(&clipboardPoll, &QTimer::timeout, &q, [this] { captureClipboard(); });
    }

    void start() {
        if (started)
            return;
        started = true;
        auto& storage = storage::ApplicationStorage::instance();
        window = std::make_unique<RackWindow>(model, clipboardModel);
        QObject::connect(window.get(), &RackWindow::settingsRequested, &q,
                         &RackController::settingsRequested);
        QObject::connect(window.get(), &RackWindow::copyRequested, &q,
                         [this](const RackEntry& entry) { copy(entry); });
        QObject::connect(&storage, &storage::ApplicationStorage::captureHistoryChanged, &q,
                         [this] { collectHistory(); });
        QObject::connect(&storage.configuration(), &storage::ConfigurationStore::valueChanged, &q,
                         [this](const QString& key) {
                             if (key.startsWith(QStringLiteral("rack/")) ||
                                 key.startsWith(QStringLiteral("clipboard_history/")))
                                 refreshConfiguration();
                         });
        QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, &q,
                         [this] { captureClipboard(); });
        for (const auto& record : storage.captureHistory().records())
            seen.insert(record.id);
        refreshConfiguration();
    }

    void refreshConfiguration() {
        const auto& config = storage::ApplicationStorage::instance().configuration();
        enabled = config.value(QStringLiteral("rack/enabled")).toBool();
        automatic = config.value(QStringLiteral("rack/auto_add_screenshots")).toBool();
        model.setLimit(configuredLimit(QStringLiteral("rack/limit"), 24));
        clipboardModel.setLimit(configuredLimit(QStringLiteral("clipboard_history/limit"), 50));
        const bool recordClipboard =
            config.value(QStringLiteral("clipboard_history/enabled")).toBool();
        if (recordClipboard != clipboardEnabled) {
            clipboardEnabled = recordClipboard;
            // Session-only history: disabling discards everything that was recorded.
            clipboardModel.clear();
#ifdef Q_OS_MACOS
            lastChangeCount = platform::clipboardChangeCount();
            if (clipboardEnabled)
                clipboardPoll.start();
            else
                clipboardPoll.stop();
#endif
        }
        if (!enabled || !automatic) {
            queue.clear();
            ++generation;
        }
        if (window) {
            window->setClipboardHistoryEnabled(clipboardEnabled);
            window->setDisplayMode(config.value(QStringLiteral("rack/display_mode")).toString());
            if (enabled) {
                window->show();
                window->setCaptureActive(QStringLiteral("configuration"), false);
            } else {
                window->hide();
            }
        }
    }

    void copy(const RackEntry& entry) {
        {
            const QScopedValueRollback guard(writingClipboard, true);
            if (entry.image.isNull())
                QApplication::clipboard()->setText(entry.text);
            else
                QApplication::clipboard()->setImage(entry.image);
        }
#ifdef Q_OS_MACOS
        lastChangeCount = platform::clipboardChangeCount();
#endif
        // Copying a history item moves it to the front instead of recording it again.
        if (clipboardEnabled && clipboardModel.contains(entry.id))
            clipboardModel.add(entry.text, entry.image);
    }

    void captureClipboard() {
        if (!clipboardEnabled || writingClipboard)
            return;
#ifdef Q_OS_MACOS
        const qint64 count = platform::clipboardChangeCount();
        if (count == lastChangeCount)
            return;
        lastChangeCount = count;
        if (platform::clipboardContentConcealed())
            return;
#endif
        const QMimeData* data = QApplication::clipboard()->mimeData();
        if (data == nullptr)
            return;
        if (data->hasImage())
            clipboardModel.add({}, qvariant_cast<QImage>(data->imageData()));
        else if (data->hasText())
            clipboardModel.add(data->text());
    }

    void collectHistory() {
        auto& storage = storage::ApplicationStorage::instance();
        const auto records = storage.captureHistory().records();
        QSet<QString> current;
        // Repository order is newest first; process a burst in chronological order.
        for (auto it = records.crbegin(); it != records.crend(); ++it) {
            current.insert(it->id);
            if (enabled && automatic && !seen.contains(it->id) && !storage.directoryChanging()) {
                queue.enqueue(*it);
                while (queue.size() > model.limit())
                    queue.dequeue();
            }
        }
        seen = std::move(current);
        loadNext();
    }

    void loadNext() {
        if (loading.valid() || queue.isEmpty())
            return;
        const auto record = queue.dequeue();
        auto* repository = &storage::ApplicationStorage::instance().captureHistory();
        loadingGeneration = generation;
        loading = std::async(std::launch::async, [repository, record] {
            const auto image = repository->loadResultImagePreservingRecord(record);
            return image.value_or(QImage());
        });
        completion.start();
    }

    void finishLoad() {
        if (!loading.valid() ||
            loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        QImage image = loading.get();
        completion.stop();
        if (enabled && automatic && loadingGeneration == generation && !image.isNull())
            model.add({}, std::move(image));
        loadNext();
    }

    void shutdown() {
        completion.stop();
        clipboardPoll.stop();
        queue.clear();
        if (loading.valid())
            loading.wait();
        window.reset();
        model.clear();
        clipboardModel.clear();
        enabled = false;
        clipboardEnabled = false;
    }

    RackController& q;
    RackContentModel model;
    RackContentModel clipboardModel;
    std::unique_ptr<RackWindow> window;
    QSet<QString> seen;
    QQueue<storage::CaptureHistoryRecord> queue;
    std::future<QImage> loading;
    QTimer completion;
    QTimer clipboardPoll;
    quint64 generation = 0;
    quint64 loadingGeneration = 0;
    qint64 lastChangeCount = -1;
    bool started = false;
    bool enabled = false;
    bool automatic = false;
    bool clipboardEnabled = false;
    bool writingClipboard = false;
};

RackController::RackController(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this)) {}
RackController::~RackController() {
    shutdown();
}
void RackController::start() {
    m_impl->start();
}
void RackController::toggle() {
    if (m_impl->enabled && m_impl->window)
        m_impl->window->toggle();
}
void RackController::showClipboardHistory() {
    if (m_impl->enabled && m_impl->window)
        m_impl->window->showClipboardHistory();
}
void RackController::setCaptureActive(const QString& source, bool active) {
    if (m_impl->enabled && m_impl->window)
        m_impl->window->setCaptureActive(source, active);
}
void RackController::shutdown() {
    m_impl->shutdown();
}
} // namespace snow_shot::presentation
