#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genplusgx {
struct ArchivedGameEntry;
}

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
  [[nodiscard]] virtual std::optional<std::filesystem::path> choosePatch(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseVideoArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseShaderPreset(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseStateImport(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseCheatImport(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseStateExport(
    QWidget* parent,
    const std::filesystem::path& suggestedPath);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseRecordingDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseMovieOpen(
    QWidget* parent,
    const std::filesystem::path& initialDirectory);
  [[nodiscard]] virtual std::optional<std::filesystem::path> chooseMovieSave(
    QWidget* parent,
    const std::filesystem::path& suggestedPath);
  [[nodiscard]] virtual std::optional<std::string> chooseArchiveEntry(
    QWidget* parent,
    const std::filesystem::path& archivePath,
    const std::vector<ArchivedGameEntry>& entries);
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
  [[nodiscard]] std::optional<std::filesystem::path> choosePatch(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseVideoArtwork(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseShaderPreset(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseStateImport(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseCheatImport(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseStateExport(
    QWidget* parent,
    const std::filesystem::path& suggestedPath) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseRecordingDirectory(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseMovieOpen(
    QWidget* parent,
    const std::filesystem::path& initialDirectory) override;
  [[nodiscard]] std::optional<std::filesystem::path> chooseMovieSave(
    QWidget* parent,
    const std::filesystem::path& suggestedPath) override;
  [[nodiscard]] std::optional<std::string> chooseArchiveEntry(
    QWidget* parent,
    const std::filesystem::path& archivePath,
    const std::vector<ArchivedGameEntry>& entries) override;
  void showError(QWidget* parent, const QString& title, const QString& message) override;
};

[[nodiscard]] QString gameFileDialogFilter();
[[nodiscard]] QString discFileDialogFilter();
[[nodiscard]] QString patchFileDialogFilter();
[[nodiscard]] QString stateFileDialogFilter();
[[nodiscard]] QString cheatFileDialogFilter();
[[nodiscard]] QString movieFileDialogFilter();
[[nodiscard]] QString pathToQString(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromQString(const QString& path);

} // namespace genplusgx::ui
