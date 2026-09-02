#pragma once

#include "genplusgx/movies/input_movie.h"

#include <QDialog>

#include <array>

class QAbstractTableModel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QSpinBox;
class QTableView;
class QTextEdit;

namespace genplusgx::ui {

class InputMovieEditorDialog final : public QDialog {
  Q_OBJECT

public:
  explicit InputMovieEditorDialog(
    movies::InputMovie movie, QWidget* parent = nullptr);

  [[nodiscard]] const movies::InputMovie& movie() const noexcept;

private:
  void loadEditor();
  void storeMetadata();
  void refreshTimeline(std::size_t selectedFrame);
  [[nodiscard]] InputSnapshot editedInput() const;
  void applyFrame();
  void insertFrame();
  void duplicateFrame();
  void deleteFrame();
  void truncateAfter();

  movies::InputMovie movie_;
  QAbstractTableModel* model_{nullptr};
  QLabel* summary_{nullptr};
  QTableView* timeline_{nullptr};
  QSpinBox* frame_{nullptr};
  QComboBox* player_{nullptr};
  QCheckBox* connected_{nullptr};
  std::array<QCheckBox*, 12U> buttons_{};
  QSpinBox* analogX_{nullptr};
  QSpinBox* analogY_{nullptr};
  QLineEdit* author_{nullptr};
  QTextEdit* notes_{nullptr};
};

} // namespace genplusgx::ui
