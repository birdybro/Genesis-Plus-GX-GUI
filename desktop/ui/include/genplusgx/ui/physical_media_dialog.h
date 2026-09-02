#pragma once

#include "genplusgx/platform/physical_media.h"

#include <QDialog>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class QCloseEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace genplusgx::ui {

struct PhysicalMediaDialogActions final {
  std::function<void()> discover;
  std::function<void(const std::string& driveId)> importDisc;
  std::function<void()> cancel;
};

class PhysicalMediaDialog final : public QDialog {
  Q_OBJECT

public:
  explicit PhysicalMediaDialog(QWidget* parent = nullptr);

  void setActions(PhysicalMediaDialogActions actions);
  void beginDiscovery();
  void setDrives(std::vector<platform::PhysicalDrive> drives);
  void setImportStarted();
  void setImportProgress(
    std::uint32_t completedSectors, std::uint32_t totalSectors);
  void setOperationFailed(const std::string& detail);
  void setOperationCancelled();
  void setImportReady();
  [[nodiscard]] bool busy() const noexcept;

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  enum class State {
    idle,
    discovering,
    importing,
  };

  void requestImport();
  void requestCancel();
  void updateControls();

  PhysicalMediaDialogActions actions_;
  std::vector<platform::PhysicalDrive> drives_;
  QListWidget* driveList_{nullptr};
  QLabel* statusLabel_{nullptr};
  QProgressBar* progressBar_{nullptr};
  QPushButton* refreshButton_{nullptr};
  QPushButton* importButton_{nullptr};
  QPushButton* cancelButton_{nullptr};
  QPushButton* closeButton_{nullptr};
  State state_{State::idle};
};

} // namespace genplusgx::ui
