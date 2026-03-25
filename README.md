# XorFilter

Author: Mikhail Khoroshavin aka "XopMC"

<div align="center">
  <p><strong>XOR filter builder for large hex datasets</strong></p>
  <p>Fast in-memory construction, legacy file compatibility, Windows and Linux release pipeline.</p>
  <p>
    <a href="#english">English</a> |
    <a href="#русский">Русский</a>
  </p>
</div>

## English

### Overview

`XorFilter` is a command-line tool that builds binary XOR filter files from text inputs where each line contains a hex-encoded value.

The project is designed for high-volume input processing and keeps compatibility with previously generated `.xor_*` files. The current implementation focuses on:

- deterministic filter generation;
- compatibility with existing filter layouts;
- lower peak memory usage than the earlier implementation;
- support for several filter density modes;
- practical release packaging for Windows and Linux.

### Highlights

- Uses a low-memory 4-wise XOR binary fuse filter implementation.
- Accepts one or more input files with one hex value per line.
- Supports standard, compressed, ultra-compressed, and hyper-compressed output modes.
- Can verify the finished filter after build with `-check`.
- Can split the source text stream into numbered chunk files with `-txt`.
- Keeps the persisted filter format compatible with older `.xor_u`, `.xor_c`, `.xor_uc`, and `.xor_hc` files.

### Supported Input Forms

The builder accepts the legacy-compatible input forms used by the project:

- plain 40-character hex lines;
- `0x`-prefixed hex lines;
- legacy-compatible lines that start with `02`, `03`, or `04`, where the builder keeps the historical normalization behavior before extracting the first 20 bytes.

Example input:

```text
00112233445566778899aabbccddeeff00112233
0x89abcdef0123456789abcdef0123456789abcdef
02fedcba9876543210fedcba9876543210fedcba98
```

### Output Modes

| Mode | Flag | Extension | Notes |
| --- | --- | --- | --- |
| Uncompressed | default | `.xor_u` | Stores the default 32-bit fingerprint format |
| Compressed | `-compress` | `.xor_c` | Lower output size, very low false-positive rate |
| Ultra compressed | `-ultra` | `.xor_uc` | Smaller output, higher false-positive rate than `-compress` |
| Hyper compressed | `-hyper` | `.xor_hc` | Smallest output, highest false-positive rate among supported modes |

Approximate false-positive rates used in the CLI help:

- `-compress`: `0.0000001%`
- `-ultra`: `0.001444%`
- `-hyper`: `0.3556%`

### Build

#### Windows

Recommended environment:

- Visual Studio 2022
- MSBuild
- Clang/LLVM toolset configured in the solution

Build command:

```powershell
msbuild hex_to_xor.sln /p:Configuration=Release /p:Platform=x64
```

Output:

```text
x64\Release\hex_to_xor.exe
```

#### Linux

Expected toolchain:

- `clang++`
- `make`
- OpenMP runtime
- Intel TBB development package

Example dependency install on Ubuntu:

```bash
sudo apt update
sudo apt install -y clang make libomp-dev libtbb-dev
```

Build command:

```bash
make
```

Output:

```text
./build/hex_to_xor
```

### Command-Line Arguments

| Argument | Description | Details |
| --- | --- | --- |
| `-i <file>` | Adds an input file | Can be specified multiple times |
| `-o <folder>` | Writes output files to a custom directory | If omitted, output is written next to the current working directory |
| `-check` | Verifies the populated filter | Re-checks that inserted keys are found |
| `-compress` | Builds compressed filters | Produces `.xor_c` files |
| `-ultra` | Builds ultra-compressed filters | Produces `.xor_uc` files |
| `-hyper` | Builds hyper-compressed filters | Produces `.xor_hc` files |
| `-mini` | Uses the smaller large-filter preset | About `2,147,483,644` entries |
| `-max` | Uses the large-filter preset | About `8,589,934,584` entries |
| `-max2` | Uses the largest preset | About `17,179,869,168` entries |
| `-txt` | Saves numbered split text chunks while processing | Useful when the source stream is large |
| `-force` | Uses the conservative duplicate-removal path | Higher CPU cost, avoids the fast path |

### Naming Convention for Output Files

The tool derives the file name from the first input file and appends a sequence number plus the mode extension.

Examples:

- `keys.txt` -> `keys_0.xor_u`
- `keys.txt` + `-compress` -> `keys_0.xor_c`
- `keys.txt` + `-ultra` -> `keys_0.xor_uc`
- `keys.txt` + `-hyper` -> `keys_0.xor_hc`

If `-txt` is enabled, split text files are created with names similar to:

- `keys_split_0.txt`
- `keys_split_1.txt`

### Usage Examples

#### 1. Build a default filter from one file

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt
```

Linux:

```bash
./build/hex_to_xor -i ./data/hashes.txt
```

#### 2. Build a compressed filter

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress
```

