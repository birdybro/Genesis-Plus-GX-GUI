#include "genplusgx/cloud/cloud_manifest.h"

#include "genplusgx/state_manager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <set>
#include <system_error>
#include <utility>

namespace genplusgx::cloud {
namespace {

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

QString pathString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

bool validSegment(const std::string& value, std::size_t maximum)
{
  return !value.empty() && value.size() <= maximum && value != "." &&
    value != ".." && std::ranges::all_of(value, [](unsigned char byte) {
      return std::isalnum(byte) != 0 || byte == '-' || byte == '_' || byte == '.';
    });
}

bool validStateFilename(const std::string& value)
{
  if (value == "resume.gpgxstate") {
    return true;
  }
  constexpr std::string_view prefix{"slot-"};
  constexpr std::string_view suffix{".gpgxstate"};
  return value.size() == prefix.size() + 1U + suffix.size() &&
    value.starts_with(prefix) && value.ends_with(suffix) &&
    value[prefix.size()] >= '0' && value[prefix.size()] <= '9';
}

bool validSaveFilename(const std::string& value)
{
  return value == "cartridge.srm" || value == "scd-internal.brm" ||
    value == "scd-cartridge.brm";
}

std::vector<std::string> keyParts(const std::string& key)
{
  std::vector<std::string> parts;
  std::size_t begin = 0U;
  while (begin <= key.size()) {
    const auto end = key.find('/', begin);
    parts.emplace_back(key.substr(begin, end == std::string::npos
      ? std::string::npos : end - begin));
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return parts;
}

std::string fileSha256(const std::filesystem::path& path, std::uint64_t size)
{
  QFile input{pathString(path)};
  if (!input.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash{QCryptographicHash::Sha256};
  std::array<char, 64U * 1024U> buffer{};
  std::uint64_t readTotal = 0U;
  while (true) {
    const auto read = input.read(buffer.data(), static_cast<qint64>(buffer.size()));
    if (read < 0) {
      return {};
    }
    if (read == 0) {
      break;
    }
    readTotal += static_cast<std::uint64_t>(read);
    hash.addData(QByteArrayView{buffer.data(), read});
  }
  return readTotal == size ? hash.result().toHex().toStdString() : std::string{};
}

Status validateRemoteManifest(const RemoteManifest& manifest)
{
  if (manifest.files.size() > maximumFiles) {
    return failure(Error::dataTooLarge, "The remote manifest has too many files.");
  }
  std::uint64_t total = 0U;
  for (const auto& [key, record] : manifest.files) {
    if (key != record.key || !validFileKey(key) || !validSha256(record.sha256) ||
        record.size > maximumBytesForKey(key)) {
      return failure(Error::invalidManifest,
        "The remote manifest contains an invalid file record.");
    }
    if (total > maximumTotalBytes - record.size) {
      return failure(Error::dataTooLarge,
        "The remote manifest exceeds the synchronized-data limit.");
    }
    total += record.size;
  }
  return {};
}

} // namespace

bool validSha256(const std::string& value) noexcept
{
  return value.size() == 64U && std::ranges::all_of(value,
    [](unsigned char byte) {
      return std::isdigit(byte) != 0 || (byte >= 'a' && byte <= 'f');
    });
}

bool validFileKey(const std::string& key) noexcept
{
  if (key.empty() || key.size() > 256U || key.find('\\') != std::string::npos) {
    return false;
  }
  const auto parts = keyParts(key);
  if (parts.size() != 3U || !validSegment(parts[1], 160U) ||
      !validSegment(parts[2], 64U)) {
    return false;
  }
  if (parts[0] == "saves") {
    return validSaveFilename(parts[2]);
  }
  if (parts[0] == "states") {
    return validStateFilename(parts[2]);
  }
  return false;
}

std::size_t maximumBytesForKey(const std::string& key) noexcept
{
  if (key.starts_with("saves/")) {
    return PersistenceStore::maximumRamBytes;
  }
  if (key.starts_with("states/")) {
    return SaveStateManager::maximumFileBytes;
  }
  return 0U;
}

std::string baselineId(const Settings& settings)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  const auto add = [&hash](const std::string& value) {
    hash.addData(QByteArrayView{value.data(), static_cast<qsizetype>(value.size())});
    constexpr char separator{'\0'};
    hash.addData(QByteArrayView{&separator, 1});
  };
  add(settings.endpoint);
  add(settings.username);
  add(settings.remoteDirectory);
  return hash.result().toHex().left(32).toStdString();
}

Action chooseAction(const FileRecord* local, const FileRecord* remote,
  const Baseline* baseline) noexcept
{
  if (local && remote && local->sha256 == remote->sha256) {
    return Action::unchanged;
  }
  if (local == nullptr && remote == nullptr) {
    return Action::unchanged;
  }
  if (baseline == nullptr) {
    if (local && remote) {
      return Action::conflict;
    }
    return local ? Action::upload : Action::download;
  }
  const std::string currentLocal = local ? local->sha256 : std::string{};
  const std::string currentRemote = remote ? remote->sha256 : std::string{};
  const bool localChanged = currentLocal != baseline->localSha256;
  const bool remoteChanged = currentRemote != baseline->remoteSha256;
  if (localChanged && remoteChanged) {
    return local && remote ? Action::conflict
                           : (local ? Action::upload : Action::download);
  }
  if (localChanged) {
    return local ? Action::upload : Action::download;
  }
  if (remoteChanged) {
    return remote ? Action::download : Action::upload;
  }
  return Action::unchanged;
}

std::vector<std::uint8_t> encodeRemoteManifest(const RemoteManifest& manifest)
{
  if (!validateRemoteManifest(manifest)) {
    return {};
  }
  QJsonArray files;
  for (const auto& [key, record] : manifest.files) {
    files.append(QJsonObject{
      {QStringLiteral("key"), QString::fromStdString(key)},
      {QStringLiteral("sha256"), QString::fromStdString(record.sha256)},
      {QStringLiteral("size"), static_cast<qint64>(record.size)},
    });
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("files"), files},
  }}.toJson(QJsonDocument::Compact);
  return {reinterpret_cast<const std::uint8_t*>(data.constData()),
    reinterpret_cast<const std::uint8_t*>(data.constData() + data.size())};
}

