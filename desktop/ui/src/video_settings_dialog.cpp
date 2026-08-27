#include "genplusgx/ui/video_settings_dialog.h"

#include "genplusgx/ui/dialog_service.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <utility>
#include <algorithm>

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

  auto* shaderGroup = new QGroupBox(tr("CRT and Libretro shaders"), this);
  shaderGroup->setObjectName(QStringLiteral("videoShaderGroup"));
  auto* shaderForm = new QFormLayout(shaderGroup);
  shaderMode_ = combo(*shaderGroup, "shaderModeCombo");
  addChoice(*shaderMode_, tr("Off"), video::ShaderMode::disabled);
  addChoice(*shaderMode_, tr("Built-in CRT"), video::ShaderMode::builtinCrt);
  addChoice(*shaderMode_, tr("Custom Libretro .slangp preset"),
    video::ShaderMode::libretroPreset);
  shaderForm->addRow(tr("Shader:"), shaderMode_);
  shaderPath_ = new QLabel(shaderGroup);
  shaderPath_->setObjectName(QStringLiteral("shaderPresetPathLabel"));
  shaderPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  shaderPath_->setWordWrap(true);
  shaderForm->addRow(tr("Preset:"), shaderPath_);
  chooseShaderPreset_ = new QPushButton(tr("Browse…"), shaderGroup);
  chooseShaderPreset_->setObjectName(QStringLiteral("chooseShaderPresetButton"));
  shaderForm->addRow(QString{}, chooseShaderPreset_);
  shaderValidation_ = new QLabel(shaderGroup);
  shaderValidation_->setObjectName(QStringLiteral("shaderValidationLabel"));
  shaderValidation_->setWordWrap(true);
  shaderValidation_->setAccessibleName(tr("Shader validation status"));
  shaderForm->addRow(QString{}, shaderValidation_);
  connect(shaderMode_, &QComboBox::currentIndexChanged,
    this, [this] {
      updateShaderControls();
      loadShaderParameters();
    });
  connect(chooseShaderPreset_, &QPushButton::clicked,
    this, &VideoSettingsDialog::choosePreset);
#if !GENPLUSGX_HAS_LIBRETRO_SHADERS
  shaderGroup->setEnabled(false);
  shaderGroup->setToolTip(
    tr("This build does not include Libretro shader support."));
#endif
  root->addWidget(shaderGroup);

  shaderParametersGroup_ = new QGroupBox(tr("Shader parameters"), this);
  shaderParametersGroup_->setObjectName(
    QStringLiteral("shaderParametersGroup"));
  auto* parameterScroll = new QScrollArea(shaderParametersGroup_);
  parameterScroll->setObjectName(QStringLiteral("shaderParametersScrollArea"));
  parameterScroll->setWidgetResizable(true);
  parameterScroll->setMaximumHeight(190);
  auto* parameterContents = new QWidget(parameterScroll);
  parameterContents->setObjectName(QStringLiteral("shaderParameterContents"));
  shaderParametersForm_ = new QFormLayout(parameterContents);
  parameterScroll->setWidget(parameterContents);
  auto* parameterGroupLayout = new QVBoxLayout(shaderParametersGroup_);
  parameterGroupLayout->addWidget(parameterScroll);
  root->addWidget(shaderParametersGroup_);

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
    if (apply()) {
      accept();
    }
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  setSettings(current);
}

void VideoSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

void VideoSettingsDialog::setPresetChooser(PresetChooser chooser)
{
  presetChooser_ = std::move(chooser);
}

settings::VideoSettings VideoSettingsDialog::settings() const
{
  auto shader = shaderConfiguration_;
  shader.mode = choice<video::ShaderMode>(*shaderMode_);
  if (shader.mode != video::ShaderMode::libretroPreset) {
    shader.presetPath.clear();
  }
  shader.parameters.clear();
  if (shader.mode != video::ShaderMode::disabled) {
    for (std::size_t index = 0U;
         index < shaderParameterEditors_.size(); ++index) {
      shader.parameters.push_back({
        .name = shaderParameterMetadata_[index].name,
        .value = static_cast<float>(shaderParameterEditors_[index]->value()),
      });
    }
  }
  return {
    .aspect = choice<video::AspectMode>(*aspect_),
    .scaling = choice<video::ScaleMode>(*scaling_),
    .presentationFilter = choice<video::VideoFilter>(*presentationFilter_),
    .shader = std::move(shader),
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
  shaderConfiguration_ = value.shader;
  select(*shaderMode_, value.shader.mode);
  select(*overscan_, value.core.overscan);
  select(*ntscFilter_, value.core.ntscFilter);
  select(*interlacedRender_, value.core.interlacedRender);
  gameGearExtended_->setChecked(value.core.gameGearExtendedScreen);
  updateShaderControls();
  loadShaderParameters();
}

bool VideoSettingsDialog::apply()
{
  const auto value = settings();
  if (!settings::validateVideoSettings(value)) {
    return false;
  }
  return !settingsSink_ || settingsSink_(value);
}

void VideoSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultVideoSettings());
}

