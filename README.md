# XorFilter

Author: Mikhail Khoroshavin aka "XopMC"

<div align="center">
  <p><strong>Build compact XOR filters from massive hex datasets</strong></p>
  <p>From raw text dumps to clean release-ready filter files, with flexible size profiles for real batch workloads.</p>
  <p>
    <a href="#english">English</a> |
    <a href="#русский">Русский</a>
  </p>
</div>

## English

### Overview

`XorFilter` is a command-line tool that builds binary XOR filter files from text inputs where each line contains a hex-encoded value.

It is built for people who work with big lists, long-running batch jobs, and data pipelines where you want the final result to stay compact, fast to query, and easy to regenerate.

Instead of forcing one rigid output profile, `XorFilter` lets you choose how aggressively you want to shrink the result. You can start with a straightforward default build, switch to tighter compression when file size matters, verify the result after generation, and package the same workflow for both Windows and Linux.

The current implementation focuses on:

- deterministic filter generation;
- lower peak memory usage during in-memory construction;
- several output density profiles;
- simple CLI flow for one file or many shards;
- practical release packaging for Windows and Linux.

### What It Can Do

- Uses a low-memory 4-wise XOR binary fuse filter implementation.
- Accepts one or more input files with one hex value per line.
- Supports standard, compressed, ultra-compressed, and hyper-compressed output modes.
- Can verify the finished filter after build with `-check`.
- Can split the source text stream into numbered chunk files with `-txt`.
- Fits both quick local runs and larger automated build pipelines.

### Accepted Input Formats

The builder accepts several practical line formats out of the box:

- plain 40-character hex lines;
- `0x`-prefixed hex lines;
- lines that start with `02`, `03`, or `04`, which are normalized automatically before the filter key is derived.

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
| `-mini` | Uses the smaller large-filter preset | About `2,147,483,644` entries per filter file; useful when you want to keep peak RAM lower |
| `-max` | Uses the large-filter preset | About `8,589,934,584` entries per filter file; can require very large amounts of RAM |
| `-max2` | Uses the largest preset | About `17,179,869,168` entries per filter file; intended only for machines with extreme RAM capacity |
| `-txt` | Saves numbered split text chunks while processing | Useful when the source stream is large |
| `-force` | Uses the conservative duplicate-removal path | Higher CPU cost, avoids the fast path |

### Capacity Presets and RAM Usage

`-mini`, `-max`, and `-max2` are not just "make the filter bigger" switches.

They define the maximum batch size that is accumulated before the current filter file is built and flushed. In practice, that means they affect two things at the same time:

- how large one output filter file can become;
- how much RAM the tool may need while it is collecting, sorting, deduplicating, and building that batch.

This is the practical way to think about them:

- `-mini` is the safer choice when you want to limit peak RAM. The tool will flush smaller batches, which usually means more numbered output files, but each batch is easier to build. Typical output size is about `9 GB` in the default mode and about `4.5 GB` in ultra mode.
- `-max` lets one filter file grow much larger. This can reduce the number of output files, but it may require more than `256 GB RAM` on very large runs. Typical output size is about `36 GB` in the default mode and about `18 GB` in ultra mode.
- `-max2` is the extreme preset. It targets the largest per-file batch size and may require more than `512 GB RAM` on very large runs. Typical output size is about `72 GB` in the default mode and about `36 GB` in ultra mode.

If your goal is to keep memory under control, start with `-mini`. If your goal is to produce fewer, larger filter files and your machine has enough RAM, move up to `-max` or `-max2`.

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

#### 9. Choose a preset based on memory budget

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -mini
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -max
```

`-mini` is the better starting point when RAM matters. `-max` is for machines that can afford much larger in-memory batches.

#### 10. Use the conservative duplicate-removal path

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -force
```

### Testing

The repository includes a smoke script and validation checks so you can quickly confirm that a build still behaves as expected.

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

### Why It Feels Practical

- Works well for large text-based datasets without turning the CLI flow into a maze.
- Gives you several size/accuracy trade-offs instead of a single one-size-fits-all mode.
- Lets you validate the finished result immediately with `-check`.
- Ships with release packaging for Windows and Linux so the same workflow can move from local experiments to distribution.

### Repository Layout

| Path | Purpose |
| --- | --- |
| `Source.cpp` | CLI, file ingestion, sorting, and filter generation flow |
| `xor_filter.h` | Low-memory 4-wise XOR binary fuse filter implementation |
| `hex_key_utils.h` | Shared helpers for input decoding and hashing |
| `tests/` | Smoke tests, fixtures, and validation checks |
| `.github/workflows/` | Automated CI and release workflows |

### License

MIT License. See `LICENSE.txt`.

### Author

Mikhail Khoroshavin aka "XopMC"

---

## Русский

### Обзор

`XorFilter` — это консольный инструмент для построения бинарных XOR-фильтров из текстовых входных файлов, где каждая строка содержит одно hex-значение.

Он рассчитан на людей, которые работают с большими списками, тяжёлыми batch-задачами и data pipeline’ами, где на выходе хочется получить компактный, быстрый и удобный для повторной сборки результат.

