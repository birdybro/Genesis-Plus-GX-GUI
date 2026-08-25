#include "genplusgx/ui/help_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace genplusgx::ui {
namespace {

QString userGuideText()
{
  return HelpDialog::tr(
    "Getting started\n"
    "\n"
    "Open a supported cartridge ROM or Sega CD image with File → Open Game, "
    "drop it onto the display, or pass it on the command line. Sega CD titles "
    "require a user-provided regional BIOS configured under Tools → BIOS "
    "Settings.\n"
    "\n"
    "Use Input → Controller Configuration to map keyboard and SDL gamepad "
    "controls. Video, audio, system, save-state, screenshot, cheat, library, "
    "and per-game options are available from the standard menus.\n"
    "\n"
    "Save data is loaded and written automatically in the platform application "
    "data directory. No ROMs or proprietary Sega firmware are included. The "
    "complete guide is installed as docs/USER_GUIDE.md with source builds.");
}

QString keyboardShortcutText()
{
  return HelpDialog::tr(
    "Open game                 Ctrl+O\n"
    "Close game                Ctrl+W\n"
    "Game library              Ctrl+L\n"
    "Pause / resume            Space\n"
    "Hard reset                Ctrl+R\n"
    "Fast forward toggle       Tab\n"
    "Frame advance             N (while paused)\n"
    "Save state                F5\n"
    "Load state                F8\n"
    "Select state slot         Ctrl+0 … Ctrl+9\n"
    "Previous / next slot      Ctrl+[ / Ctrl+]\n"
    "Delete selected state     Ctrl+Delete\n"
    "Screenshot                F12\n"
    "Fullscreen                Alt+Return\n"
    "Mute                      M\n"
    "Volume up / down          + / -\n"
    "Preferences               Platform standard shortcut\n"
    "Quit                      Platform standard shortcut");
}

} // namespace

HelpDialog::HelpDialog(HelpTopic topic, QWidget* parent)
  : QDialog(parent), topic_(topic)
{
  const bool shortcuts = topic_ == HelpTopic::keyboardShortcuts;
  setObjectName(shortcuts ? QStringLiteral("keyboardShortcutsDialog")
                          : QStringLiteral("userGuideDialog"));
  setWindowTitle(shortcuts ? tr("Keyboard Shortcuts") : tr("User Guide"));
  setModal(true);
  resize(640, 480);

  auto* layout = new QVBoxLayout(this);
  auto* heading = new QLabel(windowTitle(), this);
  heading->setObjectName(shortcuts
      ? QStringLiteral("keyboardShortcutsHeading")
      : QStringLiteral("userGuideHeading"));
  heading->setAccessibleName(windowTitle());
  layout->addWidget(heading);

  content_ = new QPlainTextEdit(this);
  content_->setObjectName(shortcuts
      ? QStringLiteral("keyboardShortcutsContent")
      : QStringLiteral("userGuideContent"));
  content_->setAccessibleName(shortcuts
      ? tr("Keyboard shortcut reference") : tr("Genesis Plus GX GUI user guide"));
  content_->setReadOnly(true);
  content_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  content_->setPlainText(shortcuts ? keyboardShortcutText() : userGuideText());
  layout->addWidget(content_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(shortcuts
      ? QStringLiteral("keyboardShortcutsButtonBox")
      : QStringLiteral("userGuideButtonBox"));
  if (auto* close = buttons->button(QDialogButtonBox::Close)) {
    close->setObjectName(shortcuts
        ? QStringLiteral("closeKeyboardShortcutsButton")
        : QStringLiteral("closeUserGuideButton"));
  }
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

HelpTopic HelpDialog::topic() const noexcept
{
  return topic_;
}

} // namespace genplusgx::ui
