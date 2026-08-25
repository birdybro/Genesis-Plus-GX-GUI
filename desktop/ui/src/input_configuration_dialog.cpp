#include "genplusgx/ui/input_configuration_dialog.h"

#include "genplusgx/ui/binding_capture_button.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace genplusgx::ui {
namespace {

struct InputDescription final {
  InputButton input;
  const char* name;
  const char* suffix;
};

constexpr std::array inputDescriptions{
  InputDescription{InputButton::up, "Up", "Up"},
  InputDescription{InputButton::down, "Down", "Down"},
  InputDescription{InputButton::left, "Left", "Left"},
  InputDescription{InputButton::right, "Right", "Right"},
  InputDescription{InputButton::a, "A", "A"},
  InputDescription{InputButton::b, "B", "B"},
  InputDescription{InputButton::c, "C", "C"},
  InputDescription{InputButton::x, "X", "X"},
  InputDescription{InputButton::y, "Y", "Y"},
  InputDescription{InputButton::z, "Z", "Z"},
  InputDescription{InputButton::mode, "Mode", "Mode"},
  InputDescription{InputButton::start, "Start", "Start"},
};

struct DeviceDescription final {
  input::LogicalDeviceType type;
  const char* name;
};

constexpr std::array deviceDescriptions{
  DeviceDescription{input::LogicalDeviceType::none, "None"},
  DeviceDescription{input::LogicalDeviceType::pad3Button, "Genesis 3-button pad"},
  DeviceDescription{input::LogicalDeviceType::pad6Button, "Genesis 6-button pad"},
  DeviceDescription{input::LogicalDeviceType::segaMouse, "Sega Mouse"},
  DeviceDescription{input::LogicalDeviceType::lightGun, "Light gun"},
  DeviceDescription{input::LogicalDeviceType::paddle, "Paddle"},
  DeviceDescription{input::LogicalDeviceType::sportsPad, "Sports Pad"},
  DeviceDescription{input::LogicalDeviceType::xe1Ap, "XE-1AP"},
  DeviceDescription{input::LogicalDeviceType::pico, "Pico tablet"},
  DeviceDescription{input::LogicalDeviceType::terebiOekaki, "Terebi Oekaki"},
  DeviceDescription{input::LogicalDeviceType::graphicBoard, "Graphic Board"},
  DeviceDescription{input::LogicalDeviceType::activator, "Activator"},
};

struct AxisDescription final {
  SDL_GamepadAxis axis;
  int direction;
  InputButton defaultInput;
  const char* name;
  const char* suffix;
};

constexpr std::array axisDescriptions{
  AxisDescription{SDL_GAMEPAD_AXIS_LEFTX, -1, InputButton::left,
    "Left stick left", "LeftXNegative"},
  AxisDescription{SDL_GAMEPAD_AXIS_LEFTX, 1, InputButton::right,
    "Left stick right", "LeftXPositive"},
  AxisDescription{SDL_GAMEPAD_AXIS_LEFTY, -1, InputButton::up,
    "Left stick up", "LeftYNegative"},
  AxisDescription{SDL_GAMEPAD_AXIS_LEFTY, 1, InputButton::down,
    "Left stick down", "LeftYPositive"},
};

QString inputDisplayName(InputButton input)
{
  const auto found = std::find_if(inputDescriptions.begin(), inputDescriptions.end(),
    [input](const auto& description) { return description.input == input; });
  return found == inputDescriptions.end()
    ? QObject::tr("Unknown") : QObject::tr(found->name);
}

QString profileNameForIndex(const input::InputConfiguration& configuration, int number)
{
  while (true) {
    const auto candidate = QObject::tr("Profile %1").arg(number++);
    const bool exists = std::any_of(configuration.profiles.begin(),
      configuration.profiles.end(), [&candidate](const auto& profile) {
        return QString::fromStdString(profile.name) == candidate;
      });
    if (!exists) {
      return candidate;
    }
  }
}

} // namespace

