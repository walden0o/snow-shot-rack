// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackwindow.h"

#include "snow_shot/platform/screenshotnative.h"
#include "snow_shot/presentation/styles/thememanager.h"
#include "snow_shot/storage/clipboardfilecache.h"
#include "widgets/button.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace snow_shot::presentation {
namespace {
class RackList final : public QListWidget {
  public:
    using QListWidget::QListWidget;
    std::function<void(const QString&)> drag;

  protected:
    void startDrag(Qt::DropActions) override {
        if (currentItem() && drag)
            drag(currentItem()->data(Qt::UserRole).toString());
    }
};
} // namespace

RackWindow::RackWindow(RackContentModel& model, RackContentModel& clipboardModel)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_model(model), m_clipboardModel(clipboardModel), m_screen(QGuiApplication::primaryScreen()),
      m_animation(this, "geometry") {
    setObjectName(QStringLiteral("rack-window"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_QuitOnClose, false);
    m_body = new QWidget(this);
    auto* layout = new QVBoxLayout(m_body);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    m_title = new QLabel(m_body);
    m_settings = new adqt::widgets::AdButton(m_body);
    m_close = new adqt::widgets::AdButton(m_body);
    m_settings->setObjectName(QStringLiteral("rack-settings"));
    m_close->setObjectName(QStringLiteral("rack-collapse"));
    m_settings->setButtonStyle(adqt::widgets::AdButton::ButtonStyle::Text);
    m_close->setButtonStyle(adqt::widgets::AdButton::ButtonStyle::Text);
    m_screenshotsTab = new adqt::widgets::AdButton(m_body);
    m_clipboardTab = new adqt::widgets::AdButton(m_body);
    m_clearHistory = new adqt::widgets::AdButton(m_body);
    m_screenshotsTab->setObjectName(QStringLiteral("rack-screenshots-tab"));
    m_clipboardTab->setObjectName(QStringLiteral("rack-clipboard-tab"));
    m_clearHistory->setObjectName(QStringLiteral("rack-clear-clipboard"));
    m_clearHistory->setButtonStyle(adqt::widgets::AdButton::ButtonStyle::Text);
    m_search = new QLineEdit(m_body);
    m_search->setObjectName(QStringLiteral("rack-clipboard-search"));
    m_search->setClearButtonEnabled(true);
    header->addWidget(m_title);
    header->addWidget(m_screenshotsTab);
    header->addWidget(m_clipboardTab);
    header->addWidget(m_search, 1);
    header->addStretch();
    header->addWidget(m_clearHistory);
    header->addWidget(m_settings);
    header->addWidget(m_close);
    layout->addLayout(header);
    auto* list = new RackList(m_body);
    m_list = list;
    list->drag = [this](const QString& id) { dragEntry(id); };
    list->setObjectName(QStringLiteral("rack-cards"));
    list->setViewMode(QListView::IconMode);
    list->setFlow(QListView::LeftToRight);
    list->setWrapping(false);
    list->setIconSize(QSize(144, 100));
    list->setGridSize(QSize(164, 132));
    list->setSpacing(4);
    list->setDragEnabled(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setFrameShape(QFrame::NoFrame);
    layout->addWidget(list, 1);
    m_empty = new QLabel(m_body);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);
    m_hint = new QLabel(m_body);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (const auto selected = entry(item->data(Qt::UserRole).toString())) {
            emit copyRequested(*selected);
            m_hint->setText(selected->image.isNull() ? tr("Text copied to clipboard")
                                                     : tr("Copied to clipboard"));
        }
    });
    connect(list, &QWidget::customContextMenuRequested, this, &RackWindow::contextMenu);
    connect(m_settings, &QAbstractButton::clicked, this, [this] {
        collapse();
        emit settingsRequested();
    });
    connect(m_close, &QAbstractButton::clicked, this, &RackWindow::collapse);
    connect(&m_model, &RackContentModel::changed, this, &RackWindow::refresh);
    connect(&m_clipboardModel, &RackContentModel::changed, this, &RackWindow::refresh);
    connect(m_screenshotsTab, &QAbstractButton::clicked, this,
            [this] { setView(View::Screenshots); });
    connect(m_clipboardTab, &QAbstractButton::clicked, this, [this] { setView(View::Clipboard); });
    connect(m_clearHistory, &QAbstractButton::clicked, this, [this] { m_clipboardModel.clear(); });
    connect(m_search, &QLineEdit::textChanged, this, &RackWindow::refresh);
    connect(&styles::ThemeManager::instance(), &styles::ThemeManager::themeChanged, this,
            [this] { applyTheme(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        m_screen = QGuiApplication::primaryScreen();
        m_animation.stop();
        setGeometry(targetGeometry(m_expanded));
    });
    m_animation.setDuration(170);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QPropertyAnimation::finished, this,
            [this] { m_body->setVisible(m_expanded); });
    m_closeTimer.setSingleShot(true);
    m_closeTimer.setInterval(200);
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        if (!interacting() && !geometry().contains(QCursor::pos()))
            collapse();
    });
    m_pointerTimer.setInterval(50);
    connect(&m_pointerTimer, &QTimer::timeout, this, &RackWindow::trackPointer);
    m_pointerTimer.start();
    setView(View::Screenshots);
    applyTheme();