void VideoSettingsDialog::choosePreset()
{
  const auto initialDirectory = shaderConfiguration_.presetPath.has_parent_path()
    ? shaderConfiguration_.presetPath.parent_path()
    : std::filesystem::path{};
  const auto selected = presetChooser_ ? presetChooser_(initialDirectory)
                                       : std::nullopt;
  if (!selected) {
    return;
  }
  shaderConfiguration_.mode = video::ShaderMode::libretroPreset;
  shaderConfiguration_.presetPath = *selected;
  shaderConfiguration_.parameters.clear();
  select(*shaderMode_, video::ShaderMode::libretroPreset);
  updateShaderControls();
  loadShaderParameters();
}

void VideoSettingsDialog::updateShaderControls()
{
  const auto mode = choice<video::ShaderMode>(*shaderMode_);
  chooseShaderPreset_->setEnabled(mode == video::ShaderMode::libretroPreset);
  if (mode == video::ShaderMode::builtinCrt) {
    shaderPath_->setText(tr("Bundled Genesis Plus GX CRT preset"));
  } else if (mode == video::ShaderMode::libretroPreset &&
             !shaderConfiguration_.presetPath.empty()) {
    shaderPath_->setText(pathToQString(shaderConfiguration_.presetPath));
  } else {
    shaderPath_->setText(tr("None"));
  }
}

void VideoSettingsDialog::clearShaderParameters()
{
  shaderParameterMetadata_.clear();
  shaderParameterEditors_.clear();
  while (shaderParametersForm_->rowCount() > 0) {
    shaderParametersForm_->removeRow(0);
  }
}

void VideoSettingsDialog::loadShaderParameters()
{
  clearShaderParameters();
  auto configuration = shaderConfiguration_;
  configuration.mode = choice<video::ShaderMode>(*shaderMode_);
  if (configuration.mode != video::ShaderMode::libretroPreset) {
    configuration.presetPath.clear();
  }
  if (configuration.mode == video::ShaderMode::disabled) {
    shaderValidation_->clear();
    shaderParametersGroup_->hide();
    return;
  }
  configuration.parameters.clear();
  const auto inspection = video::inspectShaderConfiguration(configuration);
  if (!inspection.success) {
    shaderValidation_->setText(QString::fromStdString(inspection.error));
    shaderParametersGroup_->hide();
    return;
  }
  shaderValidation_->setText(tr("Preset is valid and ready to use."));
  shaderParameterMetadata_ = inspection.parameters;
  for (const auto& parameter : shaderParameterMetadata_) {
    auto* editor = new QDoubleSpinBox(shaderParametersGroup_);
    editor->setObjectName(QStringLiteral("shaderParameter_%1")
      .arg(QString::fromStdString(parameter.name)));
    editor->setAccessibleName(QString::fromStdString(parameter.description));
    editor->setDecimals(4);
    editor->setRange(parameter.minimum, parameter.maximum);
    editor->setSingleStep(parameter.step > 0.0F ? parameter.step : 0.01F);
    const auto override = std::find_if(
      shaderConfiguration_.parameters.cbegin(),
      shaderConfiguration_.parameters.cend(),
      [&parameter](const video::ShaderParameterValue& value) {
        return value.name == parameter.name;
      });
    editor->setValue(override != shaderConfiguration_.parameters.cend()
      ? override->value : parameter.initial);
    shaderParametersForm_->addRow(
      QString::fromStdString(parameter.description) + QStringLiteral(":"),
      editor);
    shaderParameterEditors_.push_back(editor);
  }
  shaderParametersGroup_->setVisible(!shaderParameterEditors_.empty());
}

} // namespace genplusgx::ui
