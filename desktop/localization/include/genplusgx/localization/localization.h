#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QCoreApplication;
class QTranslator;

namespace genplusgx::localization {

inline constexpr std::string_view systemLanguage{"system"};
inline constexpr std::string_view englishLanguage{"en"};
inline constexpr std::string_view pseudoLanguage{"en_XA"};

enum class TranslationError : std::uint8_t {
  none,
  invalidPreference,
  catalogUnavailable,
  installationFailed,
};

struct TranslationStatus final {
  TranslationError error{TranslationError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TranslationError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct LanguageChoice final {
  std::string preference;
  QString displayName;
  bool testingOnly{false};
};

[[nodiscard]] inline constexpr bool isSupportedLanguagePreference(
  std::string_view preference) noexcept
{
  return preference == systemLanguage || preference == englishLanguage ||
         preference == pseudoLanguage;
}
[[nodiscard]] std::vector<LanguageChoice> languageChoices();
[[nodiscard]] QString languagePreferenceDisplayName(
  std::string_view preference);

class TranslationManager final {
public:
  explicit TranslationManager(
    QCoreApplication& application, QStringList catalogDirectories = {});
  ~TranslationManager();

  TranslationManager(const TranslationManager&) = delete;
  TranslationManager& operator=(const TranslationManager&) = delete;
  TranslationManager(TranslationManager&&) = delete;
  TranslationManager& operator=(TranslationManager&&) = delete;

  [[nodiscard]] TranslationStatus apply(
    std::string_view preference, QStringList systemUiLanguages = {});

  [[nodiscard]] const std::string& requestedLanguage() const noexcept;
  [[nodiscard]] const std::string& effectiveLanguage() const noexcept;
  [[nodiscard]] bool usedEnglishFallback() const noexcept;
  [[nodiscard]] QString loadedCatalogPath() const;

  [[nodiscard]] static QStringList defaultCatalogDirectories();

private:
  [[nodiscard]] TranslationStatus activateCatalog(
    const QString& localeName, bool preserveSystemLocaleOnFailure = false);
  void activateEnglish(bool fallback, bool preserveSystemLocale = false);

  QCoreApplication& application_;
  QStringList catalogDirectories_;
  std::unique_ptr<QTranslator> translator_;
  std::string requestedLanguage_{systemLanguage};
  std::string effectiveLanguage_{englishLanguage};
  QString loadedCatalogPath_;
  bool usedEnglishFallback_{false};
};

} // namespace genplusgx::localization
