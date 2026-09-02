#include "genplusgx/ui/input_movie_editor_dialog.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTextEdit>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <utility>

namespace genplusgx::ui {
namespace {

constexpr std::array buttonDescriptors{
  std::pair{InputButton::up, "Up"},
  std::pair{InputButton::down, "Down"},
  std::pair{InputButton::left, "Left"},
  std::pair{InputButton::right, "Right"},
  std::pair{InputButton::a, "A"},
  std::pair{InputButton::b, "B"},
  std::pair{InputButton::c, "C"},
  std::pair{InputButton::x, "X"},
  std::pair{InputButton::y, "Y"},
  std::pair{InputButton::z, "Z"},
  std::pair{InputButton::mode, "Mode"},
  std::pair{InputButton::start, "Start"},
};

std::string boundedUtf8(QString value, qsizetype maximumBytes)
{
  auto encoded = value.toUtf8();
  while (encoded.size() > maximumBytes && !value.isEmpty()) {
    const auto codeUnits = value.size();
    const bool surrogatePair = codeUnits >= 2 &&
      value.back().isLowSurrogate() &&
      value[codeUnits - 2].isHighSurrogate();
    value.chop(surrogatePair ? 2 : 1);
    encoded = value.toUtf8();
  }
  return encoded.toStdString();
}

QString inputText(const InputDeviceState& state)
{
  if (!state.connected) {
    return QStringLiteral("—");
  }
  QStringList names;
  for (const auto& [button, name] : buttonDescriptors) {
    if (hasButton(state.buttons, button)) {
      names.push_back(QString::fromLatin1(name));
    }
  }
  if (state.analogX != 0 || state.analogY != 0) {
    names.push_back(QStringLiteral("(%1,%2)")
      .arg(state.analogX).arg(state.analogY));
  }
  return names.empty() ? QStringLiteral("Neutral") : names.join(QLatin1Char{'+'});
}

class TimelineModel final : public QAbstractTableModel {
public:
  explicit TimelineModel(const movies::InputMovie* movie, QObject* parent)
    : QAbstractTableModel(parent), movie_(movie)
  {
  }

  int rowCount(const QModelIndex& parent = {}) const override
  {
    return parent.isValid() || movie_ == nullptr
      ? 0 : static_cast<int>(std::min<std::size_t>(
          movie_->frames.size(), static_cast<std::size_t>(
            std::numeric_limits<int>::max())));
  }

  int columnCount(const QModelIndex& parent = {}) const override
  {
    return parent.isValid() ? 0 : 9;
  }

  QVariant data(const QModelIndex& index, int role) const override
  {
    if (!index.isValid() || movie_ == nullptr || role != Qt::DisplayRole ||
        index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= movie_->frames.size()) {
      return {};
    }
    if (index.column() == 0) {
      return index.row();
    }
    return inputText(movie_->frames[static_cast<std::size_t>(index.row())]
      .players[static_cast<std::size_t>(index.column() - 1)]);
  }

  QVariant headerData(
    int section, Qt::Orientation orientation, int role) const override
  {
    if (role != Qt::DisplayRole) {
      return {};
    }
    if (orientation == Qt::Vertical) {
      return section;
    }
    const auto translated = [](const char* source) {
      return QCoreApplication::translate("InputMovieEditorDialog", source);
    };
    return section == 0
      ? translated("Frame")
      : translated("P%1").arg(section);
  }

