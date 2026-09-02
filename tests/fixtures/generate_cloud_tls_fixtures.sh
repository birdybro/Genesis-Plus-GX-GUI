#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 OUTPUT_DIRECTORY" >&2
  exit 2
fi

output_directory=$1
mkdir -p "$output_directory"
work_directory=$(mktemp -d)
trap 'rm -rf -- "$work_directory"' EXIT
umask 077

cat >"$work_directory/ca-extensions.cnf" <<'EOF'
[ca_extensions]
basicConstraints = critical,CA:TRUE,pathlen:0
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
EOF

cat >"$work_directory/server-extensions.cnf" <<'EOF'
[server_extensions]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:localhost,IP:127.0.0.1
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
EOF

openssl genpkey -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out "$work_directory/ca-key.pem" >/dev/null 2>&1
openssl req -new -x509 -sha256 -days 3650 \
  -key "$work_directory/ca-key.pem" \
  -subj "/CN=Genesis Plus GX GUI Cloud Test CA" \
  -extensions ca_extensions \
  -config "$work_directory/ca-extensions.cnf" \
  -out "$work_directory/ca.pem"

openssl genpkey -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out "$work_directory/server-key.pem" >/dev/null 2>&1
openssl req -new -sha256 \
  -key "$work_directory/server-key.pem" \
  -subj "/CN=localhost" \
  -out "$work_directory/server.csr"
openssl x509 -req -sha256 -days 824 \
  -in "$work_directory/server.csr" \
  -CA "$work_directory/ca.pem" \
  -CAkey "$work_directory/ca-key.pem" \
  -CAcreateserial \
  -extensions server_extensions \
  -extfile "$work_directory/server-extensions.cnf" \
  -out "$work_directory/server.pem"

openssl x509 -in "$work_directory/ca.pem" -outform DER \
  -out "$output_directory/cloud-test-ca.der"
openssl x509 -in "$work_directory/server.pem" -outform DER \
  -out "$output_directory/cloud-test-server.der"
openssl rsa -in "$work_directory/server-key.pem" -traditional -outform DER \
  -out "$output_directory/cloud-test-server-key.der" >/dev/null 2>&1
chmod 0644 \
  "$output_directory/cloud-test-ca.der" \
  "$output_directory/cloud-test-server.der" \
  "$output_directory/cloud-test-server-key.der"
