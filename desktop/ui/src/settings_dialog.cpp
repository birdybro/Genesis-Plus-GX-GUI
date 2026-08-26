#include "genplusgx/ui/settings_dialog.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <utility>

namespace genplusgx::ui {
namespace {

struct PageButton final {
  QString label;
  const char* objectName;
  SettingsPageAction action;
};

QString pathText(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

QString themeName(settings::ThemeMode theme)
{
  switch (theme) {
    case settings::ThemeMode::system:
      return QObject::tr("System default");
    case settings::ThemeMode::light:
      return QObject::tr("Light");
    case settings::ThemeMode::dark:
      return QObject::tr("Dark");
  }
  return QObject::tr("Unknown");
}

QString aspectName(video::AspectMode aspect)
{
  switch (aspect) {
    case video::AspectMode::native:
      return QObject::tr("Native");
    case video::AspectMode::fourThree:
      return QObject::tr("4:3");
    case video::AspectMode::stretch:
      return QObject::tr("Stretch");
  }
  return QObject::tr("Unknown");
}

QString scalingName(video::ScaleMode scaling)
{
  return scaling == video::ScaleMode::integer
    ? QObject::tr("Integer") : QObject::tr("Fit to window");
}

QString filterName(video::VideoFilter filter)
{
  return filter == video::VideoFilter::bilinear
    ? QObject::tr("Bilinear") : QObject::tr("Nearest neighbor");
}

QString hardwareName(CoreSystemHardware hardware)
{
  switch (hardware) {
    case CoreSystemHardware::automatic:
      return QObject::tr("Automatic");
    case CoreSystemHardware::sg1000:
      return QObject::tr("SG-1000");
    case CoreSystemHardware::sg1000II:
      return QObject::tr("SG-1000 II");
    case CoreSystemHardware::sg1000IIRamExtension:
      return QObject::tr("SG-1000 II + RAM extension");
    case CoreSystemHardware::markIII:
      return QObject::tr("Mark III");
    case CoreSystemHardware::masterSystem:
      return QObject::tr("Master System");
    case CoreSystemHardware::masterSystemII:
      return QObject::tr("Master System II");
    case CoreSystemHardware::gameGear:
      return QObject::tr("Game Gear");
    case CoreSystemHardware::genesis:
      return QObject::tr("Mega Drive / Genesis");
  }
  return QObject::tr("Unknown");
}

QString regionName(CoreSystemRegion region)
{
  switch (region) {
    case CoreSystemRegion::automatic:
      return QObject::tr("Automatic");
    case CoreSystemRegion::ntscU:
      return QObject::tr("NTSC-U");
    case CoreSystemRegion::palEurope:
      return QObject::tr("PAL Europe");
    case CoreSystemRegion::ntscJapan:
      return QObject::tr("NTSC-J");
    case CoreSystemRegion::palJapan:
      return QObject::tr("PAL Japan");
  }
  return QObject::tr("Unknown");
}

QWidget* makePage(
  SettingsDialog& owner,
  const std::function<void(SettingsPageAction)>& dispatch,
  const QString& title,
  const QString& description,
  const char* pageObjectName,
  const char* summaryObjectName,
  QLabel*& summary,
  std::initializer_list<PageButton> buttons)
{
  auto* page = new QWidget(&owner);
  page->setObjectName(QString::fromLatin1(pageObjectName));
  auto* layout = new QVBoxLayout(page);
  auto* heading = new QLabel(QStringLiteral("<h2>%1</h2>").arg(title), page);
  heading->setObjectName(
    QStringLiteral("%1Heading").arg(QString::fromLatin1(pageObjectName)));
  layout->addWidget(heading);
  auto* explanation = new QLabel(description, page);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  summary = new QLabel(page);
  summary->setObjectName(QString::fromLatin1(summaryObjectName));
  summary->setAccessibleName(QObject::tr("Current %1 settings summary").arg(title));
  summary->setTextInteractionFlags(
    Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  summary->setWordWrap(true);
  summary->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
  summary->setMargin(10);
  layout->addWidget(summary);
  for (const auto& specification : buttons) {
    auto* button = new QPushButton(specification.label, page);
    button->setObjectName(QString::fromLatin1(specification.objectName));
    button->setAccessibleDescription(
      QObject::tr("Open the detailed %1 editor.").arg(title));
    QObject::connect(button, &QPushButton::clicked, &owner,
      [dispatch, action = specification.action] { dispatch(action); });
    layout->addWidget(button, 0, Qt::AlignLeft);
  }
  layout->addStretch();
  return page;
}

} // namespace

SettingsDialog::SettingsDialog(SettingsOverview overview, QWidget* parent)
    : QDialog(parent), overview_(std::move(overview))
{
  setObjectName(QStringLiteral("settingsDialog"));
  setWindowTitle(tr("Settings"));
  setModal(false);
  resize(820, 590);

  auto* root = new QVBoxLayout(this);
  auto* introduction = new QLabel(
    tr("Choose a category, review its active values, then open the complete typed "
       "editor. Apply, Cancel, and Restore Defaults remain local to that category."),
    this);
  introduction->setObjectName(QStringLiteral("settingsIntroductionLabel"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  auto* navigation = new QHBoxLayout;
  categories_ = new QListWidget(this);
  categories_->setObjectName(QStringLiteral("settingsCategoryList"));
  categories_->setAccessibleName(tr("Settings categories"));
  categories_->setMinimumWidth(175);
  const std::array categoryNames{
    tr("General"), tr("Video"), tr("Audio"), tr("Input"),
    tr("System"), tr("BIOS"), tr("Paths"), tr("Advanced")};
  for (qsizetype index = 0; index < static_cast<qsizetype>(categoryNames.size());
       ++index) {
    auto* item = new QListWidgetItem(categoryNames[static_cast<std::size_t>(index)]);
    item->setData(Qt::UserRole, index);
    categories_->addItem(item);
  }
  navigation->addWidget(categories_);

  pages_ = new QStackedWidget(this);
  pages_->setObjectName(QStringLiteral("settingsPageStack"));
  const auto dispatch = [this](SettingsPageAction action) {
    this->dispatch(action);
  };
  pages_->addWidget(makePage(*this, dispatch, tr("General"),
    tr("Application appearance and desktop integration."),
    "generalSettingsPage", "generalSettingsSummary", generalSummary_, {
      {tr("Appearance Settings…"), "configureAppearanceButton",
        SettingsPageAction::appearance},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("Video"),
    tr("Presentation geometry, scaling, filtering, and Genesis Plus GX video output."),
    "videoSettingsPage", "videoSettingsSummary", videoSummary_, {
      {tr("Video Settings…"), "configureVideoButton", SettingsPageAction::video},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("Audio"),
    tr("Host playback, buffer latency, volume, mixer levels, filters, and chip cores."),
    "audioSettingsPage", "audioSettingsSummary", audioSummary_, {
      {tr("Audio Settings…"), "configureAudioButton", SettingsPageAction::audio},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("Input"),
    tr("Keyboard/controller profiles, player assignments, hotkeys, and emulated devices."),
    "inputSettingsPage", "inputSettingsSummary", inputSummary_, {
      {tr("Controller Configuration…"), "configureInputButton",
        SettingsPageAction::inputBindings},
      {tr("Player Assignments…"), "configureAssignmentsButton",
        SettingsPageAction::playerAssignments},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("System"),
    tr("Hardware, region, video standard, master clock, and accuracy behavior."),
    "systemSettingsPage", "systemSettingsSummary", systemSummary_, {
      {tr("System Settings…"), "configureSystemButton", SettingsPageAction::system},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("BIOS"),
    tr("Validate user-provided optional cartridge firmware and required Sega CD BIOS files."),
    "biosSettingsPage", "biosSettingsSummary", biosSummary_, {
      {tr("BIOS Settings…"), "configureBiosButton", SettingsPageAction::bios},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("Paths"),
    tr("Review platform application-data locations and configure user-selectable paths."),
    "pathsSettingsPage", "pathsSettingsSummary", pathsSummary_, {
      {tr("Screenshot Directory…"), "configureScreenshotPathButton",
        SettingsPageAction::screenshotPath},
      {tr("Game Library Directories…"), "configureLibraryPathsButton",
        SettingsPageAction::gameLibrary},
    }));
  pages_->addWidget(makePage(*this, dispatch, tr("Advanced"),
    tr("Per-game overrides and read-only runtime diagnostics for troubleshooting."),
    "advancedSettingsPage", "advancedSettingsSummary", advancedSummary_, {
      {tr("Per-Game Settings…"), "configurePerGameButton",
        SettingsPageAction::perGame},
      {tr("Log and Diagnostics…"), "openDiagnosticsButton",
        SettingsPageAction::diagnostics},
    }));
  perGameButton_ = findChild<QPushButton*>(QStringLiteral("configurePerGameButton"));
  navigation->addWidget(pages_, 1);
  root->addLayout(navigation, 1);

  connect(categories_, &QListWidget::currentRowChanged,
    pages_, &QStackedWidget::setCurrentIndex);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("settingsButtonBox"));
  buttons->button(QDialogButtonBox::Close)->setObjectName(
    QStringLiteral("closeSettingsButton"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  openPage(SettingsPage::general);
  refresh();
}

void SettingsDialog::setOverview(SettingsOverview overview)
{
  overview_ = std::move(overview);
  refresh();
}

void SettingsDialog::setActionSink(ActionSink sink)
{
  actionSink_ = std::move(sink);
}

void SettingsDialog::openPage(SettingsPage page)
{
  const auto index = static_cast<int>(page);
  if (index >= 0 && index < categories_->count()) {
    categories_->setCurrentRow(index);
  }
}

SettingsPage SettingsDialog::currentPage() const noexcept
{
  const auto row = categories_->currentRow();
  if (row < 0 || row > static_cast<int>(SettingsPage::advanced)) {
    return SettingsPage::general;
  }
  return static_cast<SettingsPage>(row);
}

void SettingsDialog::dispatch(SettingsPageAction action)
{
  if (actionSink_) {
    actionSink_(action);
  }
}

void SettingsDialog::refresh()
{
  generalSummary_->setText(
    tr("Theme: %1\nHigh-DPI policy: operating-system scaling")
      .arg(themeName(overview_.appearance.theme)));
  videoSummary_->setText(
    tr("Aspect: %1\nScaling: %2\nTexture filter: %3")
      .arg(aspectName(overview_.video.aspect),
        scalingName(overview_.video.scaling),
        filterName(overview_.video.presentationFilter)));
  audioSummary_->setText(
    tr("Playback device: %1\nLatency: %2 ms\nVolume: %3%4")
      .arg(overview_.audio.outputDeviceName.empty()
          ? tr("System default")
          : QString::fromStdString(overview_.audio.outputDeviceName))
      .arg(overview_.audio.latencyMilliseconds)
      .arg(overview_.audio.masterVolumePercent)
      .arg(overview_.audio.muted ? tr(" (muted)") : QString{}));
  inputSummary_->setText(
    tr("Active profile: %1\nConnected controllers: %2\nConfigured hotkeys: %3")
      .arg(QString::fromStdString(overview_.input.activeProfile))
      .arg(overview_.connectedControllerCount)
      .arg(overview_.input.hotkeys.size()));
  systemSummary_->setText(
    tr("Hardware: %1\nRegion: %2\nAccuracy lockups: %3\nAddress errors: %4")
      .arg(hardwareName(overview_.system.hardware), regionName(overview_.system.region),
        overview_.system.emulateIllegalAccessLockups ? tr("Enabled") : tr("Disabled"),
        overview_.system.enableAddressErrors ? tr("Enabled") : tr("Disabled")));

  std::size_t configured = 0U;
  std::size_t valid = 0U;
  for (std::size_t index = 0U; index < platform::biosSlotCount; ++index) {
    const auto slot = static_cast<platform::BiosSlot>(index);
    if (!overview_.bios.configuration.path(slot).empty()) {
      ++configured;
    }
    if (overview_.bios.validation[index].valid()) {
      ++valid;
    }
  }
  biosSummary_->setText(
    tr("Configured firmware files: %1 of %2\nValidated files: %3")
      .arg(configured).arg(platform::biosSlotCount).arg(valid));

  if (overview_.pathsAvailable) {
    pathsSummary_->setText(
      tr("Application data: %1\nConfiguration: %2\nSaves: %3\nStates: %4\n"
         "Screenshots: %5\nLibrary: %6\nLogs: %7")
        .arg(pathText(overview_.paths.root()),
          pathText(overview_.paths.configDirectory()),
          pathText(overview_.paths.savesDirectory()),
          pathText(overview_.paths.statesDirectory()),
          pathText(overview_.screenshots.directory),
          pathText(overview_.paths.libraryDirectory()),
          pathText(overview_.paths.logsDirectory())));
  } else {
    pathsSummary_->setText(tr("Platform application-data paths are unavailable."));
  }
  advancedSummary_->setText(overview_.gameLoaded
    ? tr("Per-game overrides are available for the loaded game. Diagnostics can be copied without personal secrets.")
    : tr("Load a game to configure per-game overrides. Diagnostics remain available."));
  perGameButton_->setEnabled(overview_.gameLoaded);
}

} // namespace genplusgx::ui
