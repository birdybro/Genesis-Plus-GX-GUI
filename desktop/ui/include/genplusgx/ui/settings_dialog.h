#pragma once

#include "genplusgx/core_system_settings.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/persistence.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/settings/appearance_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/screenshot_settings.h"
#include "genplusgx/settings/video_settings.h"

#include <QDialog>

#include <cstddef>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace genplusgx::ui {

enum class SettingsPage {
  general,
  video,
  audio,
  input,
  system,
  bios,
  paths,
  advanced,
};

enum class SettingsPageAction {
  appearance,
  video,
  audio,
  inputBindings,
  playerAssignments,
  system,
  bios,
  screenshotPath,
  gameLibrary,
  diagnostics,
  perGame,
};

struct SettingsOverview final {
  settings::AppearanceSettings appearance;
  settings::VideoSettings video;
  settings::AudioSettings audio;
  input::InputConfiguration input;
  CoreSystemSettings system;
  platform::BiosSnapshot bios;
  settings::ScreenshotSettings screenshots;
  ApplicationPaths paths;
  std::size_t connectedControllerCount{0U};
  bool pathsAvailable{false};
  bool gameLoaded{false};
};

class SettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using ActionSink = std::function<void(SettingsPageAction)>;

  explicit SettingsDialog(SettingsOverview overview, QWidget* parent = nullptr);

  void setOverview(SettingsOverview overview);
  void setActionSink(ActionSink sink);
  void openPage(SettingsPage page);
  [[nodiscard]] SettingsPage currentPage() const noexcept;

private:
  void dispatch(SettingsPageAction action);
  void refresh();

  SettingsOverview overview_;
  ActionSink actionSink_;
  QListWidget* categories_{nullptr};
  QStackedWidget* pages_{nullptr};
  QLabel* generalSummary_{nullptr};
  QLabel* videoSummary_{nullptr};
  QLabel* audioSummary_{nullptr};
  QLabel* inputSummary_{nullptr};
  QLabel* systemSummary_{nullptr};
  QLabel* biosSummary_{nullptr};
  QLabel* pathsSummary_{nullptr};
  QLabel* advancedSummary_{nullptr};
  QPushButton* perGameButton_{nullptr};
};

} // namespace genplusgx::ui