#ifdef Q_OS_MACOS
    platform::configureRackWindow(this);
#endif
    setGeometry(targetGeometry(false));
    m_body->hide();
}

void RackWindow::setClipboardHistoryEnabled(bool enabled) {
    m_clipboardEnabled = enabled;
    refresh();
}

void RackWindow::setView(View view) {
    m_view = view;
    if (view == View::Screenshots)
        m_search->clear();
    m_screenshotsTab->setButtonStyle(view == View::Screenshots
                                         ? adqt::widgets::AdButton::ButtonStyle::Solid
                                         : adqt::widgets::AdButton::ButtonStyle::Text);
    m_clipboardTab->setButtonStyle(view == View::Clipboard
                                       ? adqt::widgets::AdButton::ButtonStyle::Solid
                                       : adqt::widgets::AdButton::ButtonStyle::Text);
    retranslateUi();
}

RackWindow::View RackWindow::view() const {
    return m_view;
}

void RackWindow::showClipboardHistory() {
    if (m_expanded && m_view == View::Clipboard) {
        collapse();
        return;
    }
    setView(View::Clipboard);
    expand(true);
    m_search->setFocus();
}

RackContentModel& RackWindow::currentModel() const {
    return m_view == View::Clipboard ? m_clipboardModel : m_model;
}

void RackWindow::setDisplayMode(const QString& mode) {
    m_mode = mode;
    if (mode == QStringLiteral("always"))
        expand();
    else
        collapse();
}

void RackWindow::setCaptureActive(const QString& source, bool active) {
    if (active)
        m_captureSources.insert(source);
    else
        m_captureSources.remove(source);
    m_closeTimer.stop();
    if (!m_captureSources.isEmpty()) {
        m_animation.stop();
        hide();
    } else {
        m_expanded = m_mode == QStringLiteral("always");
        setGeometry(targetGeometry(m_expanded));
        m_body->setVisible(m_expanded);
        show();
#ifdef Q_OS_MACOS
        platform::configureRackWindow(this);
#endif
    }
}

void RackWindow::toggle() {
    if (m_expanded)
        collapse();
    else
        expand(true);
}

void RackWindow::expand(bool activate) {
    if (!m_captureSources.isEmpty())
        return;
    m_closeTimer.stop();
    if (!m_screen)
        m_screen = QGuiApplication::primaryScreen();
    animate(true);
    show();
#ifdef Q_OS_MACOS
    platform::configureRackWindow(this);
#endif
    if (activate) {
        raise();
        activateWindow();
        m_list->setFocus();
    }
}

void RackWindow::collapse() {
    if (interacting())
        return;
    m_closeTimer.stop();
    animate(false);
}

bool RackWindow::expanded() const {
    return m_expanded;
}

bool RackWindow::interacting() const {
    return m_interactionDepth > 0 || QApplication::activePopupWidget() ||
           QApplication::activeModalWidget() || (isActiveWindow() && m_search->hasFocus());
}

QRect RackWindow::targetGeometry(bool expanded) const {
    const QRect screen = m_screen ? m_screen->geometry() : QRect(0, 0, 1024, 768);
    const int width = std::min(660, screen.width() - 24);
    return expanded ? QRect(screen.right() - width - 12, screen.top(), width, 260)
                    : QRect(screen.right() - 156, screen.top(), 112, 8);
}

