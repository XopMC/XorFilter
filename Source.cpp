/*
 * hex_to_xor
 * Author: Mikhail Khoroshavin aka "XopMC"
 * Command-line tool for building XOR filters from hex-encoded input lists.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <execution>
#include <memory>
#include <random>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <omp.h>
#include <vector>
#include <sys/stat.h>
#include "hex_key_utils.h"
#include "xor_filter.h"
#include <iomanip>
#include <thread>
#include <mutex>


bool force = false;

static uint64_t XOR_MAX = XOR_MAX_SIZE;

void saveFilter(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);
void saveFilter_ultra(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);
void saveFilter_hyper(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);
namespace {

constexpr size_t kLocalKeyFlush = 1u << 15;
constexpr uint64_t kReserveCapKeys = 1ULL << 25;

// Prints the supported CLI flags and the preset sizes used by the builder.
void print_usage() {
    std::cerr << "[!] Usage: \n";
    std::cerr << "[!] -i \tfile.txt (or -i file1.txt -i file2.txt)\n";
    std::cerr << "[!] -check \tVerify the filter after it is populated\n";
    std::cerr << "[!] -compress \tBuild compressed XOR filters (.xor_c) - 0.0000001% false-positive rate\n";
    std::cerr << "[!] -ultra \tBuild ultra-compressed XOR filters (.xor_uc) - 0.001444% false-positive rate\n";
    std::cerr << "[!] -hyper \tBuild hyper-compressed XOR filters (.xor_hc) - 0.3556% false-positive rate\n";
    std::cerr << "[!] -mini \tSmaller per-filter batch: 2,147,483,644 entries (about 9 GB, or 4.5 GB for ultra)\n";
    std::cerr << "[!] -max \tLarge per-filter batch: 8,589,934,584 entries (about 36 GB, or 18 GB for ultra; can use >256 GB RAM)\n";
    std::cerr << "[!] -max2 \tLargest per-filter batch: 17,179,869,168 entries (about 72 GB, or 36 GB for ultra; can use >512 GB RAM)\n";
    std::cerr << "[!]        These presets also cap the in-memory batch size: smaller presets reduce peak RAM but may create more numbered filter files.\n";
    std::cerr << "[!] -txt \tSplit the source text into chunk files while building filters\n";
    std::cerr << "[!] -force \tUse the slower but conservative duplicate-removal path\n";
    std::cerr << "[!] -o FOLDER\tWrite output files into the selected folder\n";
}

// Returns the leaf file name without touching the original path separators.
std::string filename_only(const std::string& path) {
    const size_t split = path.find_last_of("/\\");
    return split == std::string::npos ? path : path.substr(split + 1);
}

// Drops the final file extension so we can derive output names from input files.
std::string remove_extension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// Joins a directory and a file name without depending on std::filesystem.
std::string join_path(std::string dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }

    const char last = dir.back();
    if (last != '\\' && last != '/') {
        dir.push_back('\\');
    }
    return dir + name;
}

// Generates the deterministic file name used by the optional text split mode.
std::string make_split_name(const std::string& baseName, size_t splitCount) {
    return remove_extension(filename_only(baseName)) + "_split_" + std::to_string(splitCount) + ".txt";
}

// Uses source file sizes to pre-reserve enough space for the first in-memory
// batch without overcommitting on huge inputs.
size_t estimate_initial_key_capacity(const std::vector<std::string>& files, bool compressedMode) {
    uint64_t totalBytes = 0;
    for (const std::string& file : files) {
        struct stat statBuffer {};
        if (stat(file.c_str(), &statBuffer) == 0 && statBuffer.st_size > 0) {
            totalBytes += static_cast<uint64_t>(statBuffer.st_size);
        }
    }

    if (totalBytes == 0) {
        return 0;
    }

    const uint64_t estimatedLines = totalBytes / 41 + files.size();
    const uint64_t estimatedKeys = estimatedLines * (compressedMode ? 1ULL : 2ULL);
    return static_cast<size_t>(std::min<uint64_t>(std::min<uint64_t>(estimatedKeys, XOR_MAX), kReserveCapKeys));
}

}  // namespace

// Prints a local timestamp for long-running batch jobs and smoke logs.
void print_current_time() {
    time_t rawtime;
    struct tm timeinfo;
    char buffer[80];

    time(&rawtime);
#if _WIN32
    localtime_s(&timeinfo, &rawtime);
#else
    localtime_r(&rawtime, &timeinfo);

#endif
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

    std::cerr << "Current local time: " << buffer << '\n';
}


// Parallel sorting helpers used by duplicate removal before filter construction.

static inline int floor_log2_size(size_t x) {
    if (x <= 1) return 0;
#if defined(_MSC_VER)
#if defined(_M_X64) || defined(_M_AMD64)
    unsigned long idx;
    _BitScanReverse64(&idx, (unsigned long long)x);
    return (int)idx;
#else
    unsigned long idx;
    _BitScanReverse(&idx, (unsigned long)x);
    return (int)idx;
#endif
#else
#if defined(__x86_64__) || defined(__aarch64__)
    return 63 - __builtin_clzll((unsigned long long)x);
#else
    int r = 0;
    while (x >>= 1) ++r;
    return r;
#endif
#endif
}

static inline uint64_t median3_u64(uint64_t a, uint64_t b, uint64_t c) {
    if (a < b) {
        if (b < c) return b;
        return (a < c) ? c : a;
    }
    else {
        if (a < c) return a;
        return (b < c) ? c : b;
    }
}

static inline uint64_t choose_pivot_u64(const uint64_t* lo, const uint64_t* hi) {
    const size_t n = (size_t)(hi - lo);
    if (n >= 128) {
        const size_t step = n / 8;
        const uint64_t m1 = median3_u64(lo[0], lo[step], lo[step * 2]);
        const uint64_t m2 = median3_u64(lo[n / 2 - step], lo[n / 2], lo[n / 2 + step]);
        const uint64_t m3 = median3_u64(hi[-1 - step * 2], hi[-1 - step], hi[-1]);
        return median3_u64(m1, m2, m3);
    }
    return median3_u64(lo[0], lo[n / 2], hi[-1]);
}

static inline void insertion_sort_u64(uint64_t* lo, uint64_t* hi) {
    for (uint64_t* i = lo + 1; i < hi; ++i) {
        uint64_t v = *i;
        uint64_t* j = i;
        while (j > lo && v < j[-1]) { *j = j[-1]; --j; }
        *j = v;
    }
}

static inline bool partial_insertion_sort_u64(uint64_t* lo, uint64_t* hi) {
    int limit = 16;
    for (uint64_t* i = lo + 1; i < hi; ++i) {
        if (i[0] < i[-1]) {
            uint64_t v = *i;
            uint64_t* j = i;
            do { *j = j[-1]; --j; } while (j > lo && v < j[-1]);
            *j = v;
            if (--limit == 0) return false;
        }
    }
    return true;
}

static inline void partition3_u64(uint64_t* lo, uint64_t* hi, uint64_t pivot,
    uint64_t*& lt, uint64_t*& gt, bool& swapped) {
    lt = lo;
    gt = hi;
    swapped = false;
    for (uint64_t* i = lo; i < gt; ) {
        uint64_t v = *i;
        if (v < pivot) {
            if (lt != i) { std::swap(*lt, *i); swapped = true; }
            ++lt; ++i;
        }
        else if (v > pivot) {
            --gt;
            if (gt != i) { std::swap(*i, *gt); swapped = true; }
        }
        else {
            ++i;
        }
    }
}

static inline void heap_sort_u64(uint64_t* lo, uint64_t* hi) {
    std::make_heap(lo, hi);
    std::sort_heap(lo, hi);
}


struct SortTaskU64 {
    uint64_t* lo;
    uint64_t* hi;
    int depth;
};

class TaskPool {
public:
    explicit TaskPool(unsigned threads) : stop(false), pending(0) {
        if (threads == 0) threads = 8;
        workers.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~TaskPool() {
        {
            std::lock_guard<std::mutex> lk(m);
            stop = true;
        }
        cv.notify_all();
        for (auto& t : workers) t.join();
    }

    void push(SortTaskU64 t) {
        pending.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(m);
            stack.push_back(t);
        }
        cv.notify_one();
    }

    void wait_all() {
        std::unique_lock<std::mutex> lk(done_m);
        done_cv.wait(lk, [&] { return pending.load(std::memory_order_acquire) == 0; });
    }

    bool try_pop(SortTaskU64& out) {
        std::lock_guard<std::mutex> lk(m);
        if (stack.empty()) return false;
        out = stack.back();
        stack.pop_back();
        return true;
    }

    void finish_one() {
        if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(done_m);
            done_cv.notify_all();
        }
    }

private:
    void worker_loop() {
        for (;;) {
            SortTaskU64 task;
            if (!try_pop(task)) {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return stop || !stack.empty(); });
                if (stop && stack.empty()) return;
                continue;
            }
            extern void pdqsort_parallel_worker(TaskPool*, uint64_t*, uint64_t*, int);
            pdqsort_parallel_worker(this, task.lo, task.hi, task.depth);
            finish_one();
        }
    }

    std::mutex m;
    std::condition_variable cv;
    std::vector<SortTaskU64> stack;

    std::vector<std::thread> workers;
    bool stop;

    std::atomic<int64_t> pending;

    std::mutex done_m;
    std::condition_variable done_cv;
};


static inline void pdqsort_parallel_impl(TaskPool* pool,
    uint64_t* lo, uint64_t* hi, int depth_limit,
    size_t par_cutoff)
{
    for (;;) {
        const size_t n = (size_t)(hi - lo);
        if (n < 2) return;
        if (n <= 32) { insertion_sort_u64(lo, hi); return; }

        if (n <= (1u << 18)) {
            if (partial_insertion_sort_u64(lo, hi)) return;
        }

        if (depth_limit-- <= 0) { heap_sort_u64(lo, hi); return; }

        const uint64_t pivot = choose_pivot_u64(lo, hi);

        uint64_t* lt;
        uint64_t* gt;
        bool swapped;
        partition3_u64(lo, hi, pivot, lt, gt, swapped);

        const size_t leftN = (size_t)(lt - lo);
        const size_t rightN = (size_t)(hi - gt);

        if (leftN == 0 && rightN == 0) return;

        const size_t small_s = n / 16;
        if (!swapped && (leftN <= small_s || rightN <= small_s)) {
            heap_sort_u64(lo, hi);
            return;
        }

        auto spawn_side = [&](uint64_t* a, uint64_t* b, int depth) {
            if (pool && (size_t)(b - a) >= par_cutoff) {
                pool->push({ a, b, depth });
                return true;
            }
            return false;
        };

        if (leftN < rightN) {
            if (!spawn_side(lo, lt, depth_limit)) {
                pdqsort_parallel_impl(nullptr, lo, lt, depth_limit, par_cutoff);
            }
            lo = gt;
        }
        else {
            if (!spawn_side(gt, hi, depth_limit)) {
                pdqsort_parallel_impl(nullptr, gt, hi, depth_limit, par_cutoff);
            }
            hi = lt;
        }
    }
}

void pdqsort_parallel_worker(TaskPool* pool, uint64_t* lo, uint64_t* hi, int depth_limit) {
    const size_t n = (size_t)(hi - lo);
    const size_t par_cutoff = (n >= (1u << 24)) ? (1u << 20) : (1u << 18); 
    pdqsort_parallel_impl(pool, lo, hi, depth_limit, par_cutoff);
}


static inline size_t dedup_inplace_sorted_u64(uint64_t* a, size_t n) {
    if (n == 0) return 0;
    size_t w = 1;
    for (size_t i = 1; i < n; ++i) {
        uint64_t x = a[i];
        if (x != a[w - 1]) a[w++] = x;
    }
    return w;
}


void sort_unstable_parallel_u64(uint64_t* a, size_t n, unsigned threads = 0) {
    if (n < 2) return;
    int depth = 2 * floor_log2_size(n);

    unsigned T = threads ? threads : std::thread::hardware_concurrency();
    if (T <= 1) {
        pdqsort_parallel_impl(nullptr, a, a + n, depth, (size_t)-1);
        return;
    }

    TaskPool pool(T);

    pool.push({ a, a + n, depth });

    pool.wait_all();
}

// Fast path used by default: parallel sort + in-place deduplication.
void removeDuplicates_fast(std::vector<uint64_t>& keys, unsigned threads = 0) {
    std::cerr << "[!] Starting remove duplicate\n";
    sort_unstable_parallel_u64(keys.data(), keys.size(), threads);
    keys.resize(dedup_inplace_sorted_u64(keys.data(), keys.size()));
}

// Conservative fallback used with -force when we want the STL parallel path.
void removeDuplicates(std::vector<uint64_t>& Keys) {
    std::cerr << "[!] Starting remove duplicate\n";
    std::sort(std::execution::par_unseq, Keys.begin(), Keys.end());

    auto last = std::unique(Keys.begin(), Keys.end());

    Keys.erase(last, Keys.end());
}

bool check = false;
bool compress = false;
bool ultra_compress = false;
bool hyper_compress = false;
bool split = false;

std::string outpath = "";

// Entry point: parse CLI flags, collect keys, and build one or more filter files.
int main(int argc, char** argv) {
    size_t filterCount = 0;
    std::vector<std::string> files;
    std::string filter_type = "UNCOMPRESSED";
    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "-i") == 0) {
            if (arg + 1 >= argc) {
                std::cerr << "[!] Missing filename after '-i'.\n";
                return 1;
            }
            files.push_back(argv[arg + 1]);
            arg++;
        }
        else if (strcmp(argv[arg], "-o") == 0) {
            if (arg + 1 >= argc) {
                std::cerr << "[!] Missing filename after '-o'.\n";
                return 1;
            }
            outpath = argv[arg + 1];
            arg++;
        }
        else if (strcmp(argv[arg], "-check") == 0) {
            check = true;
        }
        else if (strcmp(argv[arg], "-mini") == 0) {
            XOR_MAX = XOR_MIN_SIZE;
        }
        else if (strcmp(argv[arg], "-force") == 0) {
            force = true;
        }
        else if (strcmp(argv[arg], "-max") == 0) {
            XOR_MAX = (XOR_MAX_SIZE) * 2ULL;
        }
        else if (strcmp(argv[arg], "-max2") == 0) {
            XOR_MAX = (XOR_MAX_SIZE) * 4ULL;
        }
        else if (strcmp(argv[arg], "-txt") == 0) {
            split = true;
        }
        else if (strcmp(argv[arg], "-compress") == 0) {
            compress = true;
            hyper_compress = false;
            ultra_compress = false;
            filter_type = "COMPRESSED";
        }
        else if (strcmp(argv[arg], "-ultra") == 0) {
            ultra_compress = true;
            hyper_compress = false;
            compress = false;
            filter_type = "ULTRA COMPRESSED";
        }
        else if (strcmp(argv[arg], "-hyper") == 0) {
            ultra_compress = false;
            hyper_compress = true;
            compress = false;
            filter_type = "HYPER COMPRESSED";
        }
        else {
            std::cerr << "[!] Incorrect usage. Unexpected argument: " << argv[arg] << '\n';
            print_usage();
            return 1;
        }
    }

    if (files.empty()) {
        std::cerr << "[!] Missing input files.\n";
        print_usage();
        return 1;
    }

    const bool compressedMode = compress || ultra_compress || hyper_compress;
    std::vector<uint64_t> Keys64;
    if (const size_t initialCapacity = estimate_initial_key_capacity(files, compressedMode)) {
        Keys64.reserve(initialCapacity);
    }

    size_t split_count = 0;
    std::ofstream splitOutput;
    // Opens or rotates the optional text split file used to mirror the current batch.
    auto open_split_output = [&]() -> bool {
        if (!split) {
            return true;
        }

        if (splitOutput.is_open()) {
            splitOutput.close();
        }

        splitOutput.open(join_path(outpath, make_split_name(files[0], split_count)), std::ios::binary | std::ios::trunc);
        if (!splitOutput.is_open()) {
            std::cerr << "[!] Failed to open split output file.\n";
            return false;
        }
        return true;
    };

    // Finalizes the current in-memory batch when it reaches the configured size.
    auto build_current_filter = [&](uint64_t lineCount, const std::string& currentFile) {
        std::cerr << "[+] Processing " << currentFile << "  line: " << lineCount << "\r";
        if (!force) {
            removeDuplicates_fast(Keys64);
        }
        else {
            removeDuplicates(Keys64);
        }
        if (Keys64.size() < XOR_MAX) {
            std::cerr << "[!] Deleted " << (XOR_MAX - Keys64.size()) << " duplicate hashes, adding new lines\n";
            return;
        }

        if (ultra_compress) {
            saveFilter_ultra(Keys64, files[0], filterCount);
        }
        else if (hyper_compress) {
            saveFilter_hyper(Keys64, files[0], filterCount);
        }
        else {
            saveFilter(Keys64, files[0], filterCount);
        }

        ++filterCount;
        Keys64.clear();

        if (split) {
            ++split_count;
            if (!open_split_output()) {
                std::exit(EXIT_FAILURE);
            }
        }
    };

    if (!open_split_output()) {
        return 1;
    }

    std::cerr << "[!] Making " << filter_type << " XOR filter[s] with max size: " << XOR_MAX << " [!]\n";
    print_current_time();
    std::cerr << "[+] Starting files reading\n";

    std::atomic<int64_t> next_file{ 0 };

#pragma omp parallel
    {
        char linebuf[256];
        std::vector<uint64_t> localKeys;
        if (!split) {
            localKeys.reserve(kLocalKeyFlush * 2);
        }

        // Merges thread-local keys into the shared batch to keep the hot path
        // mostly lock-free while still preserving the existing build flow.
        auto flush_local = [&](const std::string& currentFile, uint64_t lineCount) {
            if (localKeys.empty()) {
                return;
            }

#pragma omp critical(keys_merge)
            {
                if (Keys64.capacity() < Keys64.size() + localKeys.size()) {
                    const size_t required = Keys64.size() + localKeys.size();
                    const size_t grown = Keys64.capacity() == 0 ? required : Keys64.capacity() * 2;
                    Keys64.reserve(std::max(required, grown));
                }
                Keys64.insert(Keys64.end(), localKeys.begin(), localKeys.end());
                localKeys.clear();
                if (Keys64.size() >= XOR_MAX) {
                    build_current_filter(lineCount, currentFile);
                }
            }
        };

        while (true) {
            const int64_t currentFileIndex = next_file.fetch_add(1, std::memory_order_relaxed);
            if (currentFileIndex >= static_cast<int64_t>(files.size())) {
                break;
            }

            const std::string& currentFile = files[currentFileIndex];
            std::unique_ptr<FILE, int(*)(FILE*)> input(std::fopen(currentFile.c_str(), "rb"), &std::fclose);
            if (!input) {
#pragma omp critical(log_io)
                std::cerr << "[!] Failed to open hash160 file '" << currentFile << "'\n";
                continue;
            }

            static const size_t kInputBufferSize = 1u << 20;
            std::vector<char> inputBuffer(kInputBufferSize);
            setvbuf(input.get(), inputBuffer.data(), _IOFBF, kInputBufferSize);

            uint64_t lineCount = 0;
            uint64_t lineBatchCount = 0;
            while (std::fgets(linebuf, static_cast<int>(sizeof(linebuf)), input.get())) {
                const char* line = linebuf;
                size_t length = std::strlen(linebuf);
                if (length && linebuf[length - 1] == '\n') {
                    --length;
                }
                if (length && linebuf[length - 1] == '\r') {
                    --length;
                }

                if (split) {
                    const char* normalizedLine = line;
                    size_t normalizedLength = length;
                    if (!hex_to_xor::normalize_hex_input(normalizedLine, normalizedLength)) {
                        ++lineCount;
                        ++lineBatchCount;
                        continue;
                    }

                    uint64_t decodedKeys[2];
                    const size_t decodedCount = hex_to_xor::decode_keys_from_hex_line(line, length, compressedMode, decodedKeys);
#pragma omp critical(keys_merge)
                    {
                        splitOutput.write(normalizedLine, static_cast<std::streamsize>(normalizedLength));
                        splitOutput.put('\n');
                        if (decodedCount != 0) {
                            Keys64.insert(Keys64.end(), decodedKeys, decodedKeys + decodedCount);
                        }
                        if (decodedCount != 0 && Keys64.size() >= XOR_MAX) {
                            build_current_filter(lineCount, currentFile);
                        }
                    }
                }
                else
                {
                    uint64_t decodedKeys[2];
                    const size_t decodedCount = hex_to_xor::decode_keys_from_hex_line(line, length, compressedMode, decodedKeys);
                    if (decodedCount != 0) {
                        localKeys.insert(localKeys.end(), decodedKeys, decodedKeys + decodedCount);
                        if (localKeys.size() >= kLocalKeyFlush) {
                            flush_local(currentFile, lineCount);
                        }
                    }
                }

                ++lineCount;
                ++lineBatchCount;
                if (lineBatchCount == 1000000) {
#pragma omp critical(log_io)
                    std::cerr << "[+] Processing " << currentFile << "  line: " << lineCount << "\r";
                    lineBatchCount = 0;
                }
            }

            if (!split) {
                flush_local(currentFile, lineCount);
            }

#pragma omp critical(log_io)
            std::cerr << "\n[+] Processed " << currentFile << "  lines: " << lineCount << "\n";
        }
    }

    if (!Keys64.empty()) {
        if (!force) {
            removeDuplicates_fast(Keys64);
        }
        else {
            removeDuplicates(Keys64);
        }
        if (ultra_compress) {
            saveFilter_ultra(Keys64, files[0], filterCount);
        }
        else if (hyper_compress) {
            saveFilter_hyper(Keys64, files[0], filterCount);
        }
        else {
            saveFilter(Keys64, files[0], filterCount);
        }
    }

    if (splitOutput.is_open()) {
        splitOutput.close();
    }

    std::cerr << "\n[+] Processing all files: done\n";
    print_current_time();
    return 0;
}

template <typename FingerprintType>
// Builds, optionally verifies, and saves a single filter file for one batch of keys.
void saveFilterImpl(std::vector<uint64_t>& keys,
    const std::string& baseName,
    size_t filterCount,
    const char* allocationMessage,
    const std::string& extension) {
    std::cerr << "\n[+] Start " << allocationMessage << " XOR filter allocating\n";

    xorbinaryfusefilter_lowmem4wise::XorBinaryFuseFilter<uint64_t, FingerprintType> filter(keys.size());
    std::cerr << "[+] Start keys shuffle\n";
    std::mt19937_64 rng(0x726b2b9d438b9d4d);
    std::shuffle(keys.begin(), keys.end(), rng);
    std::cerr << "[+] Start XOR filter populating (size: " << keys.size() << " )\n";

    try {
        const int result = filter.AddAll(keys.data(), 0, keys.size());
        if (result != 0) {
            std::cerr << "Error populating filter. error: " << result << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception& exception) {
        std::cerr << "Error populating filter. error: " << exception.what() << '\n';
        std::exit(EXIT_FAILURE);
    }

    if (check) {
        std::cerr << "[+] Start checking\n";
        size_t notFound = 0;
        for (uint64_t key : keys) {
            if (!filter.Contain(key)) {
                ++notFound;
            }
        }
        std::cerr << "[+] Found: " << (keys.size() - notFound) << " | Not found: " << notFound << '\n';
    }

    std::cerr << "[+] start XOR filter saving\n";
    std::string xorName = remove_extension(filename_only(baseName)) + "_" + std::to_string(filterCount) + extension;
    xorName = join_path(outpath, xorName);

    if (!filter.SaveToFile(xorName)) {
        std::cerr << "Error saving filter to file.\n";
        std::exit(EXIT_FAILURE);
    }
    std::cerr << "[+] XOR filter saving: done\n";
}

void saveFilter(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    saveFilterImpl<uint32_t>(Keys, baseName, filterCount, compress ? "COMPRESSED" : "UNCOMPRESSED", compress ? ".xor_c" : ".xor_u");
}

void saveFilter_ultra(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    saveFilterImpl<uint16_t>(Keys, baseName, filterCount, "ULTRA COMPRESSED", ".xor_uc");
}

void saveFilter_hyper(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    saveFilterImpl<uint8_t>(Keys, baseName, filterCount, "HYPER COMPRESSED", ".xor_hc");
}