`XorFilter` не загоняет пользователя в один жёсткий сценарий. Можно собрать фильтр в базовом режиме, выбрать более плотный профиль, если важен размер файла, тут же проверить результат через `-check`, а потом использовать тот же workflow и на Windows, и на Linux.

Текущая реализация делает упор на:

- детерминированную сборку фильтров;
- уменьшенное пиковое потребление памяти во время in-memory построения;
- несколько профилей плотности и размера результата;
- простой CLI-проход для одного файла и для набора шардов;
- удобную подготовку релизов под Windows и Linux.

### Что умеет инструмент

- Использует low-memory реализацию 4-wise XOR binary fuse filter.
- Принимает один или несколько входных файлов с одной hex-строкой на запись.
- Поддерживает стандартный, compressed, ultra-compressed и hyper-compressed режимы.
- Может проверять готовый фильтр после построения через `-check`.
- Может сохранять исходный поток в разбитые текстовые чанки через `-txt`.
- Подходит и для локальных прогонов, и для более серьёзных автоматизированных пайплайнов.

### Поддерживаемые форматы входных данных

Инструмент умеет принимать несколько удобных форматов строк без лишней подготовки:

- обычные 40-символьные hex-строки;
- строки с префиксом `0x`;
- строки, начинающиеся с `02`, `03` или `04`, которые автоматически нормализуются перед извлечением ключа.

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
| `-mini` | Использует уменьшенный большой preset | Около `2 147 483 644` записей на один файл фильтра; полезно, если нужно держать RAM ниже |
| `-max` | Использует большой preset | Около `8 589 934 584` записей на один файл фильтра; может требовать очень большого объёма RAM |
| `-max2` | Использует самый большой preset | Около `17 179 869 168` записей на один файл фильтра; режим для машин с экстремальным объёмом RAM |
| `-txt` | Сохраняет разбитые текстовые чанки во время обработки | Полезно для очень больших входов |
| `-force` | Включает более консервативный путь удаления дублей | Дороже по CPU, но без fast path |

### Preset'ы вместимости и потребление ОЗУ

`-mini`, `-max` и `-max2` — это не просто переключатели вида "сделать фильтр побольше".

Они задают максимальный размер батча, который инструмент накапливает перед тем, как построить очередной файл фильтра и сбросить текущий набор. На практике эти флаги одновременно влияют на две вещи:

- насколько большим может стать один выходной файл фильтра;
- сколько ОЗУ может понадобиться инструменту во время накопления, сортировки, дедупликации и построения этого батча.

Проще всего понимать их так:

- `-mini` — более безопасный вариант, если нужно ограничить пиковое потребление ОЗУ. Батчи будут меньше, поэтому инструмент чаще будет создавать несколько нумерованных файлов, но каждый из них легче собрать. Типичный размер результата — около `9 GB` в обычном режиме и около `4.5 GB` в ultra-режиме.
- `-max` позволяет сделать один файл фильтра заметно крупнее. Это уменьшает количество выходных файлов, но на очень больших прогонах может потребовать более `256 GB RAM`. Типичный размер результата — около `36 GB` в обычном режиме и около `18 GB` в ultra-режиме.
- `-max2` — экстремальный preset для самого большого размера батча на один файл. На очень больших прогонах может потребовать более `512 GB RAM`. Типичный размер результата — около `72 GB` в обычном режиме и около `36 GB` в ultra-режиме.

Если главная цель — удержать потребление памяти в разумных пределах, лучше начинать с `-mini`. Если цель — получить меньше, но более крупных файлов фильтра, и у машины достаточно памяти, тогда уже имеет смысл переходить к `-max` или `-max2`.

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

#### 9. Выбрать preset под объём доступной памяти

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -mini
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -compress -max
```

Если RAM ограничена, лучше начинать с `-mini`. `-max` имеет смысл только на машинах, где действительно можно позволить себе очень большой in-memory батч.

#### 10. Использовать консервативный путь удаления дублей

```powershell
x64\Release\hex_to_xor.exe -i .\data\hashes.txt -force
```

### Тестирование

В репозитории есть smoke-скрипт и проверочный набор, чтобы быстро убедиться, что сборка ведёт себя ожидаемо.

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

### Почему этим удобно пользоваться

- Инструмент хорошо чувствует себя на больших текстовых наборах и не превращает CLI в перегруженный конструктор.
- Даёт несколько вариантов баланса между размером и точностью, а не один жёсткий режим.
- Позволяет сразу перепроверить результат через `-check`.
- Уже подготовлен к релизной упаковке под Windows и Linux, так что один и тот же workflow легко переносится из локальной работы в дистрибуцию.

### Структура репозитория

| Путь | Назначение |
| --- | --- |
| `Source.cpp` | CLI, чтение файлов, сортировка и основной flow построения |
| `xor_filter.h` | Реализация low-memory 4-wise XOR binary fuse filter |
| `hex_key_utils.h` | Общие helper’ы для декодирования входа и hashing |
| `tests/` | Smoke-тесты, фикстуры и проверочный набор |
| `.github/workflows/` | Автоматические CI и release workflows |

### Лицензия

MIT License. См. `LICENSE.txt`.

### Автор

Mikhail Khoroshavin aka "XopMC"
