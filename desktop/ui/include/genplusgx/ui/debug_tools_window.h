#pragma once

#include "genplusgx/core_debug.h"
#include "genplusgx/debug_analysis.h"

#include <QMainWindow>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
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

struct DebugMemoryWatch final {
  CoreDebugMemoryRegion region{CoreDebugMemoryRegion::m68kRam};
  std::uint32_t offset{0};
  DebugValueWidth width{DebugValueWidth::byte};
  DebugValueFormat format{DebugValueFormat::unsignedInteger};
  std::uint32_t previousValue{0};
  bool initialized{false};
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
  void showRequestError(const std::string& detail, std::uint64_t clientToken);
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
  void buildAnalysisPage();
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
  void beginRamSearch();
  void filterRamSearch();
  void resetRamSearch();
  void updateRamSearchTable();
  void updateSearchValueControl();
  void updateWatchAddressRange();
  void addMemoryWatch();
  void removeMemoryWatch();
  void updateMemoryWatches();
  void updateBreakpointAddressRange();
  void addFrameBreakpoint();
  void removeFrameBreakpoint();
  void updateBreakpointTable();
  [[nodiscard]] bool submitFrameBreakpoints();
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
  QTabWidget* tabs_{nullptr};
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
  QWidget* analysisPage_{nullptr};
  QTabWidget* analysisTabs_{nullptr};
  QComboBox* ramSearchRegion_{nullptr};
  QComboBox* ramSearchWidth_{nullptr};
  QComboBox* ramSearchComparison_{nullptr};
  QCheckBox* ramSearchSigned_{nullptr};
  QLineEdit* ramSearchValue_{nullptr};
  QLabel* ramSearchCount_{nullptr};
  QTableWidget* ramSearchResults_{nullptr};
  QComboBox* watchRegion_{nullptr};
  QSpinBox* watchAddress_{nullptr};
  QComboBox* watchWidth_{nullptr};
  QCheckBox* watchSigned_{nullptr};
  QTableWidget* watchTable_{nullptr};
  QComboBox* breakpointCpu_{nullptr};
  QSpinBox* breakpointAddress_{nullptr};
  QTableWidget* breakpointTable_{nullptr};
  QComboBox* stateSlot_{nullptr};
  QWidget* statePage_{nullptr};
  bool gameLoaded_{false};
  bool paused_{false};
  bool snapshotPending_{false};
  bool memoryPending_{false};
  bool updating_{false};
  DebugRamSearch ramSearch_;
  CoreDebugMemoryRegion ramSearchMemoryRegion_{CoreDebugMemoryRegion::m68kRam};
  DebugValueFormat ramSearchFormat_{DebugValueFormat::unsignedInteger};
  std::vector<DebugMemoryWatch> watches_;
  std::vector<CoreDebugBreakpoint> frameBreakpoints_;
};

} // namespace genplusgx::ui
