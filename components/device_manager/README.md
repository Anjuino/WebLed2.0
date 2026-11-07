# device_manager

Всё «про устройство» в одном месте:
- Memory / temperature (исходная функциональность)
- Чтение метаданных (project, serial) из raw-раздела `meta`
- Версия прошивки (`firmware_version()`)
- Скрипты для бампа версии и прошивки метаданных

## Зависимости

`freertos driver esp_partition`

## Что внутри

```
components/device_manager/
├── CMakeLists.txt
├── include/
│   ├── DeviceManager.h     (memory + device_meta_* + MAX_LEN'ы)
│   └── version.h           (firmware_version() — автоинкремент при сборке)
├── src/
│   └── DeviceManager.cpp
├── bump_version.py         (инкремент build-номера)
└── provision.py            (запись project+serial в meta)
```

## API

### Memory
```c
#include "DeviceManager.h"
void memory_task(void *pvParameters);   // запустить в таске, логирует раз в 2 мин
memory_info getstatus(void);            // одноразовый снимок
```

### Метаданные устройства (project + serial)
```c
#include "DeviceManager.h"

char serial[32];
char project[64];
if (device_meta_read_serial(serial, sizeof(serial)) &&
    device_meta_read_project(project, sizeof(project))) {
    ESP_LOGI("boot", "device: %s, %s", project, serial);   // "device: Led, SN-00001"
} else {
    ESP_LOGW("boot", "not provisioned");
}

if (device_meta_is_provisioned()) { ... }   // только факт записи
```

### Версия прошивки
```c
#include "DeviceManager.h"   // подтягивает version.h
ESP_LOGI("boot", "version: %s", firmware_version());
```

## Интеграция в другой проект

1. Скопировать всю папку `components/device_manager/`.
2. В `main/CMakeLists.txt`:
   ```cmake
   REQUIRES device_manager ...
   ```
3. Все компоненты, которым нужны `version.h` или `device_meta_*` — добавить
   `device_manager` в свой `REQUIRES` (тогда `include/` подхватится автоматически).

## Формат данных на флеше

Раздел `meta` в `partitions.csv`:
```csv
storage,  data,   littlefs,  0x340000,  0xBF000,
meta,     data,   0x83,      0x3FF000,  0x1000
```

Структура (104 байта):
| Offset | Size | Поле       | Описание                                |
|--------|------|------------|-----------------------------------------|
| 0      | 4    | `magic`    | `0xDEADBEEF` — маркер «уже записано»   |
| 4      | 64   | `project`  | ASCII, null-terminated, макс 63 символа |
| 68     | 32   | `serial`   | ASCII, null-terminated, макс 31 символ  |
| 100    | 4    | `crc32`    | CRC32 от первых 100 байт                |

Метка `meta` и тип `0x83` — это контракт. Если меняешь — править в трёх местах:
- `partitions.csv` (label/type)
- `DeviceManager.cpp` (`find_meta_partition()`)
- `provision.py` (`META_PARTITION`)

## Скрипты

### bump_version.py — автоинкремент при сборке

Подключён через `add_custom_target(bump_version ALL ...)` в `CMakeLists.txt`
компонента. Срабатывает на каждом `idf.py build` ДО компиляции.

Формат `include/version.h`:
```c
inline const char *firmware_version() { return "1.0.0.13"; }
```
Меняется только 4-е число. Старшие (major.minor.patch) — вручную.

Можно запускать и руками:
```bash
python components/device_manager/bump_version.py
```

### provision.py — запись project+serial (один раз при производстве)

```bash
python components/device_manager/provision.py "Led, SN-00001" --port COM3
```

Формат: `"<project>, <serial>"` через запятую. Пробелы по краям срезаются.
Проверка длин: project ≤ 63, serial ≤ 31.

Что делает:
1. Парсит "project, serial"
2. Собирает 104-байтный бинарь (magic + project + serial + crc32)
3. Пишет в раздел `meta` через `parttool.py write_partition`

Нужен `IDF_PATH` в окружении (через `export.bat`).

Серийник задаёт имена устройства: точка доступа `webled00001`, mDNS `webled00001.local`
(цифры из серийника: `SN-00001` → `00001`). Прошивать его можно после прошивки и первого запуска — на следующей
перезагрузке автоматические имена (по MAC / `webled`) один раз заменятся на имена с серийником.
Подробно — раздел «Имена устройства» в [docs/REST_API.md](../../docs/REST_API.md#имена-устройства).

## Проверка

После `provision.py`:
1. Открыть serial-монитор
2. Перезагрузить устройство
3. В логе: `device: Led, SN-00001`
4. В логе `wifimanager`: имя точки доступа `webled00001`, mDNS `webled00001`

Если `not provisioned` — meta пустая, прогони `provision.py` ещё раз.
Если `meta partition not found in table` — раздел не объявлен в `partitions.csv`.
