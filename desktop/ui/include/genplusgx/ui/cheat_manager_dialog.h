#pragma once

#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/core_debug.h"
#include "genplusgx/debug_analysis.h"

#include <QDialog>

#include <filesystem>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace genplusgx::ui {

class DialogService;

class CheatManagerDialog final : public QDialog {
  Q_OBJECT

public:
  using ConfigurationSink =
    std::function<PersistenceStatus(const cheats::CheatConfiguration&)>;
  using DebugRequestSink = std::function<bool(CoreDebugRequest)>;

  CheatManagerDialog(cheats::CheatSystem system,
    cheats::CheatConfiguration configuration,
    QWidget* parent = nullptr);

  void setConfigurationSink(ConfigurationSink sink);
  void setDialogService(std::shared_ptr<DialogService> service,
    std::filesystem::path initialDirectory);
  void setDebugRequestSink(DebugRequestSink sink);
  void setConfiguration(cheats::CheatConfiguration configuration);
  void presentDebugResponse(CoreDebugResponse response);
  void showDebugRequestError(
    const std::string& detail, std::uint64_t clientToken);
  [[nodiscard]] const cheats::CheatConfiguration& configuration() const noexcept;

private:
  [[nodiscard]] bool applyChanges();
  [[nodiscard]] cheats::CheatConfiguration configurationFromTable() const;
  void populateTable();
  void addDefinition();
  void importDefinitions();
  void removeSelectedDefinition();
  void requestNewSearch();
  void requestSearchFilter();
  void resetSearch();
  void updateSearchValueControl();
  void updateSearchResults();
  void addSelectedSearchResult();
  [[nodiscard]] bool requestSearchSnapshot(bool begin);
  void showValidationMessage(const QString& message);

  enum class PendingSearch {
    none,
    begin,
    filter,
  };

  cheats::CheatSystem system_;
  cheats::CheatConfiguration configuration_;
  ConfigurationSink configurationSink_;
  DebugRequestSink debugRequestSink_;
  std::shared_ptr<DialogService> dialogService_;
  std::filesystem::path importDirectory_;
  std::shared_ptr<const CoreDebugSnapshot> searchSnapshot_;
  DebugRamSearch ramSearch_;
  PendingSearch pendingSearch_{PendingSearch::none};
  DebugRamComparison pendingComparison_{DebugRamComparison::equalTo};
  DebugValueFormat pendingFormat_{DebugValueFormat::unsignedInteger};
  std::int64_t pendingValue_{0};
  QTabWidget* tabs_{nullptr};
  QTableWidget* table_{nullptr};
  QComboBox* searchComparison_{nullptr};
  QCheckBox* searchSigned_{nullptr};
  QLineEdit* searchValue_{nullptr};
  QLabel* searchCount_{nullptr};
  QTableWidget* searchResults_{nullptr};
  QPushButton* searchNewButton_{nullptr};
  QPushButton* searchFilterButton_{nullptr};
  QPushButton* searchAddButton_{nullptr};
  QLabel* validationLabel_{nullptr};
  QDialogButtonBox* buttons_{nullptr};
};

} // namespace genplusgx::ui
