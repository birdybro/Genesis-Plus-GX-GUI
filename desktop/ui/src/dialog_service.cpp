#include "genplusgx/ui/dialog_service.h"

#include "genplusgx/game_file.h"
#include "genplusgx/game_patch.h"
#include "genplusgx/ui/archive_entry_dialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStringList>

namespace genplusgx::ui {

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

QString gameFileDialogFilter()
{
  QStringList patterns;
  for (const auto extension : supportedGameExtensions()) {
    patterns.push_back(QStringLiteral("*") + QString::fromLatin1(extension));
  }
  return QObject::tr("Supported games (%1);;All files (*)").arg(patterns.join(u' '));
}

QString discFileDialogFilter()
{
  QStringList patterns;
  for (const auto extension : supportedDiscExtensions()) {
    patterns.push_back(QStringLiteral("*") + QString::fromLatin1(extension));
  }
  return QObject::tr("Sega CD / Mega CD images (%1);;All files (*)")
    .arg(patterns.join(u' '));
}

QString patchFileDialogFilter()
{
  QStringList patterns;
  for (const auto extension : supportedGamePatchExtensions()) {
    patterns.push_back(QStringLiteral("*") + QString::fromLatin1(extension));
  }
  return QObject::tr("Soft patches (%1);;All files (*)")
    .arg(patterns.join(u' '));
}

QString stateFileDialogFilter()
{
  return QObject::tr("Genesis Plus GX GUI states (*.gpgxstate);;All files (*)");
}

QString cheatFileDialogFilter()
{
  return QObject::tr(
    "Cheat lists (*.cht *.txt);;RetroArch cheat lists (*.cht);;"
    "Plain-text cheat lists (*.txt);;All files (*)");
}

QString movieFileDialogFilter()
{
  return QObject::tr("Genesis Plus GX input movies (*.gpgx-movie);;All files (*)");
}

std::optional<std::filesystem::path> DialogService::chooseDisc(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  return chooseGame(parent, initialDirectory);
}

std::optional<std::filesystem::path> DialogService::choosePatch(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseDirectory(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseArtwork(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseVideoArtwork(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseShaderPreset(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseStateImport(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseCheatImport(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseStateExport(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseRecordingDirectory(
  QWidget*,
  const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseMovieOpen(
  QWidget*, const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> DialogService::chooseMovieSave(
  QWidget*, const std::filesystem::path&)
{
  return std::nullopt;
}

std::optional<std::string> DialogService::chooseArchiveEntry(
  QWidget*,
  const std::filesystem::path&,
  const std::vector<ArchivedGameEntry>&)
{
  return std::nullopt;
}

std::optional<std::filesystem::path> QtDialogService::chooseGame(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Open Game"),
    pathToQString(initialDirectory),
    gameFileDialogFilter());
  if (selected.isEmpty()) {
    return std::nullopt;
  }
  return pathFromQString(selected);
}

std::optional<std::filesystem::path> QtDialogService::chooseDisc(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Change Sega CD / Mega CD Disc"),
    pathToQString(initialDirectory),
    discFileDialogFilter());
  if (selected.isEmpty()) {
    return std::nullopt;
  }
  return pathFromQString(selected);
}

std::optional<std::filesystem::path> QtDialogService::choosePatch(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Choose IPS, BPS, or UPS Soft Patch"),
    pathToQString(initialDirectory),
    patchFileDialogFilter());
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseDirectory(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getExistingDirectory(
    parent,
    QObject::tr("Add Game Directory"),
    pathToQString(initialDirectory),
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseArtwork(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Choose Local Box Art"),
    pathToQString(initialDirectory),
    QObject::tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)"));
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseVideoArtwork(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Choose Bezel or Overlay Artwork"),
    pathToQString(initialDirectory),
    QObject::tr("Supported artwork (*.png *.jpg *.jpeg *.bmp);;All files (*)"));
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseShaderPreset(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Load Libretro Shader Preset"),
    pathToQString(initialDirectory),
    QObject::tr("Libretro Slang presets (*.slangp);;All files (*)"));
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseStateImport(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Import Save State"),
    pathToQString(initialDirectory),
    stateFileDialogFilter());
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseCheatImport(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent,
    QObject::tr("Import Cheat List"),
    pathToQString(initialDirectory),
    cheatFileDialogFilter());
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseStateExport(
  QWidget* parent,
  const std::filesystem::path& suggestedPath)
{
  auto selected = QFileDialog::getSaveFileName(
    parent,
    QObject::tr("Export Save State"),
    pathToQString(suggestedPath),
    stateFileDialogFilter());
  if (selected.isEmpty()) {
    return std::nullopt;
  }
  if (!selected.endsWith(QStringLiteral(".gpgxstate"), Qt::CaseInsensitive)) {
    selected += QStringLiteral(".gpgxstate");
  }
  return pathFromQString(selected);
}

std::optional<std::filesystem::path> QtDialogService::chooseRecordingDirectory(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getExistingDirectory(
    parent,
    QObject::tr("Choose Recording Directory"),
    pathToQString(initialDirectory),
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseMovieOpen(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  const auto selected = QFileDialog::getOpenFileName(
    parent, QObject::tr("Open Input Movie"), pathToQString(initialDirectory),
    movieFileDialogFilter());
  return selected.isEmpty()
    ? std::nullopt
    : std::optional<std::filesystem::path>{pathFromQString(selected)};
}

std::optional<std::filesystem::path> QtDialogService::chooseMovieSave(
  QWidget* parent,
  const std::filesystem::path& suggestedPath)
{
  auto selected = QFileDialog::getSaveFileName(
    parent, QObject::tr("Save Input Movie"), pathToQString(suggestedPath),
    movieFileDialogFilter());
  if (selected.isEmpty()) {
    return std::nullopt;
  }
  if (!selected.endsWith(QStringLiteral(".gpgx-movie"), Qt::CaseInsensitive)) {
    selected += QStringLiteral(".gpgx-movie");
  }
  return pathFromQString(selected);
}

std::optional<std::string> QtDialogService::chooseArchiveEntry(
  QWidget* parent,
  const std::filesystem::path& archivePath,
  const std::vector<ArchivedGameEntry>& entries)
{
  ArchiveEntryDialog dialog{archivePath, entries, parent};
  return dialog.exec() == QDialog::Accepted
    ? dialog.selectedEntry() : std::nullopt;
}

void QtDialogService::showError(
  QWidget* parent,
  const QString& title,
  const QString& message)
{
  auto* dialog = new QMessageBox(
    QMessageBox::Critical,
    title,
    message,
    QMessageBox::Ok,
    parent);
  dialog->setObjectName(QStringLiteral("gameLoadErrorDialog"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->open();
}

} // namespace genplusgx::ui
