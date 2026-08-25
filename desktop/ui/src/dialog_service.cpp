#include "genplusgx/ui/dialog_service.h"

#include "genplusgx/game_file.h"

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

std::optional<std::filesystem::path> DialogService::chooseDisc(
  QWidget* parent,
  const std::filesystem::path& initialDirectory)
{
  return chooseGame(parent, initialDirectory);
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
