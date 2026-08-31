#pragma once

#include "genplusgx/core_debug.h"

#include <QMainWindow>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace genplusgx::ui {

enum class DebugControlOperation {
  pause,
  resume,
  frameAdvance,
  hardReset,
  softReset,
};

enum class DebugStateOperation {
  save,
  load,
  remove,
};

class DebugToolsWindow final : public QMainWindow {
  Q_OBJECT

public:
  using RequestSink = std::function<bool(CoreDebugRequest)>;
  using ControlSink = std::function<void(DebugControlOperation)>;
  using StateSink = std::function<void(DebugStateOperation, std::uint32_t)>;

  explicit DebugToolsWindow(QWidget* parent = nullptr);

  void setRequestSink(RequestSink sink);
  void setControlSink(ControlSink sink);
  void setStateSink(StateSink sink);
  void setGameLoaded(bool loaded);
  void setPaused(bool paused);
  void presentResponse(CoreDebugResponse response);
  void showRequestError(const std::string& detail);
  [[nodiscard]] std::shared_ptr<const CoreDebugSnapshot> snapshot() const;
  void requestRefresh();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void buildToolbar();
  void buildCpuPage();
  void buildMemoryPage();
  void buildVdpPage();
  void buildSoundPage();
  void buildInputPage();
  void buildStatePage();
  void updateAllViews();
  void updateCpuViews();
  void updateMemoryRegion();
  void requestMemory();
  void updateMemoryView(const CoreDebugResponse& response);
  void writeMemory();
  void updateVdpViews();
  void updateSoundViews();
  void updateInputView();
  void applyM68kEdit(int row, int column);
  void applyZ80Edit(int row, int column);
  void applyVdpEdit(int row, int column);
  [[nodiscard]] bool submit(CoreDebugRequest request);
  void setStatus(const QString& text, int timeout = 0);

  RequestSink requestSink_;
  ControlSink controlSink_;
  StateSink stateSink_;
  std::shared_ptr<const CoreDebugSnapshot> snapshot_;
  QTimer* refreshTimer_{nullptr};
  QAction* pauseAction_{nullptr};
  QAction* resumeAction_{nullptr};
  QAction* frameAdvanceAction_{nullptr};
  QAction* hardResetAction_{nullptr};
  QAction* softResetAction_{nullptr};
  QAction* refreshAction_{nullptr};
  QTableWidget* m68kRegisters_{nullptr};
  QTableWidget* z80Registers_{nullptr};
  QComboBox* memoryRegion_{nullptr};
  QSpinBox* memoryOffset_{nullptr};
  QSpinBox* memoryLength_{nullptr};
  QPlainTextEdit* memoryView_{nullptr};
  QLineEdit* memoryWriteBytes_{nullptr};
  QPushButton* memoryWriteButton_{nullptr};
  QTableWidget* vdpRegisters_{nullptr};
  QTableWidget* palette_{nullptr};
  QLabel* tiles_{nullptr};
  QTableWidget* sprites_{nullptr};
  QComboBox* planeSelector_{nullptr};
  QLabel* planeImage_{nullptr};
  QTableWidget* scroll_{nullptr};
  QTableWidget* fmRegisters_{nullptr};
  QTableWidget* psgRegisters_{nullptr};
  QTableWidget* inputState_{nullptr};
  QComboBox* stateSlot_{nullptr};
  QWidget* statePage_{nullptr};
  bool gameLoaded_{false};
  bool paused_{false};
  bool snapshotPending_{false};
  bool memoryPending_{false};
  bool updating_{false};
};

} // namespace genplusgx::ui
