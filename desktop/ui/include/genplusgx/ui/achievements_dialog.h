#pragma once

#include "genplusgx/achievements/achievement_settings.h"
#include "genplusgx/achievements/achievement_types.h"

#include <QDialog>

#include <functional>
#include <string>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace genplusgx::ui {

class AchievementsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(
    const achievements::Settings&)>;
  using LoginSink = std::function<void(std::string, std::string)>;
  using LogoutSink = std::function<void()>;

  explicit AchievementsDialog(QWidget* parent = nullptr);

  void setSettings(achievements::Settings settings);
  void setSnapshot(const achievements::Snapshot& snapshot);
  void setSettingsSink(SettingsSink sink);
  void setLoginSink(LoginSink sink);
  void setLogoutSink(LogoutSink sink);
  void showError(const std::string& detail);

private:
  [[nodiscard]] bool apply();
  void signIn();
  void refreshControls();

  QCheckBox* enabled_{nullptr};
  QCheckBox* hardcore_{nullptr};
  QCheckBox* unofficial_{nullptr};
  QCheckBox* encore_{nullptr};
  QCheckBox* notifications_{nullptr};
  QLineEdit* username_{nullptr};
  QLineEdit* password_{nullptr};
  QLabel* connection_{nullptr};
  QLabel* score_{nullptr};
  QLabel* game_{nullptr};
  QLabel* presence_{nullptr};
  QLabel* validation_{nullptr};
  QPushButton* signIn_{nullptr};
  QPushButton* signOut_{nullptr};
  QPushButton* apply_{nullptr};
  QTableWidget* achievements_{nullptr};
  achievements::Settings settings_;
  achievements::Snapshot snapshot_;
  SettingsSink settingsSink_;
  LoginSink loginSink_;
  LogoutSink logoutSink_;
};

} // namespace genplusgx::ui