InputConfigurationDialog::InputConfigurationDialog(
  input::InputConfiguration configuration,
  std::vector<input::ControllerInfo> controllers,
  QWidget* parent)
  : QDialog(parent),
    configuration_(std::move(configuration)),
    controllers_(std::move(controllers)),
    reservedHotkeys_(input::defaultReservedHotkeyKeys())
{
  if (!input::validateInputConfiguration(configuration_)) {
    configuration_ = input::defaultInputConfiguration();
  }
  setObjectName(QStringLiteral("inputConfigurationDialog"));
  setWindowTitle(tr("Input Configuration"));
  setModal(true);
  setMinimumSize(720, 620);

  auto* layout = new QVBoxLayout(this);
  buildProfileRow(*layout);

  tabs_ = new QTabWidget(this);
  tabs_->setObjectName(QStringLiteral("inputConfigurationTabs"));
  tabs_->addTab(buildBindingsPage(), tr("Bindings"));
  tabs_->addTab(buildAssignmentsPage(), tr("Player Assignments"));
  tabs_->addTab(buildAdvancedPage(), tr("Advanced Devices"));
  layout->addWidget(tabs_, 1);

  conflictLabel_ = new QLabel(this);
  conflictLabel_->setObjectName(QStringLiteral("inputConflictLabel"));
  conflictLabel_->setWordWrap(true);
  conflictLabel_->setAccessibleName(tr("Input configuration validation message"));
  conflictLabel_->hide();
  layout->addWidget(conflictLabel_);

  buttons_ = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
      QDialogButtonBox::RestoreDefaults,
    this);
  buttons_->setObjectName(QStringLiteral("inputConfigurationButtonBox"));
  buttons_->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("inputOkButton"));
  buttons_->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("inputCancelButton"));
  buttons_->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("inputApplyButton"));
  buttons_->button(QDialogButtonBox::RestoreDefaults)
    ->setObjectName(QStringLiteral("inputRestoreDefaultsButton"));
  connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(applyChanges()); });
  connect(buttons_->button(QDialogButtonBox::Ok), &QPushButton::clicked,
    this, [this] {
      if (applyChanges()) {
        accept();
      }
    });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons_->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
    this, &InputConfigurationDialog::restoreCurrentProfile);
  layout->addWidget(buttons_);

  refreshProfileList();
  refreshEditor();
}

void InputConfigurationDialog::setConfigurationSink(ConfigurationSink sink)
{
  configurationSink_ = std::move(sink);
}

void InputConfigurationDialog::setAssignmentSink(AssignmentSink sink)
{
  assignmentSink_ = std::move(sink);
}

void InputConfigurationDialog::setControllers(
  std::vector<input::ControllerInfo> controllers)
{
  controllers_ = std::move(controllers);
  refreshAssignments();
}

void InputConfigurationDialog::openTab(InputConfigurationTab tab)
{
  tabs_->setCurrentIndex(static_cast<int>(tab));
}

input::InputConfiguration InputConfigurationDialog::configuration() const
{
  return configuration_;
}

bool InputConfigurationDialog::captureControllerButton(SDL_GamepadButton button)
{
  const auto found = std::find_if(controllerButtons_.begin(), controllerButtons_.end(),
    [](const BindingCaptureButton* capture) { return capture->isCapturing(); });
  return found != controllerButtons_.end() && (*found)->captureControllerButton(button);
}

