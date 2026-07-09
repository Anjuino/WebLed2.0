# 1. Создаем CA (ECDSA P-256)
echo "1. Создаем CA..."
openssl ecparam -genkey -name prime256v1 -out ca.key
openssl req -new -x509 -days 18250 -key ca.key -out ca.crt -subj "//CN=AnjeyCA"