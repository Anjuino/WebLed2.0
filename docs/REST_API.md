# WebLed2.0 REST API

Полное описание HTTP-эндпоинтов, которые отдаёт ESP32-S3 (HTTP, порт 80).

Базовый URL в примерах: `http://<ip-esp>` (например `http://192.168.0.50`).
Все ответы — JSON с `Content-Type: application/json`, кроме случаев, где явно указан другой формат.

Успешный ответ = HTTP 200, ошибки = 400/500 с текстовым телом (не JSON).

---

## Сводная таблица

| Метод | URL                          | Назначение                           |
|-------|------------------------------|--------------------------------------|
| GET   | `/api/led`                   | Получить текущее состояние LED + список доступных эффектов |
| POST  | `/api/led`                   | Установить состояние LED             |
| GET   | `/api/deviceinfo`            | Температура CPU, память, WiFi-сети   |
| POST  | `/api/ota/local-upload`      | Залить прошивку прямо через REST (тело — сырой `.bin`) |
| POST  | `/api/ota/remote-upload`     | Запустить фоновую задачу — стянуть прошивку с удалённого сервера |

---

## `GET /api/led`

Возвращает текущее состояние LED-полосы и список всех доступных эффектов.

### Параметры

Нет.

### Тело ответа

```json
{
  "speed":      <number>,            // скорость эффекта (0..255, см. реализацию LED)
  "mode":       <number>,            // id текущего эффекта
  "brightness": <number>,            // яркость (0..255)
  "color": {
    "r": <number>,                   // красный (0..255)
    "g": <number>,                   // зелёный  (0..255)
    "b": <number>                    // синий    (0..255)
  },
  "modes": [
    { "id_mode": <number>, "name": "<string>" },
    { "id_mode": <number>, "name": "<string>" }
  ]
}
```

`modes` отсортирован по `id_mode` по возрастанию. Состав определяется в `components/led/led.cpp` через массив `LedEffect` (там же задаётся максимально допустимый `id_mode`).

### Коды ответов

| Код | Когда |
|-----|-------|
| 200 | OK |
| 500 | Внутренняя ошибка (не удалось собрать JSON) |

### Пример

```bash
curl http://192.168.0.50/api/led
```

```json
{
  "speed": 128,
  "mode": 3,
  "brightness": 200,
  "color": { "r": 255, "g": 64, "b": 0 },
  "modes": [
    { "id_mode": 0, "name": "Static"   },
    { "id_mode": 1, "name": "Rainbow"  },
    { "id_mode": 2, "name": "Pulse"    },
    { "id_mode": 3, "name": "Equalizer"}
  ]
}
```

---

## `POST /api/led`

Устанавливает новое состояние LED. Все поля опциональны — передавай только те, что хочешь изменить.

### Тело запроса

```json
{
  "speed":      <number>,                        // опционально, целое
  "mode":       <number>,                        // опционально, целое; должен быть в списке modes
  "brightness": <number>,                        // опционально, целое 0..255
  "color": {                                      // опционально целиком
    "r": <number>,
    "g": <number>,
    "b": <number>
  }
}
```

### Тело ответа (успех)

```json
{
  "success": true
}
```

### Коды ответов

| Код | Когда |
|-----|-------|
| 200 | Изменения приняты |
| 400 | `Empty body` — тело пустое |
| 400 | `Invalid JSON` — тело не парсится как JSON |
| 400 | `Unknown mode id` — передан `mode`, которого нет в `modes` |
| 500 | Внутренняя ошибка |

### Поведение

- Любое отсутствующее поле = оставить как было.
- `color` без полного набора `r`/`g`/`b` (любое не-число) = игнорируется целиком.
- Передан `mode` без проверки в списке `modes` → 400.

### Примеры

```bash
# Сменить режим
curl -X POST http://192.168.0.50/api/led \
  -H 'Content-Type: application/json' \
  -d '{"mode": 2}'

# Установить яркость и цвет
curl -X POST http://192.168.0.50/api/led \
  -H 'Content-Type: application/json' \
  -d '{"brightness": 100, "color": {"r": 0, "g": 128, "b": 255}}'

# Несколько параметров сразу
curl -X POST http://192.168.0.50/api/led \
  -H 'Content-Type: application/json' \
  -d '{"speed": 200, "mode": 3, "brightness": 180}'
```

---

## `GET /api/deviceinfo`

Диагностика: температура чипа, состояние памяти, видимые WiFi-сети.

### Параметры

Нет.

### Тело ответа

```json
{
  "cpu_temp":      <number>,          // °C, округлено до 2 знаков
  "memory_info": {
    "iram_total":   <number>,         // всего IRAM в байтах
    "iram_free":    <number>,         // свободно сейчас
    "iram_min_free":<number>          // минимум свободного за время работы
  },
  "wifi_networks": [
    {
      "ssid":  "<string>",            // SSID сети (UTF-8, может быть пустым для hidden)
      "rssi":  <number>,             // уровень сигнала, dBm (обычно -30..-90)
      "bssid": "AA:BB:CC:DD:EE:FF"   // MAC AP в нижнем регистре, через двоеточие
    }
  ]
}
```

### Коды ответов

| Код | Когда |
|-----|-------|
| 200 | OK |
| 500 | Внутренняя ошибка |

### Замечания

