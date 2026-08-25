#pragma once

#include "genplusgx/settings/per_game_settings.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace genplusgx::ui {

class PerGameSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using ConfigurationSink =
    std::function<PersistenceStatus(const settings::PerGameSettings&)>;

  PerGameSettingsDialog(settings::PerGameSettings overrides,
    settings::GlobalGameSettings global,
    std::vector<std::string> inputProfiles,
    std::vector<std::string> audioDevices,
    QWidget* parent = nullptr);

  void setConfigurationSink(ConfigurationSink sink);
  void setSession(
    const settings::PerGameSettings& overrides, settings::GlobalGameSettings global);
  void setConfiguration(const settings::PerGameSettings& overrides);
  [[nodiscard]] settings::PerGameSettings configuration() const;

private:
  void updateControls();
  void editVideo();
  void editAudio();
  void editSystem();
  void editBios();
  [[nodiscard]] bool apply();
  void restoreGlobal();

  settings::GlobalGameSettings global_;
  settings::VideoSettings video_;
  settings::AudioSettings audio_;
  CoreSystemSettings system_;
  platform::BiosConfiguration bios_;
  std::vector<std::string> inputProfiles_;
  std::vector<std::string> audioDevices_;
  QCheckBox* videoOverride_{nullptr};
  QCheckBox* audioOverride_{nullptr};
  QCheckBox* systemOverride_{nullptr};
  QCheckBox* inputOverride_{nullptr};
  QCheckBox* biosOverride_{nullptr};
  QPushButton* editVideoButton_{nullptr};
  QPushButton* editAudioButton_{nullptr};
  QPushButton* editSystemButton_{nullptr};
  QPushButton* editBiosButton_{nullptr};
  QComboBox* inputProfile_{nullptr};
  QLabel* validation_{nullptr};
  ConfigurationSink configurationSink_;
};

} // namespace genplusgx::ui
