#pragma once

#include <filesystem>
#include <optional>

class QString;
class QWidget;

namespace genplusgx::ui {

class DialogService {
public:
  virtual ~DialogService() = default;

  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseGame(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) = 0;
  virtual void showError(QWidget* parent, const QString& title, const QString& message) = 0;
};

class QtDialogService final : public DialogService {
public:
  [[nodiscard]] std::optional<std::filesystem::path> chooseGame(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  void showError(QWidget* parent, const QString& title, const QString& message) override;
};

[[nodiscard]] QString gameFileDialogFilter();
[[nodiscard]] QString pathToQString(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromQString(const QString& path);

} // namespace genplusgx::ui
