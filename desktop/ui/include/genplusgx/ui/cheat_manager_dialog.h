#pragma once

#include "genplusgx/cheats/cheat_manager.h"

#include <QDialog>

#include <functional>

class QDialogButtonBox;
class QLabel;
class QTableWidget;

namespace genplusgx::ui {

class CheatManagerDialog final : public QDialog {
  Q_OBJECT

public:
  using ConfigurationSink =
    std::function<PersistenceStatus(const cheats::CheatConfiguration&)>;

  CheatManagerDialog(cheats::CheatSystem system,
    cheats::CheatConfiguration configuration,
    QWidget* parent = nullptr);

  void setConfigurationSink(ConfigurationSink sink);
  void setConfiguration(cheats::CheatConfiguration configuration);
  [[nodiscard]] const cheats::CheatConfiguration& configuration() const noexcept;

private:
  [[nodiscard]] bool applyChanges();
  [[nodiscard]] cheats::CheatConfiguration configurationFromTable() const;
  void populateTable();
  void addDefinition();
  void removeSelectedDefinition();
  void showValidationMessage(const QString& message);

  cheats::CheatSystem system_;
  cheats::CheatConfiguration configuration_;
  ConfigurationSink configurationSink_;
  QTableWidget* table_{nullptr};
  QLabel* validationLabel_{nullptr};
  QDialogButtonBox* buttons_{nullptr};
};

} // namespace genplusgx::ui