- `wifi_networks` — это результат **свежего сканирования** при каждом запросе (занимает ~1–3 секунды, запрос блокирует хендлер).
- `cpu_temp` снимается с внутреннего сенсора ESP32-S3, точность ±1-2°C.

### Пример

```bash
curl http://192.168.0.50/api/deviceinfo
```

```json
{
  "cpu_temp": 42.50,
  "memory_info": {
    "iram_total":    131072,
    "iram_free":      53248,
    "iram_min_free":  41984
  },
  "wifi_networks": [
    { "ssid": "HomeWiFi",   "rssi": -54, "bssid": "a4:2b:b0:11:22:33" },
    { "ssid": "Neighbor",   "rssi": -78, "bssid": "00:1a:2b:33:44:55" }
  ]
}
```

---

## `POST /api/ota/local-upload`

Прямая заливка `.bin` через HTTP. Тело запроса — сырой бинарь (не JSON).

### Тело запроса

- **Content-Type**: `application/octet-stream` (любой, не проверяется)
- **Content-Length**: обязателен
- **Тело**: байты `.bin`

### Тело ответа (успех)

```json
{
  "success": true,
  "message": "OTA update successful"
}
```

После ответа ESP ждёт ~2 секунды и ребутается — на запрос уже никто не ответит.

### Коды ответов

| Код | Когда |
|-----|-------|
| 200 | Прошивка принята, записана, ESP ребутнётся |
| 400 | `Content-Length header required` |
| 500 | `OTA failed` — ошибка записи (нет OTA-партиции, размер > партиции, прочитал < чем обещано и т.п.) |

### Пример

```bash
curl -X POST http://192.168.0.50/api/ota/local-upload \
  --data-binary @build/WebLed2.0.bin \
  -H 'Content-Type: application/octet-stream'
```

После `200 OK` в ответе ESP ребутнётся через 2 секунды.

---

## `POST /api/ota/remote-upload`

Запускает фоновую задачу, которая сама стянет прошивку с удалённого сервера (`OTA_REMOTE_URL` в `components/server/src/ota.cpp`) и прошьётся.

Сам хендлер отвечает **сразу**, не дожидаясь конца скачивания. Чтобы узнать результат — следи за логом ESP или жди `POST /ack` на сервере.

### Параметры / тело запроса

Нет. Тело игнорируется.

### Тело ответа (успех)

```json
{
  "started": true,
  "message": "OTA fetch started in background"
}
```

### Тело ответа (ошибка)

```json
{
  "started": false,
  "error": "OTA already in progress"
}
```

или

```json
{
  "started": false,
  "error": "Failed to start OTA task"
}
```

### Коды ответов

| Код | Когда |
|-----|-------|
| 200 | Всегда (в т.ч. при ошибке запуска задачи — JSON покажет причину) |
| 500 | Внутренняя ошибка отправки JSON |

### Что делает ESP дальше

1. Берёт URL из `#define OTA_REMOTE_URL` в `ota.cpp`
2. Добавляет query: `?current=<FIRMWARE_VERSION>&chipid=<MAC>`
3. GET-ит этот URL (`esp_http_client`)
4. Если сервер ответил 200 + `Content-Length` — качает в OTA-партицию через общий стрим `OtaManager::write_stream`
5. На успехе шлёт `POST OTA_ACK_URL` с JSON `{"status":"installed","chipid":"..."}`
6. На ошибке шлёт тот же ack с `{"status":"failed","error":"...","chipid":"..."}`
7. На успехе — `vTaskDelay(3s)` + `esp_restart()`

### Связанные константы (в `ota.cpp`)

```c
#define OTA_REMOTE_URL  "http://192.168.0.113:5000/getfirmware"
#define OTA_ACK_URL     "http://192.168.0.113:5000/ack"
#define FIRMWARE_VERSION 1
```

`FIRMWARE_VERSION` шлётся в `?current=` — сервер сравнивает со своей версией и решает, отдавать ли `.bin`.

### Пример

```bash
curl -X POST http://192.168.0.50/api/ota/remote-upload
```

Ответ приходит сразу. Скачивание и прошивка идут в фоне ~1–10 секунд.

---

## Общие ошибки

| Код | Когда |
|-----|-------|
| 400 | Тело пустое или не парсится как JSON |
| 500 | Внутренняя ошибка (не собрался cJSON, не хватило памяти и т.п.) |

Все ответы с 4xx/5xx (кроме `/api/ota/local-upload`) возвращают **plain text** с коротким описанием, не JSON.

---

## Как устроен `Content-Type`

Все успешные JSON-ответы идут с заголовком `Content-Type: application/json`.
Ошибочные plain-text ответы идут с дефолтным `text/plain` от `httpd_resp_send_err`.
`/api/ota/local-upload` в успехе — JSON, в ошибке — `text/plain`.

---

## Что не входит в этот API (но стоит знать)

- **mDNS**: имя хоста задаётся `wifi->get_mdns_name()` (например `espledae7ca8.local`). Используй его вместо IP если mDNS работает в твоей сети.
- **CORS**: сервер не отдаёт CORS-заголовки. Если делаешь веб-морду на другом origin — добавь `Access-Control-Allow-Origin` через прокси или модифицируй `send_resp_json` в `http_json.cpp`.
- **Аутентификация**: нет ни пароля, ни токенов. Если ESP в общей сети — кто угодно может перепрошить.