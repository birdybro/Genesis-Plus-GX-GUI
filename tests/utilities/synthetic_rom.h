#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::test {

enum class SyntheticSegaCdRegion : std::uint8_t {
  usa,
  europe,
  japan,
};

[[nodiscard]] std::vector<std::uint8_t> makeGenesisRamMarkerRom();
[[nodiscard]] std::vector<std::uint8_t> makeGenesisSramWriterRom();
[[nodiscard]] std::vector<std::uint8_t> makeGenesisBootRom();
[[nodiscard]] std::vector<std::uint8_t> makeZ80RamMarkerRom(
  std::uint8_t marker = 0x5AU);
[[nodiscard]] std::vector<std::uint8_t> makeZ80BootRom(
  std::size_t size = 1U * 1024U);
[[nodiscard]] std::vector<std::uint8_t> makeSegaCdBios();
[[nodiscard]] std::vector<std::uint8_t> makeSegaCdDiscImage(
  SyntheticSegaCdRegion region = SyntheticSegaCdRegion::usa);

struct SyntheticZipEntry final {
  std::string name;
  std::vector<std::uint8_t> data;
};

[[nodiscard]] bool writeZipFixture(
  const std::filesystem::path& path,
  std::vector<SyntheticZipEntry> entries,
  bool compressed = true);

class TemporaryFixture final {
public:
  TemporaryFixture(std::vector<std::uint8_t> bytes, std::string_view extension);
  ~TemporaryFixture();

  TemporaryFixture(const TemporaryFixture&) = delete;
  TemporaryFixture& operator=(const TemporaryFixture&) = delete;
  TemporaryFixture(TemporaryFixture&&) = delete;
  TemporaryFixture& operator=(TemporaryFixture&&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::test
