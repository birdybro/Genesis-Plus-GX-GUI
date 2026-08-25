#include "genplusgx/platform/bios_manager.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

namespace genplusgx::platform {
namespace {

constexpr std::size_t maximumFirmwareBytes = 4U * 1024U * 1024U;
constexpr std::size_t maximumCorePathBytes = 4'095U;

constexpr std::array<BiosDescriptor, biosSlotCount> descriptors{{
  {BiosSlot::genesis, "genesis", "Genesis / Mega Drive boot ROM", "Worldwide"},
  {BiosSlot::masterSystemUsa, "masterSystemUsa", "Master System BIOS (USA)", "USA"},
  {BiosSlot::masterSystemEurope, "masterSystemEurope", "Master System BIOS (Europe)", "Europe"},
  {BiosSlot::masterSystemJapan, "masterSystemJapan", "Master System BIOS (Japan)", "Japan"},
  {BiosSlot::gameGear, "gameGear", "Game Gear BIOS", "Worldwide"},
  {BiosSlot::segaCdUsa, "segaCdUsa", "Sega CD BIOS (USA)", "USA"},
  {BiosSlot::segaCdEurope, "segaCdEurope", "Mega CD BIOS (Europe)", "Europe"},
  {BiosSlot::segaCdJapan, "segaCdJapan", "Mega CD BIOS (Japan)", "Japan"},
}};

std::size_t slotIndex(BiosSlot slot) noexcept
{
  const auto index = static_cast<std::size_t>(slot);
  return index < biosSlotCount ? index : 0U;
}

QString pathString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

std::filesystem::path filesystemPath(const QString& path)
{
#if defined(_WIN32)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().toStdString()};
#endif
}

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

std::optional<int> integer(const QJsonObject& object, const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  if (!member.isDouble()) {
    return std::nullopt;
  }
  const double value = member.toDouble();
  if (!std::isfinite(value) || std::floor(value) != value ||
      value < static_cast<double>(std::numeric_limits<int>::min()) ||
      value > static_cast<double>(std::numeric_limits<int>::max()) ||
      value != static_cast<double>(static_cast<int>(value))) {
    return std::nullopt;
  }
  return static_cast<int>(value);
}

std::optional<BiosConfiguration> readConfiguration(const QJsonObject& object)
{
  BiosConfiguration configuration;
  for (const auto& descriptor : descriptors) {
    const auto member = object.value(QString::fromLatin1(
      descriptor.key.data(), static_cast<qsizetype>(descriptor.key.size())));
    if (!member.isString()) {
      return std::nullopt;
    }
    configuration.setPath(descriptor.slot, filesystemPath(member.toString()));
  }
  return configuration;
}

bool validSize(BiosSlot slot, std::uintmax_t size) noexcept
{
  switch (slot) {
    case BiosSlot::genesis:
      return size == 2U * 1024U;
    case BiosSlot::gameGear:
      return size == 1U * 1024U;
    case BiosSlot::masterSystemUsa:
    case BiosSlot::masterSystemEurope:
    case BiosSlot::masterSystemJapan:
      return size >= 1U * 1024U && size <= maximumFirmwareBytes &&
             (size % 1'024U) == 0U;
    case BiosSlot::segaCdUsa:
    case BiosSlot::segaCdEurope:
    case BiosSlot::segaCdJapan:
      return size == 128U * 1024U;
  }
  return false;
}

std::string sizeRequirement(BiosSlot slot)
{
  switch (slot) {
    case BiosSlot::genesis:
      return "Genesis boot firmware must be exactly 2 KiB.";
    case BiosSlot::gameGear:
      return "Game Gear firmware must be exactly 1 KiB.";
    case BiosSlot::masterSystemUsa:
    case BiosSlot::masterSystemEurope:
    case BiosSlot::masterSystemJapan:
      return "Master System firmware must be a 1 KiB-aligned file from 1 KiB through 4 MiB.";
    case BiosSlot::segaCdUsa:
    case BiosSlot::segaCdEurope:
    case BiosSlot::segaCdJapan:
      return "Sega CD / Mega CD firmware must be exactly 128 KiB.";
  }
  return "The firmware size is not supported.";
}

std::string detectedType(BiosSlot slot, std::span<const std::uint8_t> data)
{
  if (slot == BiosSlot::genesis) {
    return "Genesis / Mega Drive boot firmware";
  }
  if (slot == BiosSlot::gameGear) {
    return "Game Gear boot firmware";
  }
  if (slot == BiosSlot::masterSystemUsa ||
      slot == BiosSlot::masterSystemEurope ||
      slot == BiosSlot::masterSystemJapan) {
    return "Master System boot firmware";
  }

  const auto contains = [data](std::string_view needle) {
    return std::search(data.begin(), data.end(), needle.begin(), needle.end(),
      [](std::uint8_t left, char right) {
        return left == static_cast<std::uint8_t>(right);
      }) != data.end();
  };
  if (contains("WONDER-MEGA BOOT") || contains("WONDERMEGA2 BOOT")) {
    return "Wondermega firmware";
  }
  if (contains("CDX BOOT ROM")) {
    return "Sega CDX / Multi-Mega firmware";
  }
  return "Sega CD / Mega CD boot firmware";
}

bool hasUsefulContent(std::span<const std::uint8_t> data) noexcept
{
  return !data.empty() && std::ranges::any_of(data, [first = data.front()](
    std::uint8_t value) { return value != first; });
}

} // namespace

const std::array<BiosDescriptor, biosSlotCount>& biosDescriptors() noexcept
{
  return descriptors;
}

const BiosDescriptor& biosDescriptor(BiosSlot slot) noexcept
{
  return descriptors[slotIndex(slot)];
}

const std::filesystem::path& BiosConfiguration::path(BiosSlot slot) const noexcept
{
  return paths[slotIndex(slot)];
}

void BiosConfiguration::setPath(BiosSlot slot, std::filesystem::path path)
{
  paths[slotIndex(slot)] = std::move(path);
}

BiosValidation validateBios(BiosSlot slot, const std::filesystem::path& path)
{
  BiosValidation result;
  result.slot = slot;
  result.path = path;
  if (path.empty()) {
    result.message = "Not configured.";
    return result;
  }
  if (pathString(path).toUtf8().size() >
      static_cast<qsizetype>(maximumCorePathBytes)) {
    result.state = BiosValidationState::pathTooLong;
    result.message = "The firmware path is too long for the emulator core.";
    return result;
  }

  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::exists(status)) {
    result.state = BiosValidationState::missing;
    result.message = "The configured firmware file does not exist.";
    return result;
  }
  if (!std::filesystem::is_regular_file(status)) {
    result.state = BiosValidationState::notRegularFile;
    result.message = "The configured path is not a regular file.";
    return result;
  }
  result.fileSize = std::filesystem::file_size(path, error);
  if (error) {
    result.state = BiosValidationState::unreadable;
    result.message = "The firmware file size could not be read.";
    return result;
  }
  if (!validSize(slot, result.fileSize)) {
    result.state = BiosValidationState::invalidSize;
    result.message = sizeRequirement(slot);
    return result;
  }

