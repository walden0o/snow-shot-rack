// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PRESENTATION_RACKWINDOW_H
#define SNOW_SHOT_PRESENTATION_RACKWINDOW_H

#include "snow_shot/presentation/rackcontentmodel.h"
#include <QPointer>
#include <QPropertyAnimation>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QScreen;
namespace adqt::widgets {
class AdButton;
}

namespace snow_shot::presentation {
class RackWindow final : public QWidget {
    Q_OBJECT
  public:
    enum class View { Screenshots, Clipboard };

    RackWindow(RackContentModel& model, RackContentModel& clipboardModel);
    void setClipboardHistoryEnabled(bool enabled);
    void setView(View view);
    void showClipboardHistory();
    [[nodiscard]] View view() const;
    void setDisplayMode(const QString& mode);
    void setCaptureActive(const QString& source, bool active);
    void toggle();
    void expand(bool activate = false);
    void collapse();
    [[nodiscard]] bool expanded() const;
    [[nodiscard]] bool interacting() const;

  signals:
    void copyRequested(const snow_shot::presentation::RackEntry& entry);
    void settingsRequested();

  protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void refresh();
    void retranslateUi();
    void applyTheme();
    void trackPointer();
    void animate(bool expanded);
    void dragEntry(const QString& id);
    void contextMenu(const QPoint& point);
    void finishInteraction();
    void openEntry(const RackEntry& entry);
    [[nodiscard]] QString exportEntry(const RackEntry& entry);
    [[nodiscard]] QRect targetGeometry(bool expanded) const;
    [[nodiscard]] bool pointerInHoverArea(const QPoint& cursor) const;
    [[nodiscard]] std::optional<RackEntry> entry(const QString& id) const;
    [[nodiscard]] RackContentModel& currentModel() const;

    RackContentModel& m_model;
    RackContentModel& m_clipboardModel;
    adqt::widgets::AdButton* m_screenshotsTab = nullptr;
    adqt::widgets::AdButton* m_clipboardTab = nullptr;
    adqt::widgets::AdButton* m_clearHistory = nullptr;
    QLineEdit* m_search = nullptr;
    View m_view = View::Screenshots;
    bool m_clipboardEnabled = false;
    QWidget* m_body = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_empty = nullptr;
    QListWidget* m_list = nullptr;
    adqt::widgets::AdButton* m_settings = nullptr;
    adqt::widgets::AdButton* m_close = nullptr;
    QPointer<QScreen> m_screen;
    QPropertyAnimation m_animation;
    QTimer m_pointerTimer;
    QTimer m_closeTimer;
    QString m_mode = QStringLiteral("hover");
    QSet<QString> m_captureSources;
    int m_interactionDepth = 0;
    bool m_expanded = false;
    bool m_refreshPending = false;
};
} // namespace snow_shot::presentation
#endif
