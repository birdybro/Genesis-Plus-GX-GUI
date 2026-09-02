#include "genplusgx/ui/game_information_dialog.h"

#include "genplusgx/ui/dialog_service.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <optional>

namespace genplusgx::ui {
namespace {

QLineEdit* valueField(
  QWidget& parent,
  const char* objectName,
  const QString& accessibleName)
{
  auto* field = new QLineEdit(&parent);
  field->setObjectName(QString::fromLatin1(objectName));
  field->setAccessibleName(accessibleName);
  field->setReadOnly(true);
  field->setClearButtonEnabled(false);
  return field;
}

QString optionalText(const std::string& value)
{
  return value.empty() ? QStringLiteral("—") : QString::fromStdString(value);
}

QString checksumText(const library::GameMetadata& metadata)
{
  QStringList values;
  if (metadata.headerChecksum) {
    values.push_back(QObject::tr("Header: 0x%1").arg(
      static_cast<unsigned int>(*metadata.headerChecksum), 4, 16, QLatin1Char{'0'}));
  }
  if (metadata.computedChecksum) {
    values.push_back(QObject::tr("Computed: 0x%1").arg(
      static_cast<unsigned int>(*metadata.computedChecksum), 4, 16, QLatin1Char{'0'}));
  }
  return values.empty() ? QStringLiteral("—") : values.join(QStringLiteral(" · "));
}

QString discText(const library::GameMetadata& metadata)
{
  QStringList values;
  if (metadata.trackCount != 0U) {
    values.push_back(QObject::tr("%1 track(s)").arg(metadata.trackCount));
  }
  if (!metadata.relatedDataPath.empty()) {
    values.push_back(QObject::tr("Data: %1").arg(
      pathToQString(metadata.relatedDataPath)));
  }
  return values.empty() ? QStringLiteral("—") : values.join(QStringLiteral(" · "));
}

} // namespace

GameInformationDialog::GameInformationDialog(
  const library::GameMetadata& metadata,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("gameInformationDialog"));
  setWindowTitle(tr("Game Information"));
  setModal(true);
  resize(720, 620);
  setMinimumSize(560, 440);