  void refresh()
  {
    beginResetModel();
    endResetModel();
  }

private:
  const movies::InputMovie* movie_;
};

} // namespace

InputMovieEditorDialog::InputMovieEditorDialog(
  movies::InputMovie movie, QWidget* parent)
  : QDialog(parent), movie_(std::move(movie))
{
  setObjectName(QStringLiteral("inputMovieEditorDialog"));
  setWindowTitle(tr("TAS Input Movie Editor"));
  resize(980, 700);

  auto* root = new QVBoxLayout(this);
  summary_ = new QLabel(
    tr("Game %1…  •  Start frame %2  •  %3 input frames  •  %4 rerecords")
      .arg(QString::fromStdString(movie_.descriptor.gameSha256.substr(0U, 12U)))
      .arg(movie_.startFrame).arg(movie_.frames.size())
      .arg(movie_.metadata.rerecordCount), this);
  summary_->setObjectName(QStringLiteral("movieSummaryLabel"));
  root->addWidget(summary_);

  model_ = new TimelineModel(&movie_, this);
  timeline_ = new QTableView(this);
  timeline_->setObjectName(QStringLiteral("movieTimeline"));
  timeline_->setAccessibleName(tr("Input movie timeline"));
  timeline_->setAccessibleDescription(
    tr("Frame-by-frame input for all eight emulated controller ports."));
  timeline_->setModel(model_);
  timeline_->setSelectionBehavior(QAbstractItemView::SelectRows);
  timeline_->setSelectionMode(QAbstractItemView::SingleSelection);
  timeline_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  timeline_->horizontalHeader()->setStretchLastSection(true);
  root->addWidget(timeline_, 1);

  auto* editor = new QGroupBox(tr("Frame input"), this);
  editor->setObjectName(QStringLiteral("movieFrameEditor"));
  auto* grid = new QGridLayout(editor);
  frame_ = new QSpinBox(editor);
  frame_->setObjectName(QStringLiteral("movieFrameNumber"));
  frame_->setAccessibleName(tr("Frame number"));
  frame_->setRange(0, static_cast<int>(movie_.frames.size() - 1U));
  player_ = new QComboBox(editor);
  player_->setObjectName(QStringLiteral("moviePlayer"));
  player_->setAccessibleName(tr("Emulated controller port"));
  for (std::size_t player = 0U; player < InputSnapshot::maximumPlayers; ++player) {
    player_->addItem(tr("Player %1").arg(player + 1U));
  }
  connected_ = new QCheckBox(tr("Connected"), editor);
  connected_->setObjectName(QStringLiteral("moviePlayerConnected"));
  connected_->setAccessibleDescription(
    tr("Whether this controller is connected on the selected frame."));
  grid->addWidget(new QLabel(tr("Frame:"), editor), 0, 0);
  grid->addWidget(frame_, 0, 1);
  grid->addWidget(new QLabel(tr("Controller:"), editor), 0, 2);
  grid->addWidget(player_, 0, 3);
  grid->addWidget(connected_, 0, 4);
  for (std::size_t index = 0U; index < buttonDescriptors.size(); ++index) {
    buttons_[index] = new QCheckBox(
      QString::fromLatin1(buttonDescriptors[index].second), editor);
    buttons_[index]->setObjectName(
      QStringLiteral("movieButton%1").arg(
        QString::fromLatin1(buttonDescriptors[index].second)));
    grid->addWidget(buttons_[index], 1 + static_cast<int>(index / 6U),
      static_cast<int>(index % 6U));
  }
  analogX_ = new QSpinBox(editor);
  analogX_->setObjectName(QStringLiteral("movieAnalogX"));
  analogX_->setAccessibleName(tr("Analog X value"));
  analogX_->setRange(-32'768, 32'767);
  analogY_ = new QSpinBox(editor);
  analogY_->setObjectName(QStringLiteral("movieAnalogY"));
  analogY_->setAccessibleName(tr("Analog Y value"));
  analogY_->setRange(-32'768, 32'767);
  grid->addWidget(new QLabel(tr("Analog X:"), editor), 3, 0);
  grid->addWidget(analogX_, 3, 1);
  grid->addWidget(new QLabel(tr("Analog Y:"), editor), 3, 2);
  grid->addWidget(analogY_, 3, 3);
  auto* apply = new QPushButton(tr("Apply frame"), editor);
  apply->setObjectName(QStringLiteral("movieApplyFrame"));
  auto* insert = new QPushButton(tr("Insert neutral before"), editor);
  insert->setObjectName(QStringLiteral("movieInsertFrame"));
  auto* duplicate = new QPushButton(tr("Duplicate frame"), editor);
  duplicate->setObjectName(QStringLiteral("movieDuplicateFrame"));
  auto* remove = new QPushButton(tr("Delete frame"), editor);
  remove->setObjectName(QStringLiteral("movieDeleteFrame"));
  auto* truncate = new QPushButton(tr("Truncate after frame"), editor);
  truncate->setObjectName(QStringLiteral("movieTruncateAfter"));
  grid->addWidget(apply, 4, 0);
  grid->addWidget(insert, 4, 1);
  grid->addWidget(duplicate, 4, 2);
  grid->addWidget(remove, 4, 3);
  grid->addWidget(truncate, 4, 4, 1, 2);
  root->addWidget(editor);

  auto* metadata = new QGroupBox(tr("Movie metadata"), this);
  metadata->setObjectName(QStringLiteral("movieMetadata"));
  auto* form = new QFormLayout(metadata);
  author_ = new QLineEdit(QString::fromUtf8(movie_.metadata.author), metadata);
  author_->setObjectName(QStringLiteral("movieAuthor"));
  author_->setAccessibleName(tr("Movie author"));
  author_->setMaxLength(128);
  notes_ = new QTextEdit(QString::fromUtf8(movie_.metadata.notes), metadata);
  notes_->setObjectName(QStringLiteral("movieNotes"));
  notes_->setAccessibleName(tr("Movie notes"));
  notes_->setAcceptRichText(false);
  notes_->setMaximumHeight(80);
  form->addRow(tr("Author:"), author_);
  form->addRow(tr("Notes:"), notes_);
  root->addWidget(metadata);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->setObjectName(QStringLiteral("movieEditorButtons"));
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    storeMetadata();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(frame_, &QSpinBox::valueChanged, this,
    [this](int) { loadEditor(); });
  connect(player_, &QComboBox::currentIndexChanged, this,
    [this](int) { loadEditor(); });
  connect(timeline_, &QTableView::clicked, this, [this](const QModelIndex& index) {
    if (index.isValid()) {
      frame_->setValue(index.row());
    }
  });
  connect(apply, &QPushButton::clicked, this, &InputMovieEditorDialog::applyFrame);
  connect(insert, &QPushButton::clicked, this, &InputMovieEditorDialog::insertFrame);
  connect(duplicate, &QPushButton::clicked,
    this, &InputMovieEditorDialog::duplicateFrame);
  connect(remove, &QPushButton::clicked, this, &InputMovieEditorDialog::deleteFrame);
  connect(truncate, &QPushButton::clicked,
    this, &InputMovieEditorDialog::truncateAfter);
  loadEditor();
}

const movies::InputMovie& InputMovieEditorDialog::movie() const noexcept
{
  return movie_;
}

void InputMovieEditorDialog::loadEditor()
{
  const auto frame = static_cast<std::size_t>(frame_->value());
  const auto player = static_cast<std::size_t>(player_->currentIndex());
  if (frame >= movie_.frames.size() || player >= InputSnapshot::maximumPlayers) {
    return;
  }
  const auto& input = movie_.frames[frame].players[player];
  connected_->setChecked(input.connected);
  for (std::size_t index = 0U; index < buttonDescriptors.size(); ++index) {
    buttons_[index]->setChecked(hasButton(
      input.buttons, buttonDescriptors[index].first));
  }
  analogX_->setValue(input.analogX);
  analogY_->setValue(input.analogY);
  timeline_->selectRow(static_cast<int>(frame));
}

void InputMovieEditorDialog::storeMetadata()
{
  movie_.metadata.author = boundedUtf8(author_->text(), 128);
  movie_.metadata.notes = boundedUtf8(notes_->toPlainText(), 4'096);
}

InputSnapshot InputMovieEditorDialog::editedInput() const
{
  const auto frame = static_cast<std::size_t>(frame_->value());
  const auto player = static_cast<std::size_t>(player_->currentIndex());
  auto snapshot = movie_.frames[frame];
  auto& input = snapshot.players[player];
  input.connected = connected_->isChecked();
  input.buttons = 0U;
  for (std::size_t index = 0U; index < buttonDescriptors.size(); ++index) {
    if (buttons_[index]->isChecked()) {
      input.buttons |= buttonMask(buttonDescriptors[index].first);
    }
  }
  input.analogX = static_cast<std::int16_t>(analogX_->value());
  input.analogY = static_cast<std::int16_t>(analogY_->value());
  snapshot.sequence = 0U;
  return snapshot;
}

void InputMovieEditorDialog::refreshTimeline(std::size_t selectedFrame)
{
  static_cast<TimelineModel*>(model_)->refresh();
  summary_->setText(
    tr("Game %1…  •  Start frame %2  •  %3 input frames  •  %4 rerecords")
      .arg(QString::fromStdString(movie_.descriptor.gameSha256.substr(0U, 12U)))
      .arg(movie_.startFrame).arg(movie_.frames.size())
      .arg(movie_.metadata.rerecordCount));
  frame_->setMaximum(static_cast<int>(movie_.frames.size() - 1U));
  frame_->setValue(static_cast<int>(std::min(
    selectedFrame, movie_.frames.size() - 1U)));
  loadEditor();
}

void InputMovieEditorDialog::applyFrame()
{
  const auto status = movies::setFrame(
    movie_, static_cast<std::size_t>(frame_->value()), editedInput());
  if (!status) {
    QMessageBox::warning(this, tr("TAS Edit Error"),
      QString::fromStdString(status.message));
    return;
  }
  refreshTimeline(static_cast<std::size_t>(frame_->value()));
}

void InputMovieEditorDialog::insertFrame()
{
  const auto selected = static_cast<std::size_t>(frame_->value());
  const auto status = movies::insertFrames(movie_, selected, 1U);
  if (status) {
    refreshTimeline(selected);
  } else {
    QMessageBox::warning(this, tr("TAS Edit Error"),
      QString::fromStdString(status.message));
  }
}

void InputMovieEditorDialog::duplicateFrame()
{
  const auto selected = static_cast<std::size_t>(frame_->value());
  const auto status = movies::insertFrames(
    movie_, selected + 1U, 1U, movie_.frames[selected]);
  if (status) {
    refreshTimeline(selected + 1U);
  } else {
    QMessageBox::warning(this, tr("TAS Edit Error"),
      QString::fromStdString(status.message));
  }
}

void InputMovieEditorDialog::deleteFrame()
{
  const auto selected = static_cast<std::size_t>(frame_->value());
  const auto status = movies::eraseFrames(movie_, selected, 1U);
  if (status) {
    refreshTimeline(selected);
  } else {
    QMessageBox::warning(this, tr("TAS Edit Error"),
      QString::fromStdString(status.message));
  }
}

void InputMovieEditorDialog::truncateAfter()
{
  const auto selected = static_cast<std::size_t>(frame_->value());
  const auto status = movies::branchFrom(movie_, selected);
  if (status) {
    refreshTimeline(selected);
  } else {
    QMessageBox::warning(this, tr("TAS Edit Error"),
      QString::fromStdString(status.message));
  }
}

} // namespace genplusgx::ui
