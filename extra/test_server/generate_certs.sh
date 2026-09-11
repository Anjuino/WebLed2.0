#!/usr/bin/env bash
# Генерирует корневой CA и подписанный им серверный сертификат для тестового
# HTTPS-сервера прошивок (firmware_server.py).
#
# Использование:
#   ./generate_certs.sh [IP-или-хост-сервера]
# По умолчанию IP берётся 192.168.0.113 — как в OTA_REMOTE_URL на ESP.
# Если сервер живёт на другом адресе, передайте его первым аргументом —
# он попадёт в SAN сертификата, иначе ESP не пройдёт проверку имени при TLS.
#
# После генерации CA-сертификат (ca.crt, публичный, без ключа) автоматически
# копируется в components/server/cert/remote_ca.crt — оттуда его заберёт
# сборка ESP (см. CMakeLists.txt компонента server) для проверки сервера
# прошивок при удалённом OTA.
#
# ca.key — приватный ключ CA — НИКУДА, кроме этой папки, копировать не нужно.

set -euo pipefail

# Git Bash (MSYS) пытается конвертировать "/CN=..." в путь Windows — отключаем.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="$SCRIPT_DIR/certs"
IP="${1:-192.168.0.113}"
DAYS_CA=18250     # ~50 лет
DAYS_SERVER=18250 # ~50 лет — не привязан к имени (skip_cert_common_name_check), перевыпускать незачем

mkdir -p "$CERT_DIR"
cd "$CERT_DIR"

echo "== Корневой CA =="
openssl genrsa -out ca.key 2048
openssl req -x509 -new -nodes -key ca.key -sha256 -days "$DAYS_CA" \
  -subj "/CN=WebLed2.0 Test CA" -out ca.crt

echo "== Ключ и запрос сервера (IP/host = $IP) =="
openssl genrsa -out server.key 2048
openssl req -new -key server.key -subj "/CN=$IP" -out server.csr

cat > server.ext <<EOF
subjectAltName = IP:$IP
EOF

echo "== Подпись серверного сертификата корневым CA =="
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days "$DAYS_SERVER" -sha256 -extfile server.ext

rm -f server.csr server.ext ca.srl

echo
echo "Готово:"
echo "  $CERT_DIR/ca.key      — приватный ключ CA (никому не отдавать)"
echo "  $CERT_DIR/ca.crt      — публичный сертификат CA"
echo "  $CERT_DIR/server.key  — приватный ключ сервера прошивок"
echo "  $CERT_DIR/server.crt  — сертификат сервера прошивок (подписан ca.crt)"

DEST="$SCRIPT_DIR/../../components/server/cert/remote_ca.crt"
cp ca.crt "$DEST"
echo
echo "CA-сертификат скопирован в $DEST — заберёт сборка ESP"