ManifestResult decodeRemoteManifest(std::span<const std::uint8_t> data)
{
  if (data.size() > maximumManifestBytes) {
    return {.status = failure(Error::dataTooLarge,
      "The remote manifest exceeds its byte limit."), .manifest = {}};
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size())},
    &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = failure(Error::invalidManifest,
      "The remote manifest is not valid JSON."), .manifest = {}};
  }
  const auto root = document.object();
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 ||
      !root.value(QStringLiteral("files")).isArray()) {
    return {.status = failure(Error::invalidManifest,
      "The remote manifest schema is not supported."), .manifest = {}};
  }
  const auto array = root.value(QStringLiteral("files")).toArray();
  if (array.size() > static_cast<qsizetype>(maximumFiles)) {
    return {.status = failure(Error::dataTooLarge,
      "The remote manifest has too many files."), .manifest = {}};
  }
  RemoteManifest manifest;
  for (const auto& value : array) {
    if (!value.isObject()) {
      return {.status = failure(Error::invalidManifest,
        "The remote manifest contains a non-object entry."), .manifest = {}};
    }
    const auto object = value.toObject();
    const auto keyValue = object.value(QStringLiteral("key"));
    const auto hashValue = object.value(QStringLiteral("sha256"));
    const auto sizeValue = object.value(QStringLiteral("size"));
    if (!keyValue.isString() || !hashValue.isString() || !sizeValue.isDouble()) {
      return {.status = failure(Error::invalidManifest,
        "The remote manifest contains an incomplete entry."), .manifest = {}};
    }
    FileRecord record{
      .key = keyValue.toString().toStdString(),
      .sha256 = hashValue.toString().toStdString(),
      .size = static_cast<std::uint64_t>(sizeValue.toInteger(-1)),
    };
    if (!validFileKey(record.key) || !validSha256(record.sha256) ||
        sizeValue.toInteger(-1) < 0 || record.size > maximumBytesForKey(record.key) ||
        manifest.files.contains(record.key)) {
      return {.status = failure(Error::invalidManifest,
        "The remote manifest contains an invalid or duplicate entry."),
        .manifest = {}};
    }
    manifest.files.emplace(record.key, std::move(record));
  }
  const auto status = validateRemoteManifest(manifest);
  return {.status = status, .manifest = status ? std::move(manifest) : RemoteManifest{}};
}

std::optional<std::filesystem::path> localPathForKey(
  const ApplicationPaths& paths, const std::string& key)
{
  if (!validFileKey(key)) {
    return std::nullopt;
  }
  const auto parts = keyParts(key);
  const auto root = parts[0] == "saves" ? paths.savesDirectory()
                                        : paths.statesDirectory();
  return (root / parts[1] / parts[2]).lexically_normal();
}

