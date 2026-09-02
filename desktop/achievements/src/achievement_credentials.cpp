#include "genplusgx/achievements/achievement_credentials.h"

#include "genplusgx/achievements/achievement_settings.h"

#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
#include <qtkeychain/keychain.h>
#endif

#include <algorithm>
#include <cctype>
#include <utility>

namespace genplusgx::achievements {

class CredentialStore::Private final {};

CredentialStore::CredentialStore(QObject* parent)
    : QObject(parent), private_(std::make_unique<Private>())
{
}

CredentialStore::~CredentialStore() = default;

std::string CredentialStore::keyForUsername(const std::string& username)
{
  std::string normalized;
  normalized.reserve(username.size() + 24U);
  normalized = "retroachievements-token-";
  std::transform(username.begin(), username.end(),
    std::back_inserter(normalized), [](unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
  return normalized;
}

void CredentialStore::readToken(
  const std::string& username, CredentialCallback callback)
{
  if (!validUsername(username)) {
    callback(CredentialResult{
      .succeeded = false, .found = false, .token = {},
      .detail = "The RetroAchievements username is invalid."});
    return;
  }
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  auto* job = new QKeychain::ReadPasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForUsername(username)));
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      const auto error = completed->error();
      if (error == QKeychain::EntryNotFound) {
        callback(CredentialResult{
          .succeeded = true, .found = false, .token = {}, .detail = {}});
        return;
      }
      if (error != QKeychain::NoError) {
        callback(CredentialResult{
          .succeeded = false,
          .found = false,
          .token = {},
          .detail = "The operating-system credential store could not be read: " +
            completed->errorString().toStdString(),
        });
        return;
      }
      const auto* read = static_cast<QKeychain::ReadPasswordJob*>(completed);
      callback(CredentialResult{
        .succeeded = true,
        .found = true,
        .token = read->textData().toStdString(),
        .detail = {},
      });
    });
  job->start();
#else
  callback(CredentialResult{
    .succeeded = false, .found = false, .token = {},
    .detail = "This build does not include a secure credential store."});
#endif
}

void CredentialStore::writeToken(const std::string& username,
  std::string token,
  CredentialCallback callback)
{
  if (!validUsername(username) || token.empty()) {
    std::fill(token.begin(), token.end(), '\0');
    callback(CredentialResult{
      .succeeded = false, .found = false, .token = {},
      .detail = "The RetroAchievements username or token is invalid."});
    return;
  }
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  auto* job = new QKeychain::WritePasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForUsername(username)));
  job->setInsecureFallback(false);
  job->setTextData(QString::fromStdString(token));
  std::fill(token.begin(), token.end(), '\0');
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      if (completed->error() != QKeychain::NoError) {
        callback(CredentialResult{
          .succeeded = false,
          .found = false,
          .token = {},
          .detail = "The operating-system credential store could not save the session: " +
            completed->errorString().toStdString(),
        });
        return;
      }
      callback(CredentialResult{
        .succeeded = true, .found = true, .token = {}, .detail = {}});
    });
  job->start();
#else
  std::fill(token.begin(), token.end(), '\0');
  callback(CredentialResult{
    .succeeded = false, .found = false, .token = {},
    .detail = "This build does not include a secure credential store."});
#endif
}

void CredentialStore::deleteToken(
  const std::string& username, CredentialCallback callback)
{
  if (!validUsername(username)) {
    callback(CredentialResult{
      .succeeded = false, .found = false, .token = {},
      .detail = "The RetroAchievements username is invalid."});
    return;
  }
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  auto* job = new QKeychain::DeletePasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForUsername(username)));
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      if (completed->error() != QKeychain::NoError &&
          completed->error() != QKeychain::EntryNotFound) {
        callback(CredentialResult{
          .succeeded = false,
          .found = false,
          .token = {},
          .detail = "The operating-system credential store could not remove the session: " +
            completed->errorString().toStdString(),
        });
        return;
      }
      callback(CredentialResult{
        .succeeded = true, .found = false, .token = {}, .detail = {}});
    });
  job->start();
#else
  callback(CredentialResult{
    .succeeded = false, .found = false, .token = {},
    .detail = "This build does not include a secure credential store."});
#endif
}

} // namespace genplusgx::achievements
