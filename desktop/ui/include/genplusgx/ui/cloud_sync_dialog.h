#pragma once

#include "genplusgx/cloud/cloud_settings.h"
#include "genplusgx/cloud/cloud_types.h"

#include <QDialog>

#include <functional>
#include <string>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace genplusgx::ui {

class CloudSyncDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(const cloud::Settings&)>;
  using PasswordSink = std::function<void(
    std::string endpoint, std::string username, std::string password)>;
  using AccountSink = std::function<void(
    std::string endpoint, std::string username)>;
  using SyncSink = std::function<void(std::string password)>;

  explicit CloudSyncDialog(QWidget* parent = nullptr);

  void setSettings(cloud::Settings settings);
  void setSettingsSink(SettingsSink sink);
  void setPasswordSink(PasswordSink sink);
  void setForgetSink(AccountSink sink);
  void setSyncSink(SyncSink sink);
  void setGameActive(bool active);
  void setBusy(bool busy);
  void showResult(const cloud::SyncResult& result);
  void showStatus(const std::string& detail);
  void showError(const std::string& detail);

private:
  [[nodiscard]] bool apply();
  [[nodiscard]] cloud::Settings stagedSettings() const;
  void rememberPassword();
  void forgetPassword();
  void synchronizeNow();
  void refreshControls();

  QCheckBox* enabled_{nullptr};
  QLineEdit* endpoint_{nullptr};
  QLineEdit* username_{nullptr};
  QLineEdit* password_{nullptr};
  QLineEdit* remoteDirectory_{nullptr};
  QCheckBox* saves_{nullptr};
  QCheckBox* states_{nullptr};
  QCheckBox* startup_{nullptr};
  QCheckBox* gameClose_{nullptr};
  QLabel* status_{nullptr};
  QTableWidget* results_{nullptr};
  QPushButton* apply_{nullptr};
  QPushButton* remember_{nullptr};
  QPushButton* forget_{nullptr};
  QPushButton* sync_{nullptr};
  cloud::Settings settings_;
  SettingsSink settingsSink_;
  PasswordSink passwordSink_;
  AccountSink forgetSink_;
  SyncSink syncSink_;
  bool gameActive_{false};
  bool busy_{false};
};

} // namespace genplusgx::ui
