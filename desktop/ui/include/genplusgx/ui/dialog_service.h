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
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseDisc(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  virtual void showError(QWidget* parent, const QString& title, const QString& message) = 0;
};

class QtDialogService final : public DialogService {
public:
  [[nodiscard]] std::optional<std::filesystem::path> chooseGame(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseDisc(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  void showError(QWidget* parent, const QString& title, const QString& message) override;
};

[[nodiscard]] QString gameFileDialogFilter();
[[nodiscard]] QString discFileDialogFilter();
[[nodiscard]] QString pathToQString(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromQString(const QString& path);

} // namespace genplusgx::ui
