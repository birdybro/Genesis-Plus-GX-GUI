#pragma once

#include "genplusgx/platform/bios_manager.h"

#include <QDialog>

#include <array>
#include <filesystem>
#include <functional>
#include <optional>

class QLabel;
class QLineEdit;

namespace genplusgx::ui {

class BiosSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  using ConfigurationSink =
    std::function<void(const platform::BiosConfiguration&)>;
  using FilePicker = std::function<std::optional<std::filesystem::path>(
    platform::BiosSlot, const std::filesystem::path&)>;

  explicit BiosSettingsDialog(
    platform::BiosSnapshot snapshot,
    QWidget* parent = nullptr);

  void setConfigurationSink(ConfigurationSink sink);
  void setFilePicker(FilePicker picker);
  void setSnapshot(const platform::BiosSnapshot& snapshot);
  [[nodiscard]] const platform::BiosSnapshot& snapshot() const noexcept;

private:
  void browse(platform::BiosSlot slot);
  void clear(platform::BiosSlot slot);
  void refresh(platform::BiosSlot slot);
  void apply();
  void restoreDefaults();

  platform::BiosSnapshot snapshot_;
  std::array<QLineEdit*, platform::biosSlotCount> pathEdits_{};
  std::array<QLabel*, platform::biosSlotCount> statusLabels_{};
  std::array<QLabel*, platform::biosSlotCount> detailLabels_{};
  std::array<QLineEdit*, platform::biosSlotCount> checksumEdits_{};
  ConfigurationSink configurationSink_;
  FilePicker filePicker_;
};

} // namespace genplusgx::ui
