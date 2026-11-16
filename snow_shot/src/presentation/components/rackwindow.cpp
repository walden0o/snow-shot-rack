// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackwindow.h"

#include "racklist_p.h"
#include "snow_shot/platform/screenshotnative.h"
#include "snow_shot/presentation/rackhoverpolicy.h"
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
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace snow_shot::presentation {
namespace {
using detail::RackList;
using detail::RackRopeOffset;

// Screenshots hang like photos on a clothesline: a pin and string drop from the
// rope (painted by the list viewport) to a slightly tilted paper card.
inline constexpr int RackPinLength = 11;
class RackCardDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto* view = qobject_cast<const QListView*>(option.widget);
        if (!view || view->viewMode() != QListView::IconMode) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect cell = option.rect;
        const qreal centerX = cell.x() + cell.width() / 2.0;
        const QPointF ropePoint(centerX, cell.top() + RackRopeOffset);
        const QPointF pinPoint(centerX, ropePoint.y() + RackPinLength);

        const QRectF cardBounds(0, 0, cell.width() - 26, cell.height() - RackPinLength - 14);
        // Alternate the tilt so neighbouring cards hang at slightly different angles.
        const qreal tilt = (index.row() % 4 - 1.5) * 1.6;

        painter->setPen(QPen(option.palette.color(QPalette::Dark), 1.0));
        painter->drawLine(ropePoint, pinPoint);

        painter->translate(pinPoint);
        painter->rotate(tilt);
        painter->translate(-cardBounds.width() / 2.0, 0);
        const QRectF paper(0, 0, cardBounds.width(), cardBounds.height());

        QColor shadow = option.palette.color(QPalette::Shadow);
        shadow.setAlpha(70);
        painter->setPen(Qt::NoPen);
        painter->setBrush(shadow);
        painter->drawRoundedRect(paper.translated(0, 2.5), 7, 7);

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->setBrush(option.palette.color(QPalette::Base));
        painter->setPen(QPen(selected ? option.palette.color(QPalette::Highlight)
                                      : option.palette.color(QPalette::Mid),
                             selected ? 1.6 : 1.0));
        painter->drawRoundedRect(paper, 6, 6);
        if (hovered && !selected) {
            QColor tint = option.palette.color(QPalette::Highlight);
            tint.setAlpha(20);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tint);
            painter->drawRoundedRect(paper, 6, 6);
        }

        const QRectF imageRect = paper.adjusted(5, 5, -5, -20);
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QPixmap thumbnail = icon.isNull() ? QPixmap() : icon.pixmap(QSize(288, 200));
        if (!thumbnail.isNull()) {
            const QSizeF fit =
                QSizeF(thumbnail.size()).scaled(imageRect.size(), Qt::KeepAspectRatio);
            const QRectF thumbRect((paper.center().x() - fit.width() / 2.0),
                                   imageRect.top() + (imageRect.height() - fit.height()) / 2.0,
                                   fit.width(), fit.height());
            painter->setPen(Qt::NoPen);
            painter->drawPixmap(thumbRect, thumbnail, QRectF(thumbnail.rect()));
        }
        painter->setPen(option.palette.color(QPalette::WindowText));
        painter->drawText(QRectF(4, paper.bottom() - 16, paper.width() - 8, 13), Qt::AlignCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();

        // Pin stays upright regardless of card tilt, clipped over the card edge.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(option.palette.color(QPalette::Highlight));
        painter->drawRoundedRect(QRectF(pinPoint.x() - 4, pinPoint.y() - 4, 8, 15), 3, 3);
        painter->restore();
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
    list->setGridSize(QSize(168, 140));
    list->setSpacing(4);
    list->setItemDelegate(new RackCardDelegate(list));
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
        dismiss();
        emit settingsRequested();
    });
    connect(m_close, &QAbstractButton::clicked, this, &RackWindow::dismiss);
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
    m_animation.setDuration(RackAnimationMs);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QPropertyAnimation::finished, this,
            [this] { m_body->setVisible(m_expanded); });
    m_closeTimer.setSingleShot(true);
    m_closeTimer.setInterval(RackCollapseDelayMs);
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        if (rackCollapseAfterDelay(pointerInHoverArea(QCursor::pos()), interacting()))
            collapse();
    });
    m_pointerTimer.setInterval(RackPointerPollMs);
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
        dismiss();
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
        dismiss();
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

void RackWindow::dismiss() {
    if (rackCollapseBlocked(interactionState(), RackCollapseTrigger::Explicit))
        return;
    if (m_search->hasFocus())
        m_search->clearFocus();
    m_closeTimer.stop();
    animate(false);
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
    return rackCollapseBlocked(interactionState(), RackCollapseTrigger::Automatic);
}

RackInteractionState RackWindow::interactionState() const {
    return {.contentInteraction = m_interactionDepth > 0,
            .popupOpen = QApplication::activePopupWidget() != nullptr,
            .modalOpen = QApplication::activeModalWidget() != nullptr,
            .searchFocused = isActiveWindow() && m_search->hasFocus()};
}

QRect RackWindow::targetGeometry(bool expanded) const {
    const QRect screen = m_screen ? m_screen->geometry() : QRect(0, 0, 1024, 768);
    return rackTargetGeometry(screen, m_screen ? m_screen->availableGeometry() : screen, expanded);
}

bool RackWindow::pointerInHoverArea(const QPoint& cursor) const {
    return rackInHoverArea(geometry(), m_screen ? m_screen->geometry() : geometry(), cursor);
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
    QScreen* hovered = m_expanded ? nullptr : QGuiApplication::screenAt(cursor);
    const RackPointerState state{
        .mode = rackDisplayModeFromName(m_mode),
        .expanded = m_expanded,
        .inHotZone =
            hovered && rackInHotZone(hovered->geometry(), hovered->availableGeometry(), cursor),
        .inHoverArea = m_expanded && pointerInHoverArea(cursor),
        .interacting = interacting(),
        .collapsePending = m_closeTimer.isActive()};
    switch (rackPointerAction(state)) {
    case RackPointerAction::Expand:
        m_screen = hovered;
        expand();
        break;
    case RackPointerAction::CancelCollapse:
        m_closeTimer.stop();
        break;
    case RackPointerAction::ScheduleCollapse:
        m_closeTimer.start();
        break;
    case RackPointerAction::None:
        break;
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
        m_list->setGridSize(QSize(168, 140));
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
    colors.setColor(QPalette::Dark, scheme.map.colorTextSecondary);
    colors.setColor(QPalette::Highlight, scheme.map.colorPrimary);
    colors.setColor(QPalette::HighlightedText, scheme.alias.accentText);
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
    // The rack is always-on: closing only dismisses the panel. The event
    // must still be accepted — ignoring it vetoes QApplication::quit.
    event->accept();
    dismiss();
}

void RackWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() < 22)
        toggle();
    else
        QWidget::mousePressEvent(event);
}

void RackWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        dismiss();
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