void RackWindow::animate(bool expanded) {
    const QRect target = targetGeometry(expanded);
    if (m_expanded == expanded &&
        (m_animation.state() == QAbstractAnimation::Running || geometry() == target))
        return;
    m_expanded = expanded;
    const QRect panel = targetGeometry(true);
    m_body->setGeometry(8, 22, panel.width() - 16, panel.height() - 28);
    if (expanded)
        m_body->show();
    m_animation.stop();
    m_animation.setStartValue(geometry());
    m_animation.setEndValue(target);
    m_animation.start();
    update();
}

void RackWindow::trackPointer() {
    if (!isVisible() || !m_captureSources.isEmpty())
        return;
    const QPoint cursor = QCursor::pos();
    if (!m_expanded && m_mode == QStringLiteral("hover")) {
        QScreen* screen = QGuiApplication::screenAt(cursor);
        if (screen && cursor.y() >= screen->geometry().top() &&
            cursor.y() <= screen->geometry().top() + 5 &&
            cursor.x() >= screen->geometry().right() - 180 &&
            cursor.x() <= screen->geometry().right() - 20) {
            m_screen = screen;
            expand();
        }
    }
    if (m_expanded && m_mode == QStringLiteral("hover")) {
        if (geometry().contains(cursor) || interacting())
            m_closeTimer.stop();
        else if (!m_closeTimer.isActive())
            m_closeTimer.start();
    }
}