FileScanResult scanLocalFiles(const ApplicationPaths& paths, const Settings& settings)
{
  const auto validation = validateSettings(settings);
  if (!validation) {
    return {.status = validation, .files = {}};
  }
  FileMap files;
  std::uint64_t total = 0U;
  const std::array roots{
    std::pair{std::string{"saves"}, settings.syncSaves},
    std::pair{std::string{"states"}, settings.syncStates},
  };
  for (const auto& [prefix, enabled] : roots) {
    if (!enabled) {
      continue;
    }
    const auto root = prefix == "saves" ? paths.savesDirectory()
                                         : paths.statesDirectory();
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
      if (error) {
        return {.status = failure(Error::ioError,
          "A local synchronization directory could not be inspected."),
          .files = {}};
      }
      continue;
    }
    std::filesystem::recursive_directory_iterator iterator{
      root, std::filesystem::directory_options::skip_permission_denied, error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
      const auto entry = *iterator;
      const auto status = entry.symlink_status(error);
      if (error) {
        break;
      }
      if (std::filesystem::is_symlink(status)) {
        iterator.disable_recursion_pending();
        iterator.increment(error);
        continue;
      }
      if (std::filesystem::is_regular_file(status)) {
        const auto relative = std::filesystem::relative(entry.path(), root, error);
        if (error) {
          break;
        }
        const auto key = prefix + '/' + relative.generic_string();
        if (validFileKey(key)) {
          const auto size = entry.file_size(error);
          if (error) {
            break;
          }
          const auto maximum = maximumBytesForKey(key);
          if (size > maximum || total > maximumTotalBytes - size ||
              files.size() >= maximumFiles) {
            return {.status = failure(Error::dataTooLarge,
              "Local synchronized data exceeds a fixed file or total limit."),
              .files = {}};
          }
          const auto hash = fileSha256(entry.path(), size);
          if (hash.empty()) {
            return {.status = failure(Error::ioError,
              "A local synchronization file changed or could not be read."),
              .files = {}};
          }
          files.emplace(key, FileRecord{key, hash, size});
          total += size;
        }
      }
      iterator.increment(error);
    }
    if (error) {
      return {.status = failure(Error::ioError,
        "A local synchronization directory could not be scanned."), .files = {}};
    }
  }
  return {.status = {}, .files = std::move(files)};
}

LocalManifestStore::LocalManifestStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

LocalManifestResult LocalManifestStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumManifestBytes);
  if (!loaded.status) {
    return {.status = failure(Error::ioError, loaded.status.message),
      .manifest = {}};
  }
  if (!loaded.exists) {
    return {};
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())}, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = failure(Error::invalidManifest,
      "The local cloud baseline is not valid JSON."), .manifest = {}};
  }
  const auto root = document.object();
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        static_cast<int>(schemaVersion) ||
      !root.value(QStringLiteral("files")).isArray()) {
    return {.status = failure(Error::invalidManifest,
      "The local cloud baseline schema is not supported."), .manifest = {}};
  }
  const auto array = root.value(QStringLiteral("files")).toArray();
  if (array.size() > static_cast<qsizetype>(maximumFiles)) {
    return {.status = failure(Error::dataTooLarge,
      "The local cloud baseline has too many entries."), .manifest = {}};
  }
  LocalManifest manifest;
  for (const auto& value : array) {
    if (!value.isObject()) {
      return {.status = failure(Error::invalidManifest,
        "The local cloud baseline contains an invalid entry."), .manifest = {}};
    }
    const auto object = value.toObject();
    Baseline baseline{
      .key = object.value(QStringLiteral("key")).toString().toStdString(),
      .localSha256 = object.value(QStringLiteral("localSha256")).toString().toStdString(),
      .remoteSha256 = object.value(QStringLiteral("remoteSha256")).toString().toStdString(),
    };
    if (!validFileKey(baseline.key) ||
        (!baseline.localSha256.empty() && !validSha256(baseline.localSha256)) ||
        (!baseline.remoteSha256.empty() && !validSha256(baseline.remoteSha256)) ||
        manifest.files.contains(baseline.key)) {
      return {.status = failure(Error::invalidManifest,
        "The local cloud baseline contains invalid or duplicate values."),
        .manifest = {}};
    }
    manifest.files.emplace(baseline.key, std::move(baseline));
  }
  return {.status = {}, .manifest = std::move(manifest)};
}

Status LocalManifestStore::save(const LocalManifest& manifest) const
{
  if (manifest.files.size() > maximumFiles) {
    return failure(Error::dataTooLarge,
      "The local cloud baseline has too many entries.");
  }
  QJsonArray files;
  for (const auto& [key, baseline] : manifest.files) {
    if (key != baseline.key || !validFileKey(key) ||
        (!baseline.localSha256.empty() && !validSha256(baseline.localSha256)) ||
        (!baseline.remoteSha256.empty() && !validSha256(baseline.remoteSha256))) {
      return failure(Error::invalidManifest,
        "An invalid local cloud baseline cannot be saved.");
    }
    files.append(QJsonObject{
      {QStringLiteral("key"), QString::fromStdString(key)},
      {QStringLiteral("localSha256"), QString::fromStdString(baseline.localSha256)},
      {QStringLiteral("remoteSha256"), QString::fromStdString(baseline.remoteSha256)},
    });
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("files"), files},
  }}.toJson(QJsonDocument::Indented);
  const auto saved = writeFileAtomically(path_, {
    reinterpret_cast<const std::uint8_t*>(data.constData()),
    static_cast<std::size_t>(data.size())}, maximumManifestBytes);
  return saved ? Status{} : failure(Error::ioError, saved.message);
}

} // namespace genplusgx::cloud
