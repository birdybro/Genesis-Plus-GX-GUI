#include "genplusgx/debug_analysis.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>

namespace genplusgx {
namespace {

std::size_t widthBytes(DebugValueWidth width) noexcept
{
  return static_cast<std::size_t>(width);
}

bool matches(
  DebugRamComparison comparison,
  std::int64_t current,
  std::int64_t previous,
  std::int64_t value) noexcept
{
  switch (comparison) {
    case DebugRamComparison::equalTo: return current == value;
    case DebugRamComparison::notEqualTo: return current != value;
    case DebugRamComparison::changed: return current != previous;
    case DebugRamComparison::unchanged: return current == previous;
    case DebugRamComparison::increased: return current > previous;
    case DebugRamComparison::decreased: return current < previous;
    case DebugRamComparison::greaterThan: return current > value;
    case DebugRamComparison::lessThan: return current < value;
  }
  return false;
}

std::string_view trim(std::string_view value) noexcept
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
      value.front() == '\r')) {
    value.remove_prefix(1U);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
      value.back() == '\r')) {
    value.remove_suffix(1U);
  }
  return value;
}

bool parseAddress(
  std::string_view token,
  CoreDebugCpu cpu,
  std::uint32_t& address) noexcept
{
  token = trim(token);
  if (token.starts_with("0x") || token.starts_with("0X")) {
    token.remove_prefix(2U);
  } else if (token.starts_with('$')) {
    token.remove_prefix(1U);
  }
  if (token.empty()) {
    return false;
  }
  std::uint32_t parsed = 0U;
  const auto result = std::from_chars(
    token.data(), token.data() + token.size(), parsed, 16);
  const auto maximum = cpu == CoreDebugCpu::m68k
    ? 0x00FF'FFFFU : 0x0000'FFFFU;
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size() ||
      parsed > maximum) {
    return false;
  }
  address = parsed;
  return true;
}

bool validSymbolName(std::string_view name) noexcept
{
  if (name.empty() || name.size() > DebugSymbolTable::maximumNameBytes) {
    return false;
  }
  return std::ranges::all_of(name, [](unsigned char character) {
    return character >= 0x21U && character <= 0x7EU &&
      character != '"' && character != '\\';
  });
}

std::string jsonEscape(std::string_view value)
{
  std::string output;
  output.reserve(value.size());
  for (const auto character : value) {
    switch (character) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U) {
          constexpr char digits[] = "0123456789abcdef";
          output += "\\u00";
          output += digits[(static_cast<unsigned char>(character) >> 4U) & 0xFU];
          output += digits[static_cast<unsigned char>(character) & 0xFU];
        } else {
          output += character;
        }
        break;
    }
  }
  return output;
}

} // namespace

bool debugReadValue(
  std::span<const std::uint8_t> memory,
  std::uint32_t offset,
  DebugValueWidth width,
  DebugValueEndian endian,
  std::uint32_t& output) noexcept
{
  const auto bytes = widthBytes(width);
  const auto start = static_cast<std::size_t>(offset);
  if ((bytes != 1U && bytes != 2U && bytes != 4U) ||
      start > memory.size() || bytes > memory.size() - start) {
    output = 0U;
    return false;
  }
  output = 0U;
  if (endian == DebugValueEndian::big) {
    for (std::size_t index = 0U; index < bytes; ++index) {
      output = (output << 8U) | memory[start + index];
    }
  } else {
    for (std::size_t index = bytes; index > 0U; --index) {
      output = (output << 8U) | memory[start + index - 1U];
    }
  }
  return true;
}

std::int64_t debugInterpretValue(
  std::uint32_t value,
  DebugValueWidth width,
  DebugValueFormat format) noexcept
{
  if (format == DebugValueFormat::unsignedInteger) {
    return static_cast<std::int64_t>(value);
  }
  switch (width) {
    case DebugValueWidth::byte:
      return static_cast<std::int8_t>(value & 0xFFU);
    case DebugValueWidth::word:
      return static_cast<std::int16_t>(value & 0xFFFFU);
    case DebugValueWidth::longWord:
      return static_cast<std::int32_t>(value);
  }
  return 0;
}

bool DebugRamSearch::begin(
  std::span<const std::uint8_t> memory,
  DebugValueWidth width,
  DebugValueEndian endian)
{
  clear();
  const auto bytes = widthBytes(width);
  if ((bytes != 1U && bytes != 2U && bytes != 4U) || memory.size() < bytes) {
    return false;
  }
  const auto count = memory.size() - bytes + 1U;
  if (count > maximumCandidates) {
    return false;
  }
  width_ = width;
  endian_ = endian;
  candidates_.reserve(count);
  for (std::size_t offset = 0U; offset < count; ++offset) {
    std::uint32_t value = 0U;
    if (!debugReadValue(memory, static_cast<std::uint32_t>(offset),
          width_, endian_, value)) {
      clear();
      return false;
    }
    candidates_.push_back({
      .offset = static_cast<std::uint32_t>(offset),
      .previousValue = value,
    });
  }
  active_ = true;
  return true;
}

bool DebugRamSearch::filter(
  std::span<const std::uint8_t> memory,
  DebugRamComparison comparison,
  DebugValueFormat format,
  std::int64_t value)
{
  if (!active_) {
    return false;
  }
  std::vector<DebugRamCandidate> retained;
  retained.reserve(candidates_.size());
  for (const auto& candidate : candidates_) {
    std::uint32_t currentValue = 0U;
    if (!debugReadValue(
          memory, candidate.offset, width_, endian_, currentValue)) {
      return false;
    }
    const auto current = debugInterpretValue(currentValue, width_, format);
    const auto previous = debugInterpretValue(
      candidate.previousValue, width_, format);
    if (matches(comparison, current, previous, value)) {
      retained.push_back({
        .offset = candidate.offset,
        .previousValue = currentValue,
      });
    }
  }
  candidates_ = std::move(retained);
  return true;
}

