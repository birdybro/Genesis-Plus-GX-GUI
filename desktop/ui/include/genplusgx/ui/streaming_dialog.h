#pragma once

#include "genplusgx/capture/streaming_service.h"

#include <QDialog>

#include <functional>

class QLabel;
class QPushButton;
class QSpinBox;

namespace genplusgx::ui {

class StreamingDialog final : public QDialog {
  Q_OBJECT

public:
  using RequestSink = std::function<capture::StreamingStatus(
    bool start, capture::StreamingConfiguration configuration)>;

  explicit StreamingDialog(QWidget* parent = nullptr);
  void setRequestSink(RequestSink sink);
  void setMetrics(capture::StreamingMetrics metrics);
  void showFailure(const std::string& detail);

private:
  void requestToggle();
  void refresh();

  RequestSink requestSink_;
  capture::StreamingMetrics metrics_;
  QSpinBox* port_{nullptr};
  QSpinBox* clients_{nullptr};
  QLabel* state_{nullptr};
  QLabel* metricsLabel_{nullptr};
  QPushButton* toggle_{nullptr};
  bool requestPending_{false};
};

} // namespace genplusgx::ui