bool InputConfigurationDialog::applyChanges()
{
  clearConflict();
  const auto validation = input::validateInputConfiguration(configuration_);
  if (!validation) {
    showConflict(QString::fromStdString(validation.message));
    return false;
  }
  std::set<std::size_t> assignedPlayers;
  std::vector<std::pair<std::uint32_t, std::size_t>> requestedAssignments;
  requestedAssignments.reserve(controllers_.size());
  for (const auto* combo : assignmentCombos_) {
    const auto player = static_cast<std::size_t>(combo->currentData().toUInt());
    if (!assignedPlayers.insert(player).second) {
      showConflict(tr("Each connected controller must have a unique player assignment."));
      return false;
    }
  }
  for (std::size_t index = 0U; index < controllers_.size(); ++index) {
    requestedAssignments.emplace_back(controllers_[index].instanceId,
      static_cast<std::size_t>(assignmentCombos_[index]->currentData().toUInt()));
  }
  if (configurationSink_) {
    configurationSink_(configuration_);
  }
  if (assignmentSink_) {
    for (const auto& [instanceId, player] : requestedAssignments) {
      assignmentSink_(instanceId, player);
    }
  }
  return true;
}

void InputConfigurationDialog::buildProfileRow(QVBoxLayout& layout)
{
  auto* row = new QWidget(this);
  row->setObjectName(QStringLiteral("inputProfileRow"));
  auto* rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);
  auto* label = new QLabel(tr("Profile:"), row);
  profileCombo_ = new QComboBox(row);
  profileCombo_->setObjectName(QStringLiteral("inputProfileCombo"));
  label->setBuddy(profileCombo_);
  auto* add = new QPushButton(tr("New Profile"), row);
  add->setObjectName(QStringLiteral("addInputProfileButton"));
  deleteProfileButton_ = new QPushButton(tr("Delete Profile"), row);
  deleteProfileButton_->setObjectName(QStringLiteral("deleteInputProfileButton"));
  rowLayout->addWidget(label);
  rowLayout->addWidget(profileCombo_, 1);
  rowLayout->addWidget(add);
  rowLayout->addWidget(deleteProfileButton_);
  connect(profileCombo_, &QComboBox::currentIndexChanged,
    this, &InputConfigurationDialog::selectProfile);
  connect(add, &QPushButton::clicked, this, &InputConfigurationDialog::addProfile);
  connect(deleteProfileButton_, &QPushButton::clicked,
    this, &InputConfigurationDialog::deleteProfile);
  layout.addWidget(row);
}

QWidget* InputConfigurationDialog::buildBindingsPage()
{
  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("inputBindingsPage"));
  auto* layout = new QGridLayout(page);
  layout->addWidget(new QLabel(tr("Emulated control"), page), 0, 0);
  layout->addWidget(new QLabel(tr("Keyboard"), page), 0, 1);
  layout->addWidget(new QLabel(tr("Controller"), page), 0, 2);
  keyboardButtons_.reserve(inputDescriptions.size());
  controllerButtons_.reserve(inputDescriptions.size());
  for (std::size_t index = 0U; index < inputDescriptions.size(); ++index) {
    const auto& description = inputDescriptions[index];
    auto* label = new QLabel(tr(description.name), page);
    auto* keyboard = new BindingCaptureButton(
      BindingCaptureKind::keyboard, description.input, page);
    auto* controller = new BindingCaptureButton(
      BindingCaptureKind::controller, description.input, page);
    keyboard->setObjectName(QStringLiteral("keyboardBinding%1Button")
        .arg(QString::fromLatin1(description.suffix)));
    controller->setObjectName(QStringLiteral("controllerBinding%1Button")
        .arg(QString::fromLatin1(description.suffix)));
    label->setBuddy(keyboard);
    connect(keyboard, &QPushButton::clicked, this,
      [this, keyboard] { cancelOtherCaptures(keyboard); });
    connect(controller, &QPushButton::clicked, this,
      [this, controller] { cancelOtherCaptures(controller); });
    connect(keyboard, &BindingCaptureButton::keyboardCaptured, this,
      [this, input = description.input](int key) { keyboardCaptured(input, key); });
    connect(controller, &BindingCaptureButton::controllerCaptured, this,
      [this, input = description.input](int button) {
        controllerCaptured(input, static_cast<SDL_GamepadButton>(button));
      });
    layout->addWidget(label, static_cast<int>(index + 1U), 0);
    layout->addWidget(keyboard, static_cast<int>(index + 1U), 1);
    layout->addWidget(controller, static_cast<int>(index + 1U), 2);
    keyboardButtons_.push_back(keyboard);
    controllerButtons_.push_back(controller);
  }
  layout->setRowStretch(static_cast<int>(inputDescriptions.size() + 1U), 1);
  return page;
}

