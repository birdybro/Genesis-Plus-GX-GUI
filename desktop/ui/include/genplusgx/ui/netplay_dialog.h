#pragma once

#include "genplusgx/netplay/netplay_types.h"
#include "genplusgx/persistence.h"

#include <QDialog>

#include <cstdint>
#include <functional>
#include <string>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace genplusgx::ui {

enum class NetplayUiOperation {
  host,
  join,
  disconnect,
};

struct NetplayUiRequest final {
  NetplayUiOperation operation{NetplayUiOperation::host};
  std::string host;
  std::uint16_t port{netplay::defaultPort};
  std::string sessionCode;
  std::uint32_t inputDelayFrames{2U};
  std::uint32_t rollbackFrames{8U};
};

class NetplayDialog final : public QDialog {
  Q_OBJECT

public:
  using RequestSink = std::function<PersistenceStatus(NetplayUiRequest)>;

  explicit NetplayDialog(QWidget* parent = nullptr);

  void setRequestSink(RequestSink sink);
  void setGameReady(bool ready);
  void setSessionState(
    netplay::NetplaySessionState state,
    const std::string& detail = {});
  void showError(const std::string& detail);

private:
  void updateMode();
  void submit();
  void disconnectSession();
  void refreshControls();

  QComboBox* mode_{nullptr};
  QLineEdit* host_{nullptr};
  QSpinBox* port_{nullptr};
  QLineEdit* code_{nullptr};
  QSpinBox* delay_{nullptr};
  QSpinBox* rollback_{nullptr};
  QLabel* status_{nullptr};
  QLabel* validation_{nullptr};
  QPushButton* connect_{nullptr};
  QPushButton* disconnect_{nullptr};
  RequestSink requestSink_;
  netplay::NetplaySessionState state_{netplay::NetplaySessionState::disconnected};
  bool gameReady_{false};
};

} // namespace genplusgx::ui
