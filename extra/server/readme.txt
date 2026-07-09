#!/bin/bash

# Из папки CA скопировать ca.crt ca.key

echo "=== Генерация ECDSA сертификатов для ESP32 ==="

# 1. Создаем сертификат для ESP32 (ECDSA P-256)
echo "1. Создаем сертификат для ESP32..."
openssl ecparam -genkey -name prime256v1 -out esp32.key

# Создаем конфиг
cat > esp32.cnf << EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = esp-device

[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
EOF

# Генерируем CSR
openssl req -new -key esp32.key -out esp32.csr -config esp32.cnf

# Подписываем CA (18250 дней = 50 лет)
openssl x509 -req -days 18250 -in esp32.csr -CA ca.crt -CAkey ca.key -set_serial 01 -out esp32.crt -extensions v3_req -extfile esp32.cnf

# 2. Удаляем временные файлы
rm esp32.csr esp32.cnf

# 3. Добавляем нулевой байт
echo -n -e '\0' >> esp32.crt
echo -n -e '\0' >> esp32.key
echo -n -e '\0' >> ca.crt

# 4. Проверяем
echo ""
echo "=== ПРОВЕРКА ==="
openssl x509 -in esp32.crt -text -noout | grep -E "Version|Not Before|Not After"
echo ""
openssl verify -CAfile ca.crt esp32.crt
echo ""