void DebugRamSearch::clear() noexcept
{
  candidates_.clear();
  active_ = false;
}

bool DebugRamSearch::active() const noexcept
{
  return active_;
}

DebugValueWidth DebugRamSearch::width() const noexcept
{
  return width_;
}

DebugValueEndian DebugRamSearch::endian() const noexcept
{
  return endian_;
}

const std::vector<DebugRamCandidate>& DebugRamSearch::candidates() const noexcept
{
  return candidates_;
}

bool DebugSymbolTable::load(std::string_view text, std::string& error)
{
  error.clear();
  if (text.size() > maximumFileBytes || text.find('\0') != std::string_view::npos) {
    error = "The symbol file is larger than 1 MiB or contains a NUL byte.";
    return false;
  }
  std::vector<DebugSymbol> parsed;
  std::size_t lineNumber = 0U;
  while (!text.empty()) {
    ++lineNumber;
    const auto newline = text.find('\n');
    auto line = trim(text.substr(0U, newline));
    text = newline == std::string_view::npos
      ? std::string_view{} : text.substr(newline + 1U);
    const auto comment = line.find_first_of("#;");
    if (comment != std::string_view::npos) {
      line = trim(line.substr(0U, comment));
    }
    if (line.empty()) {
      continue;
    }
    std::vector<std::string_view> fields;
    while (!line.empty()) {
      const auto delimiter = line.find_first_of(" \t");
      fields.push_back(line.substr(0U, delimiter));
      line = delimiter == std::string_view::npos
        ? std::string_view{} : trim(line.substr(delimiter + 1U));
    }
    CoreDebugCpu cpu = CoreDebugCpu::m68k;
    std::size_t addressField = 0U;
    if (!fields.empty() && (fields.front() == "m68k" ||
        fields.front() == "68k" || fields.front() == "z80")) {
      cpu = fields.front() == "z80" ? CoreDebugCpu::z80 : CoreDebugCpu::m68k;
      addressField = 1U;
    }
    if (fields.size() != addressField + 2U) {
      error = "Invalid symbol record on line " + std::to_string(lineNumber) +
        ": expected '[m68k|z80] hexadecimal-address name'.";
      return false;
    }
    std::uint32_t address = 0U;
    if (!parseAddress(fields[addressField], cpu, address) ||
        !validSymbolName(fields[addressField + 1U])) {
      error = "Invalid symbol address or name on line " +
        std::to_string(lineNumber) + ".";
      return false;
    }
    parsed.push_back({cpu, address, std::string{fields[addressField + 1U]}});
    if (parsed.size() > maximumSymbols) {
      error = "The symbol file contains more than 65536 records.";
      return false;
    }
  }
  std::ranges::sort(parsed, [](const auto& left, const auto& right) {
    if (left.cpu != right.cpu) {
      return left.cpu < right.cpu;
    }
    if (left.address != right.address) {
      return left.address < right.address;
    }
    return left.name < right.name;
  });
  parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
  symbols_ = std::move(parsed);
  return true;
}

bool DebugSymbolTable::loadFile(
  const std::filesystem::path& path,
  std::string& error)
{
  error.clear();
  std::error_code filesystemError;
  const auto size = std::filesystem::file_size(path, filesystemError);
  if (filesystemError || size > maximumFileBytes) {
    error = filesystemError
      ? "The symbol file could not be inspected."
      : "The symbol file is larger than 1 MiB.";
    return false;
  }
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    error = "The symbol file could not be opened.";
    return false;
  }
  std::string contents(static_cast<std::size_t>(size), '\0');
  stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!stream || stream.peek() != std::ifstream::traits_type::eof()) {
    error = "The symbol file changed or could not be read completely.";
    return false;
  }
  return load(contents, error);
}

void DebugSymbolTable::clear() noexcept
{
  symbols_.clear();
}

const DebugSymbol* DebugSymbolTable::find(
  CoreDebugCpu cpu,
  std::uint32_t address) const noexcept
{
  const auto found = std::ranges::lower_bound(
    symbols_, std::pair{cpu, address}, {}, [](const auto& symbol) {
      return std::pair{symbol.cpu, symbol.address};
    });
  return found != symbols_.end() && found->cpu == cpu &&
      found->address == address
    ? &*found : nullptr;
}

const std::vector<DebugSymbol>& DebugSymbolTable::symbols() const noexcept
{
  return symbols_;
}

std::string debugTraceJson(
  std::span<const CoreDebugTraceEntry> entries,
  const DebugSymbolTable& symbols,
  std::uint64_t droppedEntries)
{
  std::ostringstream output;
  output << "{\n  \"schema\": 1,\n  \"droppedEntries\": " << droppedEntries
    << ",\n  \"entries\": [";
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    output << (index == 0U ? "\n" : ",\n")
      << "    {\"sequence\": " << entry.sequence
      << ", \"cpu\": \""
      << (entry.cpu == CoreDebugCpu::m68k ? "m68k" : "z80")
      << "\", \"address\": \"0x" << std::hex << entry.address << std::dec
      << "\", \"cycles\": " << entry.cycles;
    if (const auto* symbol = symbols.find(entry.cpu, entry.address)) {
      output << ", \"symbol\": \"" << jsonEscape(symbol->name) << '"';
    }
    output << '}';
  }
  output << (entries.empty() ? "" : "\n") << "  ]\n}\n";
  return output.str();
}

} // namespace genplusgx
