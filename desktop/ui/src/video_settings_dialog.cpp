#include "genplusgx/ui/video_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {
namespace {

QComboBox* combo(QWidget& parent, const char* objectName)
{
  auto* result = new QComboBox(&parent);
  result->setObjectName(QString::fromLatin1(objectName));
  return result;
}

template<typename Enum>
void addChoice(QComboBox& comboBox, const QString& label, Enum value)
{
  comboBox.addItem(label, static_cast<int>(value));
}

template<typename Enum>
Enum choice(const QComboBox& comboBox)
{
  return static_cast<Enum>(comboBox.currentData().toInt());
}

template<typename Enum>
void select(QComboBox& comboBox, Enum value)
{
  const auto index = comboBox.findData(static_cast<int>(value));
  if (index >= 0) {
    comboBox.setCurrentIndex(index);
  }
}

} // namespace

VideoSettingsDialog::VideoSettingsDialog(
  settings::VideoSettings current,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("videoSettingsDialog"));
  setWindowTitle(tr("Video Settings"));
  setModal(false);
  resize(520, 460);

  auto* root = new QVBoxLayout(this);
  auto* presentationGroup = new QGroupBox(tr("Presentation"), this);
  presentationGroup->setObjectName(QStringLiteral("videoPresentationGroup"));
  auto* presentationForm = new QFormLayout(presentationGroup);
  aspect_ = combo(*presentationGroup, "videoAspectCombo");
  addChoice(*aspect_, tr("Native pixels"), video::AspectMode::native);
  addChoice(*aspect_, tr("Force 4:3"), video::AspectMode::fourThree);
  addChoice(*aspect_, tr("Stretch to window"), video::AspectMode::stretch);
  presentationForm->addRow(tr("Aspect ratio:"), aspect_);
  scaling_ = combo(*presentationGroup, "videoScalingCombo");
  addChoice(*scaling_, tr("Fit to window"), video::ScaleMode::fit);
  addChoice(*scaling_, tr("Integer scale"), video::ScaleMode::integer);
  presentationForm->addRow(tr("Scaling:"), scaling_);
  presentationFilter_ = combo(*presentationGroup, "videoPresentationFilterCombo");
  addChoice(*presentationFilter_, tr("Nearest neighbor"), video::VideoFilter::nearest);
  addChoice(*presentationFilter_, tr("Bilinear"), video::VideoFilter::bilinear);
  presentationForm->addRow(tr("Texture filtering:"), presentationFilter_);
  root->addWidget(presentationGroup);

  auto* coreGroup = new QGroupBox(tr("Genesis Plus GX output"), this);
  coreGroup->setObjectName(QStringLiteral("coreVideoOutputGroup"));
  auto* coreForm = new QFormLayout(coreGroup);
  overscan_ = combo(*coreGroup, "coreOverscanCombo");
  addChoice(*overscan_, tr("Disabled"), CoreOverscanMode::disabled);
  addChoice(*overscan_, tr("Top and bottom"), CoreOverscanMode::vertical);
  addChoice(*overscan_, tr("Left and right"), CoreOverscanMode::horizontal);
  addChoice(*overscan_, tr("All borders"), CoreOverscanMode::full);
  coreForm->addRow(tr("Overscan borders:"), overscan_);
  ntscFilter_ = combo(*coreGroup, "coreNtscFilterCombo");
  addChoice(*ntscFilter_, tr("Disabled"), CoreNtscFilter::disabled);
  addChoice(*ntscFilter_, tr("Monochrome"), CoreNtscFilter::monochrome);
  addChoice(*ntscFilter_, tr("Composite"), CoreNtscFilter::composite);
  addChoice(*ntscFilter_, tr("S-Video"), CoreNtscFilter::sVideo);
  addChoice(*ntscFilter_, tr("RGB"), CoreNtscFilter::rgb);
  coreForm->addRow(tr("NTSC filter:"), ntscFilter_);
  interlacedRender_ = combo(*coreGroup, "coreInterlacedRenderCombo");
  addChoice(*interlacedRender_, tr("Single field"),
    CoreInterlacedRenderMode::singleField);
  addChoice(*interlacedRender_, tr("Double field"),
    CoreInterlacedRenderMode::doubleField);
  coreForm->addRow(tr("Interlaced output:"), interlacedRender_);
  gameGearExtended_ = new QCheckBox(tr("Show extended 256×192 Game Gear screen"), coreGroup);
  gameGearExtended_->setObjectName(QStringLiteral("gameGearExtendedScreenCheckBox"));
  coreForm->addRow(QString{}, gameGearExtended_);
  root->addWidget(coreGroup);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("videoSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okVideoSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelVideoSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applyVideoSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreVideoDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, &VideoSettingsDialog::apply);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
    this, &VideoSettingsDialog::restoreDefaults);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  setSettings(current);
}

void VideoSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

settings::VideoSettings VideoSettingsDialog::settings() const
{
  return {
    .aspect = choice<video::AspectMode>(*aspect_),
    .scaling = choice<video::ScaleMode>(*scaling_),
    .presentationFilter = choice<video::VideoFilter>(*presentationFilter_),
    .core = {
      .overscan = choice<CoreOverscanMode>(*overscan_),
      .ntscFilter = choice<CoreNtscFilter>(*ntscFilter_),
      .interlacedRender =
        choice<CoreInterlacedRenderMode>(*interlacedRender_),
      .gameGearExtendedScreen = gameGearExtended_->isChecked(),
    },
  };
}

void VideoSettingsDialog::setSettings(const settings::VideoSettings& value)
{
  if (!settings::validateVideoSettings(value)) {
    return;
  }
  select(*aspect_, value.aspect);
  select(*scaling_, value.scaling);
  select(*presentationFilter_, value.presentationFilter);
  select(*overscan_, value.core.overscan);
  select(*ntscFilter_, value.core.ntscFilter);
  select(*interlacedRender_, value.core.interlacedRender);
  gameGearExtended_->setChecked(value.core.gameGearExtendedScreen);
}

void VideoSettingsDialog::apply()
{
  const auto value = settings();
  if (settings::validateVideoSettings(value) && settingsSink_) {
    settingsSink_(value);
  }
}

void VideoSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultVideoSettings());
}

} // namespace genplusgx::ui
