#include "genplusgx/cloud/cloud_credentials.h"

#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
#include <qtkeychain/keychain.h>
#endif

#include <QCryptographicHash>

#include <algorithm>
#include <utility>

namespace genplusgx::cloud {

class CredentialStore::Private final {};

CredentialStore::CredentialStore(QObject* parent)
    : QObject(parent), private_(std::make_unique<Private>())
{
}

CredentialStore::~CredentialStore() = default;

std::string CredentialStore::keyForAccount(
  const std::string& endpoint, const std::string& username)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArray::fromStdString(endpoint));
  constexpr char separator{'\0'};
  hash.addData(QByteArrayView{&separator, 1});
  hash.addData(QByteArray::fromStdString(username));
  return "cloud-webdav-" + hash.result().toHex().left(32).toStdString();
}

void CredentialStore::readPassword(const std::string& endpoint,
  const std::string& username, CredentialCallback callback)
{
  if (endpoint.empty() || username.empty()) {
    callback({false, false, {}, "The WebDAV account identity is incomplete."});
    return;
  }
#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
  auto* job = new QKeychain::ReadPasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForAccount(endpoint, username)));
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      if (completed->error() == QKeychain::EntryNotFound) {
        callback({true, false, {}, {}});
      } else if (completed->error() != QKeychain::NoError) {
        callback({false, false, {},
          "The operating-system credential store could not be read: " +
            completed->errorString().toStdString()});
      } else {
        const auto* read = static_cast<QKeychain::ReadPasswordJob*>(completed);
        callback({true, true, read->textData().toStdString(), {}});
      }
    });
  job->start();
#else
  callback({false, false, {},
    "This build does not include a secure credential store."});
#endif
}

void CredentialStore::writePassword(const std::string& endpoint,
  const std::string& username, std::string password, CredentialCallback callback)
{
  if (endpoint.empty() || username.empty() || password.empty() ||
      password.size() > 1'024U) {
    std::fill(password.begin(), password.end(), '\0');
    callback({false, false, {}, "The WebDAV account or password is invalid."});
    return;
  }
#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
  auto* job = new QKeychain::WritePasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForAccount(endpoint, username)));
  job->setInsecureFallback(false);
  job->setTextData(QString::fromStdString(password));
  std::fill(password.begin(), password.end(), '\0');
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      if (completed->error() != QKeychain::NoError) {
        callback({false, false, {},
          "The operating-system credential store could not save the password: " +
            completed->errorString().toStdString()});
      } else {
        callback({true, true, {}, {}});
      }
    });
  job->start();
#else
  std::fill(password.begin(), password.end(), '\0');
  callback({false, false, {},
    "This build does not include a secure credential store."});
#endif
}

void CredentialStore::deletePassword(const std::string& endpoint,
  const std::string& username, CredentialCallback callback)
{
  if (endpoint.empty() || username.empty()) {
    callback({false, false, {}, "The WebDAV account identity is incomplete."});
    return;
  }
#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
  auto* job = new QKeychain::DeletePasswordJob{
    QStringLiteral("org.genesisplusgx.gui"), this};
  job->setKey(QString::fromStdString(keyForAccount(endpoint, username)));
  job->setInsecureFallback(false);
  connect(job, &QKeychain::Job::finished, this,
    [callback = std::move(callback)](QKeychain::Job* completed) mutable {
      if (completed->error() != QKeychain::NoError &&
          completed->error() != QKeychain::EntryNotFound) {
        callback({false, false, {},
          "The operating-system credential store could not remove the password: " +
            completed->errorString().toStdString()});
      } else {
        callback({true, false, {}, {}});
      }
    });
  job->start();
#else
  callback({false, false, {},
    "This build does not include a secure credential store."});
#endif
}

} // namespace genplusgx::cloud
