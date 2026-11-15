// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PRESENTATION_RACKCONTROLLER_H
#define SNOW_SHOT_PRESENTATION_RACKCONTROLLER_H

#include <QObject>
#include <memory>

namespace snow_shot::presentation {
class RackController final : public QObject {
    Q_OBJECT
  public:
    explicit RackController(QObject* parent = nullptr);
    ~RackController() override;
    void start();
    void toggle();
    void showClipboardHistory();
    void setCaptureActive(const QString& source, bool active);
    void shutdown();

  signals:
    void settingsRequested();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace snow_shot::presentation
#endif
