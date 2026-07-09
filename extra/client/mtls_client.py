import urllib.request
import ssl
import os
import sys

# ===== НАСТРОЙКИ (меняй тут) =====
IP = "192.168.0.101"           # IP ESP32
PORT = 443                      # Порт
PATH = "/api/data"              # Путь
CACERT = "ca.crt"               # Корневой сертификат CA
CERT = "client.crt"             # Клиентский сертификат
KEY = "client.key"              # Ключ клиента
# ==================================

def main():
    url = f"https://{IP}:{PORT}{PATH}"

    # Проверка наличия файлов
    for f in [CACERT, CERT, KEY]:
        if not os.path.exists(f):
            print(f"Файл {f} не найден!")
            sys.exit(1)

    # Создаём контекст
    context = ssl.create_default_context(cafile=CACERT)
    context.load_cert_chain(certfile=CERT, keyfile=KEY)
    context.check_hostname = False

    print(f"Проверка сервера: ВКЛ (через {CACERT})")
    print(f"Проверка имени: ВЫКЛ")
    print(f"Клиентский сертификат: {CERT} загружен")

    try:
        print(f"\nПодключение к {url}")
        req = urllib.request.Request(url)

        with urllib.request.urlopen(req, timeout=5, context=context) as response:
            print(f"\nСтатус: {response.status}")
            print(f"Ответ: {response.read().decode()}")

    except urllib.error.URLError as e:
        print(f"\nОшибка: {e}")
        if "certificate" in str(e).lower():
            print("\nВозможные причины:")
            print("1. Нет клиентского сертификата на сервере")
            print("2. Сертификат не подписан этим CA")
            print("3. Неправильный ключ")
    except Exception as e:
        print(f"\nОшибка: {e}")

if __name__ == "__main__":
    main()