  const auto loaded = readFileBounded(path, maximumFirmwareBytes);
  if (!loaded.status || !loaded.exists) {
    result.state = BiosValidationState::unreadable;
    result.message = loaded.status.message.empty()
      ? "The firmware file could not be read."
      : loaded.status.message;
    return result;
  }
  if (!hasUsefulContent(loaded.data)) {
    result.state = BiosValidationState::invalidContent;
    result.message = "The firmware file contains only one repeated byte.";
    return result;
  }

  const auto data = QByteArrayView{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())};
  result.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256)
                    .toHex().toStdString();
  result.detectedType = detectedType(slot, loaded.data);
  result.state = BiosValidationState::valid;
  result.message = "Valid size and readable content; checksum is informational.";
  return result;
}

BiosConfigurationStore::BiosConfigurationStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& BiosConfigurationStore::path() const noexcept
{
  return path_;
}

BiosConfigurationLoadResult BiosConfigurationStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .configuration = {}};
  }
  if (!loaded.exists) {
    return {.status = {}, .configuration = {}};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = invalid("The BIOS configuration file is not valid JSON."),
      .configuration = {}};
  }
  const auto root = document.object();
  const auto schema = integer(root, "schemaVersion");
  if (!schema) {
    return {.status = invalid("The BIOS configuration schema version is missing."),
      .configuration = {}};
  }
  if (*schema == 0) {
    const auto migrated = readConfiguration(root);
    if (!migrated) {
      return {.status = invalid("The legacy BIOS configuration is invalid."),
        .configuration = {}};
    }
    return {.status = {}, .configuration = *migrated, .migrated = true};
  }
  if (*schema != static_cast<int>(schemaVersion)) {
    return {.status = invalid("The BIOS configuration schema version is not supported."),
      .configuration = {}};
  }
  const auto member = root.value(QStringLiteral("bios"));
  const auto configuration = member.isObject()
    ? readConfiguration(member.toObject()) : std::nullopt;
  if (!configuration) {
    return {.status = invalid("The BIOS configuration values are invalid."),
      .configuration = {}};
  }
  return {.status = {}, .configuration = *configuration};
}

PersistenceStatus BiosConfigurationStore::save(
  const BiosConfiguration& configuration) const
{
  QJsonObject bios;
  for (const auto& descriptor : descriptors) {
    bios.insert(QString::fromLatin1(
      descriptor.key.data(), static_cast<qsizetype>(descriptor.key.size())),
      pathString(configuration.path(descriptor.slot)));
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("bios"), bios},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

BiosManager::BiosManager(BiosConfigurationStore store)
  : store_(std::move(store))
{
  refreshValidation();
}

PersistenceStatus BiosManager::load()
{
  const auto loaded = store_.load();
  if (!loaded.status) {
    return loaded.status;
  }
  snapshot_.configuration = loaded.configuration;
  refreshValidation();
  if (loaded.migrated) {
    return store_.save(snapshot_.configuration);
  }
  return {};
}

PersistenceStatus BiosManager::apply(BiosConfiguration configuration)
{
  const auto saved = store_.save(configuration);
  if (!saved) {
    return saved;
  }
  snapshot_.configuration = std::move(configuration);
  refreshValidation();
  return {};
}

const BiosSnapshot& BiosManager::snapshot() const noexcept
{
  return snapshot_;
}

void BiosManager::refreshValidation()
{
  for (const auto& descriptor : descriptors) {
    snapshot_.validation[slotIndex(descriptor.slot)] = validateBios(
      descriptor.slot, snapshot_.configuration.path(descriptor.slot));
  }
}

} // namespace genplusgx::platform
