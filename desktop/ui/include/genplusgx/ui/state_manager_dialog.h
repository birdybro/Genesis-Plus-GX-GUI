#pragma once

#include <QDialog>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace genplusgx::ui {

enum class StateSlotViewState {
  empty,
  available,
  invalid,
};

struct StateSlotView final {
  std::uint32_t slot{0};
  StateSlotViewState state{StateSlotViewState::empty};
  std::uint32_t schemaVersion{0};
  std::chrono::system_clock::time_point timestamp{};
  std::uint64_t emulatedFrameNumber{0};
  std::size_t payloadBytes{0};
  std::string name;
  std::vector<std::uint8_t> thumbnailPng;
  std::string detail;
};

enum class StateUiOperation {
  save,
  load,
  remove,
  importFile,
  exportFile,
  rename,
};

struct StateUiRequest final {
  StateUiOperation operation{StateUiOperation::save};
  std::uint32_t slot{0};
  std::filesystem::path path;
  std::string name;
};

class StateManagerDialog final : public QDialog {
  Q_OBJECT

public:
  using OperationSink = std::function<void(StateUiRequest)>;
  using SelectionSink = std::function<void(std::uint32_t)>;

  explicit StateManagerDialog(QWidget* parent = nullptr);

  void setViews(const std::array<StateSlotView, 10>& views);
  void setSelectedSlot(std::uint32_t slot);
  [[nodiscard]] std::uint32_t selectedSlot() const noexcept;
  void setSessionReady(bool ready);
  void setBusy(bool busy);
  void setOperationSink(OperationSink sink);
  void setSelectionSink(SelectionSink sink);

private:
  void dispatch(StateUiOperation operation);
  void updateSelection();
  void updateButtons();

  std::array<StateSlotView, 10> views_{};
  QTableWidget* table_{nullptr};
  QLineEdit* nameEdit_{nullptr};
  QPushButton* saveButton_{nullptr};
  QPushButton* loadButton_{nullptr};
  QPushButton* importButton_{nullptr};
  QPushButton* exportButton_{nullptr};
  QPushButton* renameButton_{nullptr};
  QPushButton* deleteButton_{nullptr};
  OperationSink operationSink_;
  SelectionSink selectionSink_;
  std::uint32_t selectedSlot_{0U};
  bool sessionReady_{false};
  bool busy_{false};
  bool updating_{false};
};

} // namespace genplusgx::ui
