#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/updates/update_settings.h"
#include "genplusgx/updates/update_types.h"

#include <QDialog>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

class QCheckBox;
class QLabel;
class QPushButton;

namespace genplusgx::ui {

class UpdateDialog final : public QDialog {
  Q_OBJECT

public:
  using SettingsSink = std::function<PersistenceStatus(const updates::Settings&)>;
  using CheckSink = std::function<void()>;
  using DownloadSink = std::function<void(const updates::Asset&)>;
  using UrlSink = std::function<bool(const std::string&)>;
  using FileSink = std::function<bool(const std::filesystem::path&)>;

  explicit UpdateDialog(QWidget* parent = nullptr);

  void setSettings(updates::Settings settings);
  void setCurrentVersion(std::string version);
  void setSettingsSink(SettingsSink sink);
  void setCheckSink(CheckSink sink);
  void setDownloadSink(DownloadSink sink);
  void setUrlSink(UrlSink sink);
  void setFileSink(FileSink sink);
  void setBusy(bool busy, bool downloading = false);
  void presentCheck(updates::CheckResult result);
  void presentCheckFailure(const std::string& detail);
  void presentDownload(updates::DownloadResult result);
  void presentDownloadFailure(const std::string& detail);

private:
  bool saveSettings();
  void refresh();

  updates::Settings settings_;
  std::string currentVersion_;
  std::optional<updates::CheckResult> checkResult_;
  std::optional<updates::DownloadResult> downloadResult_;
  SettingsSink settingsSink_;
  CheckSink checkSink_;
  DownloadSink downloadSink_;
  UrlSink urlSink_;
  FileSink fileSink_;
  QCheckBox* automatic_{nullptr};
  QLabel* trustLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QLabel* detailLabel_{nullptr};
  QPushButton* checkButton_{nullptr};
  QPushButton* downloadButton_{nullptr};
  QPushButton* releasePageButton_{nullptr};
  QPushButton* openPackageButton_{nullptr};
  bool busy_{false};
  bool downloading_{false};
};

} // namespace genplusgx::ui