#### 3. Build an ultra-compressed filter

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -ultra
```

#### 4. Build a hyper-compressed filter

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -hyper
```

#### 5. Verify the filter after build

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -check
```

#### 6. Write output to a separate folder

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -o .\out
```

Linux:

```bash
./build/hex_to_xor -i ./data/hashes.txt -compress -o ./out
```

#### 7. Use multiple input files

```powershell
x64\Release\hex_to_xor.exe -i .\data\part1.txt -i .\data\part2.txt -i .\data\part3.txt
```

#### 8. Save split text chunks while building

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -txt -o .\out
```

#### 9. Use a larger preset

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -max
```

#### 10. Use the conservative duplicate-removal path

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -force
```

### Testing

The repository includes a smoke script and compatibility checks.

Run the smoke script after building:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_smoke.ps1
```

What it does:

- builds filters in all supported modes from a small fixture;
- compares generated files against checked-in golden files;
- compiles and runs `tests/filter_compat_tests.cpp` when `clang-cl` is available.

### Release Artifacts

The repository is prepared for two primary release packages:

| Artifact | Platform | Typical contents |
| --- | --- | --- |
| `XorFilter-windows-x64.zip` | Windows | `hex_to_xor.exe`, `README`, `LICENSE` |
| `XorFilter-linux-x64.tar.gz` | Linux | `hex_to_xor`, `README`, `LICENSE` |

### Compatibility Notes

- The on-disk filter format stays compatible with older `.xor_*` files.
- The tool preserves deterministic seed generation behavior.
- The old external `uint128` dependency is removed from the active build path.
- Input decoding keeps the historical legacy-compatible normalization logic.

### Repository Layout

| Path | Purpose |
| --- | --- |
| `Source.cpp` | CLI, file ingestion, sorting, and filter generation flow |
| `xor_filter.h` | Low-memory 4-wise XOR binary fuse filter implementation |
| `hex_key_utils.h` | Shared compatibility helpers for decoding and hashing |
| `tests/` | Smoke tests, fixtures, and compatibility checks |
| `.github/workflows/` | Automated CI and release workflows |

### License

MIT License. See `LICENSE.txt`.

### Author

Mikhail Khoroshavin aka "XopMC"

---

## Русский

### Обзор

`XorFilter` — это консольный инструмент для построения бинарных XOR-фильтров из текстовых входных файлов, где каждая строка содержит одно hex-значение.

Проект рассчитан на большие наборы данных и сохраняет совместимость со старыми файлами `.xor_*`, уже созданными предыдущими версиями. Текущая реализация делает упор на:

- детерминированную сборку фильтров;
- совместимость с существующим форматом файлов;
- уменьшенное пиковое потребление памяти по сравнению со старой реализацией;
- несколько режимов плотности фильтра;
- практичную подготовку релизов для Windows и Linux.

### Ключевые особенности

- Использует low-memory реализацию 4-wise XOR binary fuse filter.
- Принимает один или несколько входных файлов с одной hex-строкой на запись.
- Поддерживает стандартный, compressed, ultra-compressed и hyper-compressed режимы.
- Может проверять готовый фильтр после построения через `-check`.
- Может сохранять исходный поток в разбитые текстовые чанки через `-txt`.
- Сохраняет совместимость формата с уже существующими `.xor_u`, `.xor_c`, `.xor_uc` и `.xor_hc` файлами.

### Поддерживаемые форматы входных данных

Инструмент принимает совместимые со старой логикой варианты строк:

- обычные 40-символьные hex-строки;
- строки с префиксом `0x`;
- legacy-совместимые строки, начинающиеся с `02`, `03` или `04`, где дальше применяется историческая логика нормализации перед извлечением первых 20 байт.

Пример входного файла:

```text
00112233445566778899aabbccddeeff00112233
0x89abcdef0123456789abcdef0123456789abcdef
02fedcba9876543210fedcba9876543210fedcba98
```

### Режимы выходных файлов

| Режим | Флаг | Расширение | Описание |
| --- | --- | --- | --- |
| Обычный | по умолчанию | `.xor_u` | Базовый формат с 32-битным fingerprint |
| Сжатый | `-compress` | `.xor_c` | Меньше размер файла, очень низкий false-positive rate |
| Ультра-сжатый | `-ultra` | `.xor_uc` | Ещё меньше размер, но выше вероятность false-positive |
| Гипер-сжатый | `-hyper` | `.xor_hc` | Самый компактный режим, но с самой высокой вероятностью false-positive |

Ориентировочные значения false-positive rate из CLI:

- `-compress`: `0.0000001%`
- `-ultra`: `0.001444%`
- `-hyper`: `0.3556%`

### Сборка

#### Windows

Рекомендуемое окружение:

- Visual Studio 2022
- MSBuild
- Clang/LLVM toolset, подключённый в solution

Команда сборки:

