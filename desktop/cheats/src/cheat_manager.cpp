#include "genplusgx/cheats/cheat_manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace genplusgx::cheats {
namespace {

constexpr std::string_view genesisGameGenieAlphabet =
  "ABCDEFGHJKLMNPRSTVWXYZ0123456789";

CheatStatus cheatFailure(CheatError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

PersistenceStatus persistenceFailure(std::string message)
{
  return {
    .error = PersistenceError::invalidData,
    .message = std::move(message),
  };
}

std::string_view trimWhitespace(std::string_view input)
{
  while (
    !input.empty() && std::isspace(static_cast<unsigned char>(input.front())) != 0) {
    input.remove_prefix(1U);
  }
  while (
    !input.empty() && std::isspace(static_cast<unsigned char>(input.back())) != 0) {
    input.remove_suffix(1U);
  }
  return input;
}

std::string trimAndUpper(std::string_view input)
{
  input = trimWhitespace(input);
  std::string output{input};
  std::ranges::transform(output, output.begin(), [](unsigned char value) {
    return static_cast<char>(std::toupper(value));
  });
  return output;
}

std::optional<std::uint8_t> hexadecimal(char value)
{
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>((value - 'A') + 10);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> hexadecimalValue(
  std::string_view text, std::span<const std::size_t> positions)
{
  std::uint32_t value = 0U;
  for (const auto position : positions) {
    if (position >= text.size()) {
      return std::nullopt;
    }
    const auto digit = hexadecimal(text[position]);
    if (!digit) {
      return std::nullopt;
    }
    value = (value << 4U) | *digit;
  }
  return value;
}

std::optional<std::uint32_t> contiguousHexadecimal(
  std::string_view text, std::size_t offset, std::size_t count)
{
  std::uint32_t value = 0U;
  if (offset > text.size() || count > text.size() - offset) {
    return std::nullopt;
  }
  for (std::size_t index = offset; index < offset + count; ++index) {
    const auto digit = hexadecimal(text[index]);
    if (!digit) {
      return std::nullopt;
    }
    value = (value << 4U) | *digit;
  }
  return value;
}

std::optional<CoreCheatPatch> decodeGenesisGameGenie(std::string_view code)
{
  if (code.size() != 9U || code[4] != '-') {
    return std::nullopt;
  }
  std::uint32_t address = 0U;
  std::uint16_t data = 0U;
  std::size_t source = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    if (source == 4U) {
      ++source;
    }
    const auto found = genesisGameGenieAlphabet.find(code[source++]);
    if (found == std::string_view::npos) {
      return std::nullopt;
    }
    const auto value = static_cast<std::uint32_t>(found);
    switch (index) {
      case 0U:
        data |= static_cast<std::uint16_t>(value << 3U);
        break;
      case 1U:
        data |= static_cast<std::uint16_t>(value >> 2U);
        address |= (value & 3U) << 14U;
        break;
      case 2U:
        address |= value << 9U;
        break;
      case 3U:
        address |= ((value & 0x0fU) << 20U) | ((value >> 4U) << 8U);
        break;
      case 4U:
        data |= static_cast<std::uint16_t>((value & 1U) << 12U);
        address |= (value >> 1U) << 16U;
        break;
      case 5U:
        data |=
          static_cast<std::uint16_t>(((value & 1U) << 15U) | ((value >> 1U) << 8U));
        break;
      case 6U:
        data |= static_cast<std::uint16_t>((value >> 3U) << 13U);
        address |= (value & 7U) << 5U;
        break;
      case 7U:
        address |= value;
        break;
      default:
        break;
    }
  }
  return CoreCheatPatch{
    .address = address,
    .data = data,
    .width = CoreCheatWidth::word,
  };
}

std::optional<CoreCheatPatch> decodeGenesisActionReplay(std::string_view code)
{
  if (code.size() != 11U || code[6] != ':') {
    return std::nullopt;
  }
  const auto address = contiguousHexadecimal(code, 0U, 6U);
  const auto data = contiguousHexadecimal(code, 7U, 4U);
  if (!address || !data) {
    return std::nullopt;
  }
  return CoreCheatPatch{
    .address = *address,
    .data = static_cast<std::uint16_t>(*data),
    .width = CoreCheatWidth::word,
  };
}

std::uint32_t masterAddress(std::uint32_t address)
{
  return address >= 0xc000U ? 0xff0000U | (address & 0x1fffU) : address;
}

std::optional<CoreCheatPatch> decodeMasterGameGenie(std::string_view code)
{
  if ((code.size() != 7U && code.size() != 11U) || code[3] != '-' ||
      (code.size() == 11U && code[7] != '-')) {
    return std::nullopt;
  }
  constexpr std::array<std::size_t, 2> dataPositions{0U, 1U};
  constexpr std::array<std::size_t, 3> lowAddressPositions{2U, 4U, 5U};
  const auto data = hexadecimalValue(code, dataPositions);
  const auto lowAddress = hexadecimalValue(code, lowAddressPositions);
  const auto highAddress = hexadecimal(code[6]);
  if (!data || !lowAddress || !highAddress) {
    return std::nullopt;
  }
  CoreCheatPatch patch{
    .address = masterAddress(
      *lowAddress | (static_cast<std::uint32_t>(*highAddress ^ 0x0fU) << 12U)),
    .data = static_cast<std::uint16_t>(*data),
    .width = CoreCheatWidth::byte,
  };
  if (code.size() == 11U) {
    constexpr std::array<std::size_t, 2> referencePositions{8U, 10U};
    const auto reference = hexadecimalValue(code, referencePositions);
    if (!reference || !hexadecimal(code[9])) {
      return std::nullopt;
    }
    const auto rotated = (*reference >> 2U) | ((*reference & 3U) << 6U);
    patch.reference = static_cast<std::uint8_t>(rotated ^ 0xbaU);
    patch.referenceRequired = true;
  }
  return patch;
}

std::optional<CoreCheatPatch> decodeMasterActionReplay(std::string_view code)
{
  if (code.size() != 9U || code[4] != '-') {
    return std::nullopt;
  }
  if (!contiguousHexadecimal(code, 0U, 2U)) {
    return std::nullopt;
  }
  constexpr std::array<std::size_t, 4> addressPositions{2U, 3U, 5U, 6U};
  constexpr std::array<std::size_t, 2> dataPositions{7U, 8U};
  const auto address = hexadecimalValue(code, addressPositions);
  const auto data = hexadecimalValue(code, dataPositions);
  if (!address || !data) {
    return std::nullopt;
  }
  return CoreCheatPatch{
    .address = masterAddress(*address),
    .data = static_cast<std::uint16_t>(*data),
    .width = CoreCheatWidth::byte,
  };
}

std::optional<CoreCheatPatch> decodeFusionRam(std::string_view code)
{
  if (code.size() != 7U || code[4] != ':') {
    return std::nullopt;
  }
  const auto address = contiguousHexadecimal(code, 0U, 4U);
  const auto data = contiguousHexadecimal(code, 5U, 2U);
  if (!address || !data) {
    return std::nullopt;
  }
  return CoreCheatPatch{
    .address = masterAddress(*address),
    .data = static_cast<std::uint16_t>(*data),
    .width = CoreCheatWidth::byte,
  };
}

std::optional<CoreCheatPatch> decodeFusionRom(std::string_view code)
{
  if (code.size() != 9U || code[6] != ':') {
    return std::nullopt;
  }
  const auto reference = contiguousHexadecimal(code, 0U, 2U);
  const auto address = contiguousHexadecimal(code, 2U, 4U);
  const auto data = contiguousHexadecimal(code, 7U, 2U);
  if (!reference || !address || !data) {
    return std::nullopt;
  }
  return CoreCheatPatch{
    .address = masterAddress(*address),
    .data = static_cast<std::uint16_t>(*data),
    .reference = static_cast<std::uint8_t>(*reference),
    .width = CoreCheatWidth::byte,
    .referenceRequired = true,
  };
}

struct DecodedSegment final {
  CoreCheatPatch patch;
  CheatFormat format;
};

std::optional<DecodedSegment> decodeSegment(
  CheatSystem system, std::string_view segment)
{
  if (system == CheatSystem::genesis) {
    if (const auto patch = decodeGenesisGameGenie(segment)) {
      return DecodedSegment{*patch, CheatFormat::genesisGameGenie};
    }
    if (const auto patch = decodeGenesisActionReplay(segment)) {
      return DecodedSegment{*patch, CheatFormat::genesisActionReplay};
    }
    return std::nullopt;
  }
  if (const auto patch = decodeMasterGameGenie(segment)) {
    return DecodedSegment{*patch, CheatFormat::masterSystemGameGenie};
  }
  if (const auto patch = decodeMasterActionReplay(segment)) {
    return DecodedSegment{*patch, CheatFormat::masterSystemActionReplay};
  }
  if (const auto patch = decodeFusionRam(segment)) {
    return DecodedSegment{*patch, CheatFormat::fusionRam};
  }
  if (const auto patch = decodeFusionRom(segment)) {
    return DecodedSegment{*patch, CheatFormat::fusionRom};
  }
  return std::nullopt;
}

QString systemName(CheatSystem system)
{
  return system == CheatSystem::genesis ? QStringLiteral("genesis")
                                        : QStringLiteral("master-system");
}

} // namespace

CheatParseResult parseCheatCode(CheatSystem system, std::string_view code)
{
  CheatParseResult result;
  if (code.size() > maximumCheatCodeBytes) {
    result.status = cheatFailure(
      CheatError::tooManyPatches, "The cheat code exceeds the 256-byte limit.");
    return result;
  }
  const auto normalized = trimAndUpper(code);
  if (normalized.empty()) {
    result.status = cheatFailure(CheatError::emptyCode, "Enter a cheat code.");
    return result;
  }

  std::size_t begin = 0U;
  while (begin <= normalized.size()) {
    const auto separator = normalized.find('+', begin);
    const auto end = separator == std::string::npos ? normalized.size() : separator;
    const auto segment =
      trimAndUpper(std::string_view{normalized}.substr(begin, end - begin));
    if (segment.empty()) {
      result.status = cheatFailure(
        CheatError::invalidFormat, "A multi-part cheat contains an empty code.");
      result.patches.clear();
      result.formats.clear();
      return result;
    }
    const auto decoded = decodeSegment(system, segment);
    if (!decoded) {
      result.status = cheatFailure(CheatError::invalidFormat,
        "The code does not match a supported format for this system.");
      result.patches.clear();
      result.formats.clear();
      return result;
    }
    if (result.patches.size() >= maximumCoreCheatPatches) {
      result.status = cheatFailure(
        CheatError::tooManyPatches, "The cheat contains more than 150 patches.");
      result.patches.clear();
      result.formats.clear();
      return result;
    }
    if (!result.normalizedCode.empty()) {
      result.normalizedCode.push_back('+');
    }
    result.normalizedCode += segment;
    result.patches.push_back(decoded->patch);
    result.formats.push_back(decoded->format);
    if (separator == std::string::npos) {
      break;
    }
    begin = separator + 1U;
  }
  return result;
}

CheatStatus validateCheatConfiguration(CheatSystem system,
  const CheatConfiguration& configuration,
  std::vector<CoreCheatPatch>* enabledPatches)
{
  if (configuration.entries.size() > maximumCheatDefinitions) {
    return cheatFailure(
      CheatError::tooManyPatches, "A game can contain at most 150 cheat definitions.");
  }
  std::vector<CoreCheatPatch> decodedPatches;
  for (const auto& entry : configuration.entries) {
    if (trimWhitespace(entry.name).empty() ||
        entry.name.size() > maximumCheatNameBytes ||
        entry.code.size() > maximumCheatCodeBytes) {
      return cheatFailure(
        CheatError::invalidDefinition, "Every cheat needs a bounded name and code.");
    }
    const auto parsed = parseCheatCode(system, entry.code);
    if (!parsed.status) {
      return parsed.status;
    }
    if (entry.enabled) {
      if (decodedPatches.size() > maximumCoreCheatPatches - parsed.patches.size()) {
        return cheatFailure(CheatError::tooManyPatches,
          "Enabled cheats contain more than 150 core patches.");
      }
      decodedPatches.insert(
        decodedPatches.end(), parsed.patches.begin(), parsed.patches.end());
    }
  }
  if (enabledPatches != nullptr) {
    *enabledPatches = std::move(decodedPatches);
  }
  return {};
}

CheatStore::CheatStore(std::filesystem::path root) : root_(std::move(root)) {}

const std::filesystem::path& CheatStore::root() const noexcept { return root_; }

std::filesystem::path CheatStore::pathFor(const GameIdentity& identity) const
{
  if (!identity.valid() || root_.empty() || !root_.is_absolute()) {
    return {};
  }
  return root_ / (identity.directoryName() + ".json");
}

CheatLoadResult CheatStore::load(const GameIdentity& identity, CheatSystem system) const
{
  const auto path = pathFor(identity);
  if (path.empty()) {
    return {
      .status = persistenceFailure("The cheat store path or game identity is invalid."),
      .configuration = {},
    };
  }
  const auto loaded = readFileBounded(path, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .configuration = {}};
  }
  if (!loaded.exists) {
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return {
      .status = persistenceFailure("The cheat file is not valid JSON."),
      .configuration = {},
    };
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  const auto sha256 = root.value(QStringLiteral("gameSha256"));
  const auto storedSystem = root.value(QStringLiteral("system"));
  const auto entries = root.value(QStringLiteral("entries"));
  if (!schema.isDouble() || schema.toInt(-1) != static_cast<int>(schemaVersion) ||
      !sha256.isString() || sha256.toString().toStdString() != identity.sha256 ||
      !storedSystem.isString() || storedSystem.toString() != systemName(system) ||
      !entries.isArray() ||
      entries.toArray().size() > static_cast<qsizetype>(maximumCheatDefinitions)) {
    return {
      .status = persistenceFailure(
        "The cheat file schema, game identity, or system is invalid."),
      .configuration = {},
    };
  }
  CheatConfiguration configuration;
  configuration.entries.reserve(static_cast<std::size_t>(entries.toArray().size()));
  for (const auto value : entries.toArray()) {
    if (!value.isObject()) {
      return {
        .status = persistenceFailure("A cheat entry is invalid."),
        .configuration = {},
      };
    }
    const auto object = value.toObject();
    const auto name = object.value(QStringLiteral("name"));
    const auto code = object.value(QStringLiteral("code"));
    const auto enabled = object.value(QStringLiteral("enabled"));
    if (!name.isString() || !code.isString() || !enabled.isBool()) {
      return {
        .status = persistenceFailure("A cheat entry is incomplete."),
        .configuration = {},
      };
    }
    configuration.entries.push_back({
      .name = name.toString().toStdString(),
      .code = code.toString().toStdString(),
      .enabled = enabled.toBool(),
    });
  }
  if (const auto status = validateCheatConfiguration(system, configuration); !status) {
    return {
      .status = persistenceFailure(
        "The cheat file contains an invalid code: " + status.message),
      .configuration = {},
    };
  }
  return {.status = {}, .configuration = std::move(configuration)};
}

PersistenceStatus CheatStore::save(const GameIdentity& identity,
  CheatSystem system,
  const CheatConfiguration& configuration) const
{
  const auto path = pathFor(identity);
  if (path.empty()) {
    return persistenceFailure("The cheat store path or game identity is invalid.");
  }
  if (const auto status = validateCheatConfiguration(system, configuration); !status) {
    return persistenceFailure("Invalid cheats cannot be saved: " + status.message);
  }
  QJsonArray entries;
  for (const auto& entry : configuration.entries) {
    const auto parsed = parseCheatCode(system, entry.code);
    entries.push_back(QJsonObject{
      {QStringLiteral("name"), QString::fromStdString(entry.name)},
      {QStringLiteral("code"), QString::fromStdString(parsed.normalizedCode)},
      {QStringLiteral("enabled"), entry.enabled},
    });
  }
  const auto data = QJsonDocument{
    QJsonObject{
      {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
      {QStringLiteral("gameSha256"), QString::fromStdString(identity.sha256)},
      {QStringLiteral("system"), systemName(system)},
      {QStringLiteral("entries"), entries},
    }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    maximumFileBytes);
}

} // namespace genplusgx::cheats