  auto* outerLayout = new QVBoxLayout(this);
  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("gameInformationScrollArea"));
  scroll->setWidgetResizable(true);
  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("gameInformationContent"));
  auto* form = new QFormLayout(content);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);

  struct Row final {
    const char* label;
    const char* objectName;
  };
  constexpr Row rows[]{
    {"Title", "gameInfoTitleValue"},
    {"Domestic title", "gameInfoDomesticTitleValue"},
    {"International title", "gameInfoInternationalTitleValue"},
    {"System", "gameInfoSystemValue"},
    {"Region", "gameInfoRegionValue"},
    {"Format", "gameInfoFormatValue"},
    {"Product code", "gameInfoProductCodeValue"},
    {"ROM type", "gameInfoRomTypeValue"},
    {"Peripheral support", "gameInfoPeripheralValue"},
    {"Declared ROM size", "gameInfoRomSizeValue"},
    {"Checksum", "gameInfoChecksumValue"},
    {"Mapper / media", "gameInfoMapperValue"},
    {"Disc information", "gameInfoDiscValue"},
    {"File path", "gameInfoFilePathValue"},
    {"File size", "gameInfoFileSizeValue"},
    {"SHA-256", "gameInfoSha256Value"},
    {"Notes", "gameInfoNotesValue"},
    {"Online title", "gameInfoOnlineTitleValue"},
    {"Online alternate title", "gameInfoOnlineAlternateTitleValue"},
    {"Release date", "gameInfoOnlineReleaseDateValue"},
    {"Developer", "gameInfoOnlineDeveloperValue"},
    {"Publisher", "gameInfoOnlinePublisherValue"},
    {"Genres", "gameInfoOnlineGenresValue"},
    {"Description", "gameInfoOnlineDescriptionValue"},
    {"Metadata provider", "gameInfoOnlineProviderValue"},
    {"Metadata license", "gameInfoOnlineLicenseValue"},
    {"Metadata attribution", "gameInfoOnlineAttributionValue"},
    {"Metadata source", "gameInfoOnlineSourceValue"},
  };
  for (const auto& row : rows) {
    const auto labelText = tr(row.label);
    auto* field = valueField(*content, row.objectName, labelText);
    auto* label = new QLabel(labelText, content);
    label->setBuddy(field);
    form->addRow(label, field);
  }
  scroll->setWidget(content);
  outerLayout->addWidget(scroll);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("gameInformationButtonBox"));
  buttons->button(QDialogButtonBox::Close)->setObjectName(
    QStringLiteral("closeGameInformationButton"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  outerLayout->addWidget(buttons);

  setMetadata(metadata);
  setOnlineMetadata(std::nullopt);
}

void GameInformationDialog::setMetadata(const library::GameMetadata& metadata)
{
  const auto set = [this](const char* objectName, const QString& value) {
    auto* field = findChild<QLineEdit*>(QString::fromLatin1(objectName));
    field->setText(value);
    field->setCursorPosition(0);
  };
  set("gameInfoTitleValue", optionalText(metadata.displayTitle()));
  set("gameInfoDomesticTitleValue", optionalText(metadata.domesticTitle));
  set("gameInfoInternationalTitleValue", optionalText(metadata.internationalTitle));
  set("gameInfoSystemValue", QString::fromLatin1(gameSystemName(metadata.system)));
  set("gameInfoRegionValue", optionalText(metadata.region));
  set("gameInfoFormatValue", optionalText(metadata.format));
  set("gameInfoProductCodeValue", optionalText(metadata.productCode));
  set("gameInfoRomTypeValue", optionalText(metadata.romType));
  set("gameInfoPeripheralValue", optionalText(metadata.peripheralSupport));
  set("gameInfoRomSizeValue", metadata.declaredRomSize
    ? tr("%1 bytes").arg(QLocale{}.toString(
        static_cast<qulonglong>(*metadata.declaredRomSize)))
    : QStringLiteral("—"));
  set("gameInfoChecksumValue", checksumText(metadata));
  set("gameInfoMapperValue", optionalText(metadata.mapper));
  set("gameInfoDiscValue", discText(metadata));
  set("gameInfoFilePathValue", metadata.path.empty()
    ? QStringLiteral("—") : pathToQString(metadata.path));
  set("gameInfoFileSizeValue", tr("%1 bytes").arg(
    QLocale{}.toString(static_cast<qulonglong>(metadata.fileSize))));
  set("gameInfoSha256Value", optionalText(metadata.sha256));
  set("gameInfoNotesValue", optionalText(metadata.notes));
  setWindowTitle(metadata.displayTitle().empty()
    ? tr("Game Information")
    : tr("Game Information — %1").arg(
        QString::fromStdString(metadata.displayTitle())));
}

void GameInformationDialog::setOnlineMetadata(
  const std::optional<library::OnlineMetadataRecord>& metadata)
{
  const auto set = [this](const char* objectName, const std::string& value) {
    auto* field = findChild<QLineEdit*>(QString::fromLatin1(objectName));
    field->setText(optionalText(value));
    field->setCursorPosition(0);
  };
  if (!metadata) {
    for (const auto* objectName : {
           "gameInfoOnlineTitleValue", "gameInfoOnlineAlternateTitleValue",
           "gameInfoOnlineReleaseDateValue", "gameInfoOnlineDeveloperValue",
           "gameInfoOnlinePublisherValue", "gameInfoOnlineGenresValue",
           "gameInfoOnlineDescriptionValue", "gameInfoOnlineProviderValue",
           "gameInfoOnlineLicenseValue", "gameInfoOnlineAttributionValue",
           "gameInfoOnlineSourceValue"}) {
      set(objectName, {});
    }
    return;
  }
  std::string genres;
  for (const auto& genre : metadata->genres) {
    if (!genres.empty()) {
      genres += ", ";
    }
    genres += genre;
  }
  set("gameInfoOnlineTitleValue", metadata->preferredTitle);
  set("gameInfoOnlineAlternateTitleValue", metadata->alternateTitle);
  set("gameInfoOnlineReleaseDateValue", metadata->releaseDate);
  set("gameInfoOnlineDeveloperValue", metadata->developer);
  set("gameInfoOnlinePublisherValue", metadata->publisher);
  set("gameInfoOnlineGenresValue", genres);
  set("gameInfoOnlineDescriptionValue", metadata->description);
  set("gameInfoOnlineProviderValue", metadata->providerName);
  set("gameInfoOnlineLicenseValue", metadata->attribution.licenseSpdx);
  set("gameInfoOnlineAttributionValue", metadata->attribution.creator);
  set("gameInfoOnlineSourceValue", metadata->attribution.sourceUrl);
}

} // namespace genplusgx::ui