void RackWindow::refresh() {
    if (m_interactionDepth > 0) {
        m_refreshPending = true;
        return;
    }
    const QString selected =
        m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    m_list->clear();
    const bool clipboard = m_view == View::Clipboard;
    m_search->setVisible(clipboard && m_clipboardEnabled);
    m_clearHistory->setVisible(clipboard && m_clipboardEnabled);
    if (clipboard) {
        m_list->setViewMode(QListView::ListMode);
        m_list->setFlow(QListView::TopToBottom);
        m_list->setIconSize(QSize(48, 32));
        m_list->setGridSize({});
    } else {
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setIconSize(QSize(144, 100));
        m_list->setGridSize(QSize(164, 132));
    }
    const auto values = clipboard ? (m_clipboardEnabled ? m_clipboardModel.search(m_search->text())
                                                        : QVector<RackEntry>())
                                  : m_model.entries();
    for (const auto& value : values) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, value.id);
        const QString time = value.createdUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
        if (value.image.isNull()) {
            const QString line = value.text.simplified();
            item->setText(QStringLiteral("%1  %2").arg(time, line.left(120)));
            item->setToolTip(value.text.left(1000));
        } else {
            item->setText(clipboard ? QStringLiteral("%1  %2 × %3")
                                          .arg(time)
                                          .arg(value.image.width())
                                          .arg(value.image.height())
                                    : time);
            item->setToolTip(tr("Click to copy image; drag to share"));
            item->setIcon(QPixmap::fromImage(
                value.image.scaled(288, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        if (value.id == selected)
            m_list->setCurrentItem(item);
    }
    if (clipboard)
        m_empty->setText(m_clipboardEnabled
                             ? (m_search->text().isEmpty()
                                    ? tr("Copied text and images appear here")
                                    : tr("No matching clipboard items"))
                             : tr("Clipboard history is off. Turn it on in Settings"));
    else
        m_empty->setText(tr("Your next screenshot will appear here"));
    m_empty->setVisible(m_list->count() == 0);
    m_list->setVisible(m_list->count() > 0);
}

std::optional<RackEntry> RackWindow::entry(const QString& id) const {
    for (const auto& value : currentModel().entries()) {
        if (value.id == id)
            return value;
    }
    return std::nullopt;
}

QString RackWindow::exportEntry(const RackEntry& value) {
    QString error;
    auto directory = storage::ClipboardFileCache::shared().createDirectory(&error);
    if (!directory) {
        m_hint->setText(tr("Could not prepare the image file"));
        return {};
    }
    const QString path =
        QDir(directory->path()).filePath(QStringLiteral("Snow-Shot-%1.png").arg(value.id));
    if (!value.image.save(path, "PNG")) {
        m_hint->setText(tr("Could not prepare the image file"));
        return {};
    }
    directory->retain();
    return path;
}

void RackWindow::dragEntry(const QString& id) {
    const auto selected = entry(id);
    if (!selected)
        return;
    ++m_interactionDepth;
    m_closeTimer.stop();
    if (selected->image.isNull()) {
        QDrag drag(m_list);
        auto* mime = new QMimeData;
        mime->setText(selected->text);
        drag.setMimeData(mime);
        drag.exec(Qt::CopyAction, Qt::CopyAction);
        finishInteraction();
        return;
    }
    const QString path = exportEntry(*selected);
    if (!path.isEmpty()) {
        QDrag drag(m_list);
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        mime->setImageData(selected->image);
        drag.setMimeData(mime);
        drag.setPixmap(QPixmap::fromImage(
            selected->image.scaled(144, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        drag.exec(Qt::CopyAction, Qt::CopyAction);
    }
    finishInteraction();
}

void RackWindow::openEntry(const RackEntry& selected) {
    const QString path = exportEntry(selected);
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void RackWindow::contextMenu(const QPoint& point) {
    const auto* item = m_list->itemAt(point);
    if (!item)
        return;
    const auto selected = entry(item->data(Qt::UserRole).toString());
    if (!selected)
        return;
    ++m_interactionDepth;
    m_closeTimer.stop();
    QMenu menu(this);
    const bool image = !selected->image.isNull();
    auto& model = currentModel();
    menu.addAction(image ? tr("Copy image") : tr("Copy text"), this,
                   [this, selected] { emit copyRequested(*selected); });
    if (image)
        menu.addAction(tr("Open image"), this, [this, selected] { openEntry(*selected); });
    menu.addSeparator();
    menu.addAction(m_view == View::Clipboard ? tr("Remove from history") : tr("Remove from Rack"),
                   this, [&model, selected] { model.remove(selected->id); });
    menu.exec(m_list->viewport()->mapToGlobal(point));
    finishInteraction();
}

void RackWindow::finishInteraction() {
    --m_interactionDepth;
    if (m_refreshPending) {
        m_refreshPending = false;
        refresh();
    }
    trackPointer();
}

void RackWindow::retranslateUi() {
    setWindowTitle(tr("Rack"));
    setAccessibleName(tr("Screenshot Rack"));
    m_title->setText(tr("Rack"));
    m_settings->setText(tr("Settings"));
    m_close->setText(tr("Collapse"));
    m_screenshotsTab->setText(tr("Screenshots"));
    m_clipboardTab->setText(tr("Clipboard"));
    m_clearHistory->setText(tr("Clear"));
    m_search->setPlaceholderText(tr("Search clipboard history"));
    m_hint->setText(m_view == View::Clipboard ? tr("Click to copy again; drag to share")
                                              : tr("Click to copy image; drag to share"));
    m_list->setAccessibleName(m_view == View::Clipboard ? tr("Clipboard history")
                                                        : tr("Rack screenshots"));
    refresh();
}

void RackWindow::applyTheme() {
    const auto scheme = styles::ThemeManager::instance().themeColorScheme();
    auto colors = palette();
    colors.setColor(QPalette::Window, scheme.map.colorBgContainer);
    colors.setColor(QPalette::Base, scheme.map.colorBgContainer);
    colors.setColor(QPalette::Text, scheme.map.colorText);
    colors.setColor(QPalette::WindowText, scheme.map.colorText);
    colors.setColor(QPalette::Highlight, scheme.map.colorPrimaryBg);
    colors.setColor(QPalette::HighlightedText, scheme.map.colorText);
    setPalette(colors);
    update();
}

void RackWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, -12, -0.5, -0.5), 18, 18);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Text));
    painter.drawRoundedRect(QRectF(width() / 2.0 - 20, 2, 40, 3), 1.5, 1.5);
}

void RackWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void RackWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    collapse();
}

void RackWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() < 22)
        toggle();
    else
        QWidget::mousePressEvent(event);
}

void RackWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        collapse();
        return;
    }
    if (auto* item = m_list->currentItem()) {
        const auto selected = entry(item->data(Qt::UserRole).toString());
        if (selected && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
                         event->matches(QKeySequence::Copy))) {
            emit copyRequested(*selected);
            return;
        }
        if (selected && event->key() == Qt::Key_Delete) {
            currentModel().remove(selected->id);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
} // namespace snow_shot::presentation
