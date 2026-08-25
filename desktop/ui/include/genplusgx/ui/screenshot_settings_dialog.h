#pragma once

#include "genplusgx/settings/screenshot_settings.h"
#include "genplusgx/ui/dialog_service.h"

#include <QDialog>

#include <filesystem>
#include <functional>
#include <memory>

class QLineEdit;

namespace genplusgx::ui {

class ScreenshotSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<bool(const settings::ScreenshotSettings&)>;

  ScreenshotSettingsDialog(settings::ScreenshotSettings settings,
    std::filesystem::path defaultDirectory,
    std::shared_ptr<DialogService> dialogService,
    QWidget* parent = nullptr);

  void setDialogService(std::shared_ptr<DialogService> service);
  void setSettingsSink(SettingsSink sink);
  void setSettings(const settings::ScreenshotSettings& settings);
  [[nodiscard]] settings::ScreenshotSettings settings() const;

private:
  [[nodiscard]] bool apply();
  void chooseDirectory();
  void restoreDefaults();

  std::filesystem::path defaultDirectory_;
  std::shared_ptr<DialogService> dialogService_;
  QLineEdit* directory_{nullptr};
  SettingsSink settingsSink_;
};

} // namespace genplusgx::ui
