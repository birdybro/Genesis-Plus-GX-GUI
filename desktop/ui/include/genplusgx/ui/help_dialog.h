#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace genplusgx::ui {

enum class HelpTopic {
  userGuide,
  keyboardShortcuts,
};

class HelpDialog final : public QDialog {
  Q_OBJECT

public:
  explicit HelpDialog(HelpTopic topic, QWidget* parent = nullptr);

  [[nodiscard]] HelpTopic topic() const noexcept;

private:
  HelpTopic topic_{HelpTopic::userGuide};
  QPlainTextEdit* content_{nullptr};
};

} // namespace genplusgx::ui