QWidget* InputConfigurationDialog::buildAssignmentsPage()
{
  assignmentsPage_ = new QWidget(this);
  assignmentsPage_->setObjectName(QStringLiteral("playerAssignmentsPage"));
  assignmentsLayout_ = new QFormLayout(assignmentsPage_);
  refreshAssignments();
  return assignmentsPage_;
}

void InputConfigurationDialog::refreshAssignments()
{
  if (assignmentsLayout_ == nullptr) {
    return;
  }
  while (auto* item = assignmentsLayout_->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  assignmentCombos_.clear();
  if (controllers_.empty()) {
    auto* empty = new QLabel(
      tr("No SDL-compatible controllers are currently connected."), assignmentsPage_);
    empty->setObjectName(QStringLiteral("noControllersLabel"));
    empty->setWordWrap(true);
    assignmentsLayout_->addRow(empty);
    return;
  }
  assignmentCombos_.reserve(controllers_.size());
  for (std::size_t index = 0U; index < controllers_.size(); ++index) {
    const auto& controller = controllers_[index];
    auto* combo = new QComboBox(assignmentsPage_);
    combo->setObjectName(QStringLiteral("controllerAssignment%1Combo")
        .arg(static_cast<qulonglong>(index)));
    for (std::size_t player = 0U; player < InputSnapshot::maximumPlayers; ++player) {
      combo->addItem(tr("Player %1").arg(player + 1U),
        QVariant::fromValue(static_cast<uint>(player)));
    }
    combo->setCurrentIndex(static_cast<int>(controller.player));
    assignmentsLayout_->addRow(QString::fromStdString(controller.name) + ':', combo);
    assignmentCombos_.push_back(combo);
  }
}

QWidget* InputConfigurationDialog::buildAdvancedPage()
{
  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("advancedInputPage"));
  auto* layout = new QFormLayout(page);
  deadzoneSpin_ = new QSpinBox(page);
  deadzoneSpin_->setObjectName(QStringLiteral("controllerDeadzoneSpinBox"));
  deadzoneSpin_->setRange(0, 32'767);
  deadzoneSpin_->setSingleStep(500);
  deadzoneSpin_->setSuffix(tr(" units"));
  layout->addRow(tr("Analog deadzone:"), deadzoneSpin_);
  connect(deadzoneSpin_, &QSpinBox::valueChanged, this, [this](int value) {
    if (auto* profile = activeProfile()) {
      profile->deadzone = static_cast<std::int16_t>(value);
    }
  });

  deviceCombos_.reserve(InputSnapshot::maximumPlayers);
  for (std::size_t player = 0U; player < InputSnapshot::maximumPlayers; ++player) {
    auto* combo = new QComboBox(page);
    combo->setObjectName(QStringLiteral("logicalDevicePlayer%1Combo")
        .arg(static_cast<qulonglong>(player + 1U)));
    for (const auto& description : deviceDescriptions) {
      combo->addItem(tr(description.name),
        QVariant::fromValue(static_cast<int>(description.type)));
    }
    connect(combo, &QComboBox::currentIndexChanged, this,
      [this, player](int) {
        if (auto* profile = activeProfile()) {
          profile->devices[player] = static_cast<input::LogicalDeviceType>(
            deviceCombos_[player]->currentData().toInt());
        }
      });
    layout->addRow(tr("Player %1 device:").arg(player + 1U), combo);
    deviceCombos_.push_back(combo);
  }
  auto* axisGroup = new QGroupBox(tr("Axis mappings"), page);
  axisGroup->setObjectName(QStringLiteral("controllerAxisMappingsGroup"));
  auto* axisLayout = new QFormLayout(axisGroup);
  axisMappingCombos_.reserve(axisDescriptions.size());
  for (std::size_t index = 0U; index < axisDescriptions.size(); ++index) {
    const auto description = axisDescriptions[index];
    auto* combo = new QComboBox(axisGroup);
    combo->setObjectName(QStringLiteral("controllerAxis%1Combo")
        .arg(QString::fromLatin1(description.suffix)));
    for (const auto& inputDescription : inputDescriptions) {
      combo->addItem(tr(inputDescription.name),
        QVariant::fromValue(static_cast<int>(inputDescription.input)));
    }
    connect(combo, &QComboBox::currentIndexChanged, this,
      [this, index, description](int) {
        auto* profile = activeProfile();
        if (profile == nullptr) {
          return;
        }
        const auto found = std::find_if(profile->controllerAxisBindings.begin(),
          profile->controllerAxisBindings.end(),
          [description](const auto& binding) {
            return binding.axis == description.axis &&
              binding.direction == description.direction;
          });
        const auto input = static_cast<InputButton>(
          axisMappingCombos_[index]->currentData().toInt());
        if (found == profile->controllerAxisBindings.end()) {
          profile->controllerAxisBindings.push_back(
            {description.axis, description.direction, input});
        } else {
          found->input = input;
        }
      });
    axisLayout->addRow(tr(description.name) + ':', combo);
    axisMappingCombos_.push_back(combo);
  }
  layout->addRow(axisGroup);
  auto* note = new QLabel(
    tr("Specialized devices are stored in the profile. Availability depends on the "
       "loaded system and game."), page);
  note->setObjectName(QStringLiteral("advancedDeviceNoteLabel"));
  note->setWordWrap(true);
  layout->addRow(note);
  return page;
}

void InputConfigurationDialog::refreshProfileList()
{
  const QSignalBlocker blocker{profileCombo_};
  profileCombo_->clear();
  int selected = 0;
  for (std::size_t index = 0U; index < configuration_.profiles.size(); ++index) {
    profileCombo_->addItem(QString::fromStdString(configuration_.profiles[index].name));
    if (configuration_.profiles[index].name == configuration_.activeProfile) {
      selected = static_cast<int>(index);
    }
  }
  profileCombo_->setCurrentIndex(selected);
  deleteProfileButton_->setEnabled(configuration_.profiles.size() > 1U);
}

void InputConfigurationDialog::refreshEditor()
{
  clearConflict();
  refreshBindings();
  const auto* profile = activeProfile();
  if (profile == nullptr) {
    return;
  }
  {
    const QSignalBlocker blocker{deadzoneSpin_};
    deadzoneSpin_->setValue(profile->deadzone);
  }
  for (std::size_t player = 0U; player < deviceCombos_.size(); ++player) {
    const QSignalBlocker blocker{deviceCombos_[player]};
    deviceCombos_[player]->setCurrentIndex(
      deviceCombos_[player]->findData(static_cast<int>(profile->devices[player])));
  }
  for (std::size_t index = 0U; index < axisMappingCombos_.size(); ++index) {
    const auto description = axisDescriptions[index];
    const auto found = std::find_if(profile->controllerAxisBindings.begin(),
      profile->controllerAxisBindings.end(),
      [description](const auto& binding) {
        return binding.axis == description.axis &&
          binding.direction == description.direction;
      });
    const auto input = found == profile->controllerAxisBindings.end()
      ? description.defaultInput : found->input;
    const QSignalBlocker blocker{axisMappingCombos_[index]};
    axisMappingCombos_[index]->setCurrentIndex(
      axisMappingCombos_[index]->findData(static_cast<int>(input)));
  }
}

void InputConfigurationDialog::refreshBindings()
{
  const auto* profile = activeProfile();
  if (profile == nullptr) {
    return;
  }
  for (std::size_t index = 0U; index < inputDescriptions.size(); ++index) {
    const auto input = inputDescriptions[index].input;
    const auto keyboard = std::find_if(profile->keyboardBindings.begin(),
      profile->keyboardBindings.end(),
      [input](const auto& binding) { return binding.button == input; });
    if (keyboard != profile->keyboardBindings.end()) {
      keyboardButtons_[index]->setKeyboardBinding(keyboard->key);
    }
    const auto controller = std::find_if(profile->controllerBindings.begin(),
      profile->controllerBindings.end(),
      [input](const auto& binding) { return binding.input == input; });
    if (controller != profile->controllerBindings.end()) {
      controllerButtons_[index]->setControllerBinding(controller->button);
    }
  }
}

void InputConfigurationDialog::addProfile()
{
  auto profile = activeProfile() == nullptr
    ? input::defaultInputProfile() : *activeProfile();
  profile.name = profileNameForIndex(configuration_, 2).toStdString();
  configuration_.profiles.push_back(std::move(profile));
  configuration_.activeProfile = configuration_.profiles.back().name;
  refreshProfileList();
  refreshEditor();
}

void InputConfigurationDialog::deleteProfile()
{
  if (configuration_.profiles.size() <= 1U) {
    return;
  }
  const auto name = configuration_.activeProfile;
  std::erase_if(configuration_.profiles,
    [&name](const auto& profile) { return profile.name == name; });
  configuration_.activeProfile = configuration_.profiles.front().name;
  refreshProfileList();
  refreshEditor();
}

void InputConfigurationDialog::restoreCurrentProfile()
{
  if (auto* profile = activeProfile()) {
    const auto name = profile->name;
    *profile = input::defaultInputProfile(name);
    refreshEditor();
  }
}

void InputConfigurationDialog::selectProfile(int index)
{
  if (index < 0 || index >= static_cast<int>(configuration_.profiles.size())) {
    return;
  }
  configuration_.activeProfile = configuration_.profiles[static_cast<std::size_t>(index)].name;
  refreshEditor();
}

void InputConfigurationDialog::keyboardCaptured(InputButton input, int key)
{
  auto* profile = activeProfile();
  if (profile == nullptr) {
    return;
  }
  if (const auto conflict = input::keyboardBindingConflict(
        *profile, input, key, reservedHotkeys_)) {
    if (conflict->kind == input::InputConflictKind::applicationHotkey) {
      showConflict(tr("That key is reserved by an emulator hotkey."));
    } else {
      showConflict(tr("That key is already assigned to %1.")
          .arg(inputDisplayName(*conflict->existingInput)));
    }
    refreshBindings();
    return;
  }
  static_cast<void>(input::setKeyboardBinding(*profile, input, key));
  clearConflict();
  refreshBindings();
}

void InputConfigurationDialog::controllerCaptured(
  InputButton input,
  SDL_GamepadButton button)
{
  auto* profile = activeProfile();
  if (profile == nullptr) {
    return;
  }
  if (const auto conflict = input::controllerBindingConflict(*profile, input, button)) {
    showConflict(tr("That controller button is already assigned to %1.")
        .arg(inputDisplayName(*conflict->existingInput)));
    refreshBindings();
    return;
  }
  static_cast<void>(input::setControllerBinding(*profile, input, button));
  clearConflict();
  refreshBindings();
}

void InputConfigurationDialog::cancelOtherCaptures(BindingCaptureButton* active)
{
  clearConflict();
  for (auto* button : keyboardButtons_) {
    if (button != active) {
      button->cancelCapture();
    }
  }
  for (auto* button : controllerButtons_) {
    if (button != active) {
      button->cancelCapture();
    }
  }
}

void InputConfigurationDialog::showConflict(const QString& message)
{
  conflictLabel_->setText(message);
  conflictLabel_->show();
}

void InputConfigurationDialog::clearConflict()
{
  conflictLabel_->clear();
  conflictLabel_->hide();
}

input::InputProfile* InputConfigurationDialog::activeProfile()
{
  return configuration_.active();
}

const input::InputProfile* InputConfigurationDialog::activeProfile() const
{
  return configuration_.active();
}

} // namespace genplusgx::ui