```powershell
msbuild hex_to_xor.sln /p:Configuration=Release /p:Platform=x64
```

Результат:

```text
x64\Release\hex_to_xor.exe
```

#### Linux

Ожидаемый toolchain:

- `clang++`
- `make`
- OpenMP runtime
- development-пакет Intel TBB

Пример установки зависимостей на Ubuntu:

```bash
sudo apt update
sudo apt install -y clang make libomp-dev libtbb-dev
```

Команда сборки:

```bash
make
```

Результат:

```text
./build/hex_to_xor
```

### Аргументы командной строки

| Аргумент | Назначение | Подробности |
| --- | --- | --- |
| `-i <file>` | Добавляет входной файл | Можно указывать несколько раз |
| `-o <folder>` | Сохраняет результат в выбранную папку | Если не указан, используется текущая рабочая директория |
| `-check` | Проверяет фильтр после построения | Повторно проверяет, что все добавленные ключи находятся |
| `-compress` | Строит сжатый фильтр | Создаёт `.xor_c` |
| `-ultra` | Строит ultra-compressed фильтр | Создаёт `.xor_uc` |
| `-hyper` | Строит hyper-compressed фильтр | Создаёт `.xor_hc` |
| `-mini` | Использует уменьшенный большой preset | Около `2 147 483 644` записей |
| `-max` | Использует большой preset | Около `8 589 934 584` записей |
| `-max2` | Использует самый большой preset | Около `17 179 869 168` записей |
| `-txt` | Сохраняет разбитые текстовые чанки во время обработки | Полезно для очень больших входов |
| `-force` | Включает более консервативный путь удаления дублей | Дороже по CPU, но без fast path |

### Как формируются имена выходных файлов

Имя берётся из первого входного файла, после чего добавляются номер и расширение режима.

Примеры:

- `keys.txt` -> `keys_0.xor_u`
- `keys.txt` + `-compress` -> `keys_0.xor_c`
- `keys.txt` + `-ultra` -> `keys_0.xor_uc`
- `keys.txt` + `-hyper` -> `keys_0.xor_hc`

Если включён `-txt`, параллельно создаются файлы вида:

- `keys_split_0.txt`
- `keys_split_1.txt`

### Примеры запуска

#### 1. Построить обычный фильтр из одного файла

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt
```

Linux:

```bash
./build/hex_to_xor -i ./data/hashes.txt
```

#### 2. Построить compressed фильтр

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress
```

#### 3. Построить ultra-compressed фильтр

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -ultra
```

#### 4. Построить hyper-compressed фильтр

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -hyper
```

#### 5. Проверить фильтр после сборки

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -check
```

#### 6. Сохранить результат в отдельную папку

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -o .\out
```

Linux:

```bash
./build/hex_to_xor -i ./data/hashes.txt -compress -o ./out
```

#### 7. Использовать несколько входных файлов

```powershell
x64\Release\hex_to_xor.exe -i .\data\part1.txt -i .\data\part2.txt -i .\data\part3.txt
```

#### 8. Сохранять разбитые текстовые чанки во время построения

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -txt -o .\out
```

#### 9. Использовать более крупный preset

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -max
```

#### 10. Использовать консервативный путь удаления дублей

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -force
```

### Тестирование

В репозитории есть smoke-скрипт и проверки совместимости.

После сборки можно запустить:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_smoke.ps1
```

Что делает скрипт:

- строит фильтры во всех поддерживаемых режимах на маленькой фикстуре;
- сравнивает результаты с golden-файлами в репозитории;
- компилирует и запускает `tests/filter_compat_tests.cpp`, если доступен `clang-cl`.

### Релизные артефакты

Репозиторий подготовлен под два основных релизных пакета:

| Артефакт | Платформа | Содержимое |
| --- | --- | --- |
| `XorFilter-windows-x64.zip` | Windows | `hex_to_xor.exe`, `README`, `LICENSE` |
| `XorFilter-linux-x64.tar.gz` | Linux | `hex_to_xor`, `README`, `LICENSE` |

### Заметки по совместимости

- Формат файлов на диске остаётся совместимым со старыми `.xor_*`.
- Детерминированная генерация seed сохранена.
- Внешняя зависимость `uint128` убрана из активного build path.
- Логика декодирования входа сохраняет историческое legacy-совместимое поведение.

### Структура репозитория

| Путь | Назначение |
| --- | --- |
| `Source.cpp` | CLI, чтение файлов, сортировка и основной flow построения |
| `xor_filter.h` | Реализация low-memory 4-wise XOR binary fuse filter |
| `hex_key_utils.h` | Общие compatibility-helper’ы для декодирования и hashing |
| `tests/` | Smoke-тесты, фикстуры и compatibility checks |
| `.github/workflows/` | Автоматические CI и release workflows |

### Лицензия

MIT License. См. `LICENSE.txt`.

### Автор

Mikhail Khoroshavin aka "XopMC"
