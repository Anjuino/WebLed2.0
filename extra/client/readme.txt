# Из папки CA скопировать ca.crt ca.key
Сгенерировать клиентские

#!/bin/bash

# 1. Создаем ключ клиента
openssl ecparam -genkey -name prime256v1 -out client.key

# 2. Создаем конфиг для клиента
cat > client.cnf << EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = AntiPatrik

[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth
EOF

# 3. Генерируем CSR
openssl req -new -key client.key -out client.csr -config client.cnf

# 4. Подписываем CA
openssl x509 -req -days 3650 -in client.csr -CA ca.crt -CAkey ca.key -set_serial 02 -out client.crt -extensions v3_req -extfile client.cnf

# 5. Удаляем временные файлы
rm client.csr client.cnf

echo ""


Проверить соединение
python mtls_client.py