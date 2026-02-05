#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <string>
#include <cstring> // ��� memset
#include <omp.h>   // ��� OpenMP
#include <vector>
#include <sys/stat.h>
#include "xor_filter.h"
#include <iomanip>
#include <atomic>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <execution>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <execution>


bool force = false;

static uint64_t XOR_MAX = XOR_MAX_SIZE;

#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE __attribute__((always_inline)) inline
#endif


typedef union hash160_u {
	uint8_t uc[32];
	uint64_t      ul[4];
    uint128_t uu[2];
} hash160_t;

void saveFilter(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);
void saveFilter_ultra(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);
void saveFilter_hyper(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount);

static const unsigned char unhex_tab[80] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
  0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static inline unsigned char*
unhex(unsigned char* str, size_t str_sz,
	unsigned char* unhexed, size_t unhexed_sz) {
	int i, j;
	for (i = j = 0; i < str_sz && j < unhexed_sz; i += 2, ++j) {
		unhexed[j] = (unhex_tab[str[i + 0] & 0x4f] & 0xf0) |
			(unhex_tab[str[i + 1] & 0x4f] & 0x0f);
	}
	return unhexed;
} 


unsigned char*
hex(unsigned char* buf, size_t buf_sz,
	unsigned char* hexed, size_t hexed_sz) {
	int i, j;
	--hexed_sz;
	for (i = j = 0; i < buf_sz && j < hexed_sz; ++i, j += 2) {
		snprintf((char*)hexed + j, 3, "%02x", buf[i]);
	}
	hexed[j] = 0;
	return hexed;
}

long long estimateTotalLines(std::vector<std::string> FileNames) {

    long long TotalCount = 0;

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    for (size_t fileIdx = 0; fileIdx < FileNames.size(); ++fileIdx) {
        const long long sampleSize = 1000000;
        const long long smallFileThreshold = 1000;
        std::string line;
        std::vector<long long> lineSizes;
        std::ifstream f(FileNames[fileIdx]);
        if (!f.is_open()) {
            std::cerr << "Error opening file: " << FileNames[fileIdx] << std::endl;
            return 0;
        }

        long long totalSampledLinesSize = 0;

        f.seekg(0, std::ios::end);
        long long fileSize = f.tellg();
        if (fileSize == -1) {
            std::cerr << "Error determining file size: " << FileNames[fileIdx] << std::endl;
            return 0; 
        }
        f.seekg(0, std::ios::beg);

        if (fileSize <= smallFileThreshold) {
            long long lineCount = 0;
            while (std::getline(f, line)) {
                totalSampledLinesSize += static_cast<long long>(line.size() + 1); 
                ++lineCount;
            }
            f.close();
            TotalCount += lineCount;
            continue;
        }

        for (long long i = 0; i < sampleSize; ++i) {
            long long randomPos = std::rand() % fileSize;
            f.seekg(randomPos, std::ios::beg);

            std::getline(f, line);
            if (std::getline(f, line)) {
                long long lineSize = static_cast<long long>(line.size() + 1);
                lineSizes.push_back(lineSize);
                totalSampledLinesSize += lineSize;
            }
            else {
                break; 
            }
        }

        f.clear();
        f.seekg(0, std::ios::beg);
        f.close();

        if (lineSizes.empty()) {
            std::cerr << "No lines sampled from file: " << FileNames[fileIdx] << std::endl;
            return 0;
        }

        double averageLineSize = totalSampledLinesSize / static_cast<double>(lineSizes.size());
        TotalCount += static_cast<long long>(fileSize / averageLineSize);
    }
    return TotalCount;
}

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

    fprintf(stderr ,"Current local time: %s\n", buffer);
}


//sort

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

        if (n <= (1u << 18)) { // 1<<18 .. 1<<22
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
            // right bigger
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
    const unsigned hw = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 8;
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

void removeDuplicates_fast(std::vector<uint64_t>& keys, unsigned threads = 0) {
    printf("[!] Starting remove duplicate\n");
    sort_unstable_parallel_u64(keys.data(), keys.size(), threads);
    keys.resize(dedup_inplace_sorted_u64(keys.data(), keys.size()));
}



void removeDuplicates(std::vector<uint64_t>& Keys) {
    printf("[!] Starting remove duplicate\n");
   // std::sort(Keys.begin(), Keys.end());
    std::sort(std::execution::par_unseq, Keys.begin(), Keys.end());

    auto last = std::unique(Keys.begin(), Keys.end());

    Keys.erase(last, Keys.end());
}

void removeDuplicates(std::vector<uint128_t>& Keys) {
    printf("[!] Starting remove duplicate\n");
    std::sort(Keys.begin(), Keys.end());

    auto last = std::unique(Keys.begin(), Keys.end());

    Keys.erase(last, Keys.end());
}



static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
static constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;

static inline uint64_t fnv1a_64(const uint8_t* buffer, size_t length) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < length; i++) {
        h ^= buffer[i];
        h *= FNV_PRIME;
    }
    return h;
}

static FORCEINLINE void unhex40_20(const char* __restrict s, uint8_t* __restrict out20) {
#pragma unroll
    for (int j = 0; j < 20; ++j) {
        const unsigned char a = unhex_tab[(unsigned char)s[j * 2 + 0] & 0x4f];
        const unsigned char b = unhex_tab[(unsigned char)s[j * 2 + 1] & 0x4f];
        out20[j] = (a & 0xf0) | (b & 0x0f);
    }
}

static FORCEINLINE uint64_t fnv1a_64_20(const uint8_t* __restrict p20) {
    uint64_t h = FNV_OFFSET_BASIS;
#pragma unroll
    for (int i = 0; i < 20; ++i) { h ^= p20[i]; h *= FNV_PRIME; }
    return h;
}

bool check = false;
bool compress = false;
bool ultra_compress = false;
bool hyper_compress = false;
bool split = false;

std::string outpath = "";

int main(int argc, char** argv) {
    //hash160_t hash;
    //struct stat sb;
    unsigned char* bloom = nullptr;
    std::ifstream f;
    size_t filterCount = 0;
    std::vector<std::string> files;
    std::string filter_type = "UNCOMPRESSED";
    FILE* outputStream;


    if (argc < 2) {
        std::cerr << "[!] Usage: \n";
        std::cerr << "[!] -i \tfile.txt (or -i file1.txt -i file2.txt)\n";
        std::cerr << "[!] -check \tchecing filter after populating\n";
        std::cerr << "[!] -compress \t Make compressed xor filter (.xor_c files) - 0.0000001% F/P chance \n";
        std::cerr << "[!] -ultra \t Make ultra compressed xor filter (.xor_uc files) - 0,001444% F/P chance \n";
        //std::cerr << "[!] -hyper \t Make hyper compressed xor filter (.xor_hc files) - 0,3556% F/P chance \n";
        std::cerr << "[!] -mini \tUse 2 147 483 644 filter size (9 GB file (4.5 for ultra "/* && 2.25 for hyper*/")\n";
        std::cerr << "[!] -max \tUse 8 589 934 584 filter size (36 GB file (18 for ultra "/*&& 9 for hyper*/") !!!CAN BE USED MORE 256 GB RAM !!!\n";
        std::cerr << "[!] -max2 \tUse 17 179 869 168 filter size (72 GB file (36 for ultra "/* && 18 for hyper*/") !!!CAN BE USED MORE 512 GB RAM !!!\n";
        std::cerr << "[!] -txt \t Split original txt's base in new txt's files (-0.txt, -1.txt...). \n";
        std::cerr << "[!] -force \t use more aggressive sort, high CPU usage\n";
        std::cerr << "[!] -o FOLDER\t Saving path\n";
        return 1;
    }



    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "-i") == 0) {
            if (arg + 1 >= argc) {
                std::cerr << "[!] Missing filename after '-i'." << std::endl;
                return 1;
            }
            files.push_back(argv[arg + 1]);
            arg++;
        }
        else if (strcmp(argv[arg], "-o") == 0) {
            if (arg + 1 >= argc) {
                std::cerr << "[!] Missing filename after '-o'." << std::endl;
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
            IS_COMPRESS = true;
            filter_type = "COMPRESSED";
        }
        else if (strcmp(argv[arg], "-ultra") == 0) {
            ultra_compress = true;
            hyper_compress = false;
            compress = false;
            IS_COMPRESS = true;
            filter_type = "ULTRA COMPRESSED";
        }
        else if (strcmp(argv[arg], "-hyper") == 0) {
            ultra_compress = false;
            hyper_compress = true;
            compress = false;
            IS_COMPRESS = true;
            filter_type = "HYPER COMPRESSED";
        }
        else {
            std::cerr << "[!] Incorrect usage. Unexpected argument: " << argv[arg] << std::endl;
            std::cerr << "[!] Usage: \n";
            std::cerr << "[!] -i \tfile.txt (or -i file1.txt -i file2.txt)\n";
            std::cerr << "[!] -check \tchecing filter after populating\n";
            std::cerr << "[!] -compress \t Make compressed xor filter (.xor_c files) - 0.0000001% F/P chance \n";
            std::cerr << "[!] -ultra \t Make ultra compressed xor filter (.xor_uc files) - 0,001444% F/P chance \n";
           // std::cerr << "[!] -hyper \t Make hyper compressed xor filter (.xor_hc files) - 0,3556% F/P chance \n";
            std::cerr << "[!] -mini \tUse 2 147 483 644 filter size (9 GB file (4.5 for ultra "/* && 2.25 for hyper*/")\n";
            std::cerr << "[!] -max \tUse 8 589 934 584 filter size (36 GB file (18 for ultra "/*&& 9 for hyper*/") !!!CAN BE USED MORE 256 GB RAM !!!\n";
            std::cerr << "[!] -max2 \tUse 17 179 869 168 filter size (72 GB file (36 for ultra "/* && 18 for hyper*/") !!!CAN BE USED MORE 512 GB RAM !!!\n";
            std::cerr << "[!] -txt \t Split original txt's base in new txt's files (-0.txt, -1.txt...). \n";
            std::cerr << "[!] -force \t use more aggressive sort, high CPU usage\n";
            std::cerr << "[!] -o FOLDER\t Saving path\n";
            return 1;
        }
    }


std::string line;
std::vector<uint64_t>Keys64;
size_t split_count = 0;
std::cerr << "[!] Making " << filter_type << " XOR filter[s] with max size: " << XOR_MAX << " [!]\n";
if (split)
{

    std::string split_name = files[0];
    size_t pos = split_name.find_last_of('.');
    if (pos != std::string::npos) {
        split_name = split_name.substr(0, pos);
    }
    split_name += "_split_" + std::to_string(split_count) + ".txt";
    outputStream = fopen(split_name.c_str(), "ab");
}

//uint128_t i = ((uint128_t)0x75f69cb3310cca27 << 64) | 0x1c827e6ab0383422;
//Keys.push_back(i);
print_current_time();
std::cerr << "[+] Starting files reading\n";
int64_t fileIdx;

std::atomic<int64_t> next_file{ 0 };
std::atomic<bool> stop{ false };

#pragma omp parallel
{
    hash160_t hash;
    char linebuf[256];


    while (true) {
        if (stop.load(std::memory_order_relaxed)) break;

        int64_t fileIdx = next_file.fetch_add(1, std::memory_order_relaxed);
        if (fileIdx >= (int64_t)files.size()) break;

        FILE* in = fopen(files[fileIdx].c_str(), "rb");
        if (!in) {
#pragma omp critical
            std::cerr << "[!] Failed to open hash160 file '" << files[fileIdx] << "'\n";
            continue;
        }

        static const size_t INBUF_SZ = 1u << 20;
        std::vector<char> inbuf(INBUF_SZ);
        setvbuf(in, inbuf.data(), _IOFBF, INBUF_SZ);

        uint64_t lineCount = 0;
        uint64_t line_ct = 0;

        while (!stop.load(std::memory_order_relaxed) && fgets(linebuf, (int)sizeof(linebuf), in)) {
            const char* s = linebuf;
            size_t n = strlen(linebuf);
            if (n && linebuf[n - 1] == '\n') --n;
            if (n && linebuf[n - 1] == '\r') --n;


            if (n >= 2 && s[0] == '0' && s[1] == 'x') { s += 2; n -= 2; }

            if (n >= 66 && s[0] == '0' && (s[1] == '2' || s[1] == '3' || s[1] == '4')) { s += 2; n -= 2; }

            if (n < 40) goto next_line;

            unhex40_20(s, hash.uc);

            if (split) {
#pragma omp critical
                fwrite(s, 1, n, outputStream);
                fputc('\n', outputStream);
            }

            if (IS_COMPRESS) {
                hash.ul[3] = fnv1a_64_20(hash.uc);
            }
            else {
                hash.uc[3] = hash.uc[3] & hash.uc[16];
                hash.uc[7] = hash.uc[7] & hash.uc[17];
                hash.uc[11] = hash.uc[11] & hash.uc[18];
                hash.uc[15] = hash.uc[15] & hash.uc[19];
            }

            if (hash.uu[0] == 0) goto next_line;

#pragma omp critical
            {
                if (IS_COMPRESS)
                {
                    Keys64.push_back(hash.ul[3]);
                }
                else
                {
                    Keys64.push_back(hash.uu[0].m_hi);
                    Keys64.push_back(hash.uu[0].m_lo);
                }
            }

        next_line:
#pragma omp atomic
            lineCount++;
            line_ct++;
            if (line_ct == 1000000)
            {
                std::cerr << "[+] Processing " << files[fileIdx] << "  line: " << lineCount << "\r";
                line_ct = 0;
            }



            if (Keys64.size() >= (XOR_MAX)) {
#pragma omp critical
                {
                    std::cerr << "[+] Processing " << files[fileIdx] << "  line: " << lineCount << "\r";
                    if (!force)
                    {
                        removeDuplicates_fast(Keys64);
                    }
                    else
                    {

                        removeDuplicates(Keys64);
                    }
                    if (Keys64.size() < (XOR_MAX))
                    {
                        printf("[!] Deleted %llu duplicate hashes, adding new lines\n", XOR_MAX - Keys64.size());
                        continue;
                    }
                    if (ultra_compress)
                    {
                        saveFilter_ultra(Keys64, files[0], filterCount);
                    }
                    else if (hyper_compress)
                    {
                        saveFilter_hyper(Keys64, files[0], filterCount);
                    }
                    else
                    {
                        saveFilter(Keys64, files[0], filterCount);
                    }
                    if (split)
                    {
                        split_count++;
                        std::string split_name = files[0];
                        size_t pos = split_name.find_last_of('.');
                        if (pos != std::string::npos) {
                            split_name = split_name.substr(0, pos);
                        }
                        split_name += "_split_" + std::to_string(split_count) + ".txt";
                        outputStream = fopen(split_name.c_str(), "ab");
                    }
                    filterCount++;
                    Keys64.clear();
                }
            }

        }
        std::cerr << "\n[+] Processed " << files[fileIdx] << "  lines: " << lineCount << "\n";

    }

}
if (!Keys64.empty()) {
    if (!force)
    {
        removeDuplicates_fast(Keys64);
    }
    else
    {

        removeDuplicates(Keys64);
    }
    if (ultra_compress)
    {
        saveFilter_ultra(Keys64, files[0], filterCount);
    }
    else if (hyper_compress)
    {
        saveFilter_hyper(Keys64, files[0], filterCount);
    }
    else
    {
        saveFilter(Keys64, files[0], filterCount);
    }
}

std::cerr << "\n[+] Processing all files: done" << std::endl;
print_current_time();
return 0;
}

#define NUM_COUNT 10999985 //10999999

void saveFilter(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    if(compress)
    {
        std::cerr << "\n[+] Start COMPRESSED XOR filter allocating\n";
    }
    else
    {
        std::cerr << "\n[+] Start UNCOMPRESSED XOR filter allocating\n";
    }
    
    uint64_t n = 0;
    xorbinaryfusefilter_lowmem4wise::XorBinaryFuseFilter<uint64_t, uint32_t> filter(Keys.size());

    //std::cerr << "Start print keys\n";

    //for (uint64_t i = 0; i < Keys.size(); i++)
    //{
    //    cout << Keys[i] << "\n";
    //}
    std::cerr << "[+] Start keys shuffle\n";
    std::mt19937_64 rng(0x726b2b9d438b9d4d);
    std::shuffle(Keys.begin(), Keys.end(), rng);
    std::cerr << "[+] Start XOR filter populating (size: " << Keys.size() << " )\n";
    try {
        int res = filter.AddAll(Keys.data(), 0, Keys.size());
        if (res != 0) {
            std::cerr << "Error populating filter. error: " << res << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error populating filter. error: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
    if (check)
    {
        printf("Start checking\n");
        size_t not_found = 0;
        size_t found = 0;
        for (uint64_t i = 0; i < Keys.size(); i++) {
    
            if (!filter.Contain(Keys[i]))
            {
                not_found++;
                // std::cerr << "not found " << Keys[i] << "\r";
            }
            else
            {
                found++;
            }
    
    
        }
        printf("\nFound: %llu | Not found: %llu \n", found, not_found);
    }
    auto filename_only = [](const std::string& p) -> std::string {
        size_t s1 = p.find_last_of("/\\");          
        if (s1 == std::string::npos) return p;
        return p.substr(s1 + 1);
        };

    auto remove_ext = [](const std::string& name) -> std::string {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) return name;
        return name.substr(0, dot);
        };

    auto join_path = [](std::string dir, const std::string& name) -> std::string {
        if (dir.empty()) return name;          
        char last = dir.back();
        if (last != '\\' && last != '/') dir.push_back('\\');
        return dir + name;
        };

    std::cerr << "[+] start XOR filter saving\n";

    std::string xor_name = remove_ext(filename_only(baseName));

    if (compress) xor_name += "_" + std::to_string(filterCount) + ".xor_c";
    else          xor_name += "_" + std::to_string(filterCount) + ".xor_u";


    xor_name = join_path(outpath, xor_name);
    

    if (!filter.SaveToFile(xor_name.c_str())) {
        std::cerr << "Error saving filter to file.\n";
        exit(EXIT_FAILURE);
    }
    std::cerr << "[+] XOR filter saving: done\n";
}

void saveFilter_ultra(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    std::cerr << "\n[+] Start ULTRA COMPRESSED XOR filter allocating\n";
    uint64_t n = 0;
    xorbinaryfusefilter_lowmem4wise::XorBinaryFuseFilter<uint64_t, uint16_t> filter(Keys.size());

    //std::cerr << "Start print keys\n";

    //for (uint64_t i = 0; i < Keys.size(); i++)
    //{
    //    cout << Keys[i] << "\n";
    //}
    std::cerr << "[+] Start keys shuffle\n";
    std::mt19937_64 rng(0x726b2b9d438b9d4d);
    std::shuffle(Keys.begin(), Keys.end(), rng);
    std::cerr << "[+] Start XOR filter populating (size: " << Keys.size() << " )\n";
    try {
        int res = filter.AddAll(Keys.data(), 0, Keys.size());
        if (res != 0) {
            std::cerr << "Error populating filter. error: " << res << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error populating filter. error: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
    if (check)
    {
        printf("Start checking\n");
        size_t not_found = 0;
        size_t found = 0;
        for (uint64_t i = 0; i < Keys.size(); i++) {

            if (!filter.Contain(Keys[i]))
            {
                not_found++;
                // std::cerr << "not found " << Keys[i] << "\r";
            }
            else
            {
                found++;
            }


        }
        printf("\nFound: %llu | Not found: %llu \n", found, not_found);
    }
    auto filename_only = [](const std::string& p) -> std::string {
        size_t s1 = p.find_last_of("/\\");
        if (s1 == std::string::npos) return p;
        return p.substr(s1 + 1);
        };

    auto remove_ext = [](const std::string& name) -> std::string {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) return name;
        return name.substr(0, dot);
        };

    auto join_path = [](std::string dir, const std::string& name) -> std::string {
        if (dir.empty()) return name;
        char last = dir.back();
        if (last != '\\' && last != '/') dir.push_back('\\');
        return dir + name;
        };

    std::cerr << "[+] start XOR filter saving\n";

    std::string xor_name = remove_ext(filename_only(baseName));

    xor_name += "_" + std::to_string(filterCount) + ".xor_uc";



    xor_name = join_path(outpath, xor_name);

    if (!filter.SaveToFile(xor_name.c_str())) {
        std::cerr << "Error saving filter to file.\n";
        exit(EXIT_FAILURE);
    }
    std::cerr << "[+] XOR filter saving: done\n";
}

void saveFilter_hyper(std::vector<uint64_t>& Keys, const std::string& baseName, size_t filterCount) {
    std::cerr << "\n[+] Start HYPER COMPRESSED XOR filter allocating\n";
    uint64_t n = 0;
    xorbinaryfusefilter_lowmem4wise::XorBinaryFuseFilter<uint64_t, uint8_t> filter(Keys.size());

    //std::cerr << "Start print keys\n";

    //for (uint64_t i = 0; i < Keys.size(); i++)
    //{
    //    cout << Keys[i] << "\n";
    //}
    std::cerr << "[+] Start keys shuffle\n";
    std::mt19937_64 rng(0x726b2b9d438b9d4d);
    std::shuffle(Keys.begin(), Keys.end(), rng);
    std::cerr << "[+] Start XOR filter populating (size: " << Keys.size() << " )\n";
    try {
        int res = filter.AddAll(Keys.data(), 0, Keys.size());
        if (res != 0) {
            std::cerr << "Error populating filter. error: " << res << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error populating filter. error: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
    if (check)
    {
        printf("Start checking\n");
        size_t not_found = 0;
        size_t found = 0;
        for (uint64_t i = 0; i < Keys.size(); i++) {

            if (!filter.Contain(Keys[i]))
            {
                not_found++;
                // std::cerr << "not found " << Keys[i] << "\r";
            }
            else
            {
                found++;
            }


        }
        printf("\nFound: %llu | Not found: %llu \n", found, not_found);
    }
    auto filename_only = [](const std::string& p) -> std::string {
        size_t s1 = p.find_last_of("/\\");
        if (s1 == std::string::npos) return p;
        return p.substr(s1 + 1);
        };

    auto remove_ext = [](const std::string& name) -> std::string {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) return name;
        return name.substr(0, dot);
        };

    auto join_path = [](std::string dir, const std::string& name) -> std::string {
        if (dir.empty()) return name;
        char last = dir.back();
        if (last != '\\' && last != '/') dir.push_back('\\');
        return dir + name;
        };

    std::cerr << "[+] start XOR filter saving\n";

    std::string xor_name = remove_ext(filename_only(baseName));

    xor_name += "_" + std::to_string(filterCount) + ".xor_hc";



    xor_name = join_path(outpath, xor_name);

    if (!filter.SaveToFile(xor_name.c_str())) {
        std::cerr << "Error saving filter to file.\n";
        exit(EXIT_FAILURE);
    }
    std::cerr << "[+] XOR filter saving: done\n";
}

//OLD LOGIC
/*
int64_t fileIdx;

std::atomic<bool> need_build{ false };

#pragma omp parallel for private(line)
    for (fileIdx = 0; fileIdx < (int64_t)files.size(); ++fileIdx) {
        std::ifstream f;
        std::string line;
        long long lineCount = 0;
        long line_ct = 0;
        f.open(files[fileIdx]);
        hash160_t hash;
        if (!f.is_open()) {
            std::cerr << "[!] Failed to open hash160 file '" << files[fileIdx] << "'" << std::endl;
            continue;
        }

        std::vector<char> iobuf(1 << 20);
        f.rdbuf()->pubsetbuf(iobuf.data(), (std::streamsize)iobuf.size());
        line.reserve(128);

        constexpr size_t LOCAL_FLUSH = 1u << 15; // 32768 элементов (настрой)
        std::vector<uint64_t> localKeys;
        localKeys.reserve(LOCAL_FLUSH * 2);

        auto flush_local = [&]() {
            if (localKeys.empty()) return;
#pragma omp critical
            {
                Keys64.insert(Keys64.end(), localKeys.begin(), localKeys.end());
                localKeys.clear();
                if (Keys64.size() >= XOR_MAX) need_build.store(true, std::memory_order_relaxed);
            }
        };



        while (std::getline(f, line)) {
//                if (line.substr(0, 2) == "0x") {
//                    line = line.substr(2);
//                }
//                if ((line.size() >= 66) &&
//                    (line.substr(0, 2) == "02" || line.substr(0, 2) == "03" || line.substr(0, 2) == "04")) {
//                    line = line.substr(2);
//                }
//
//                //uint32_t lastBytes = 0;
//                uint8_t lastBytes[4];
//
//                unhex((unsigned char*)(line.c_str()), 40, hash.uc, 20);
//                if (split)
//                {
//                    line += "\n";
//                    fwrite(line.c_str(), 1, line.size(), outputStream);
//                }
//                if (IS_COMPRESS)
//                {
//                    hash.ul[3] = fnv1a_64(hash.uc, 20);
//
//                }
//                else
//                {
//                    hash.uc[3] = hash.uc[3] & hash.uc[16];
//                    hash.uc[7] = hash.uc[7] & hash.uc[17];
//                    hash.uc[11] = hash.uc[11] & hash.uc[18];
//                    hash.uc[15] = hash.uc[15] & hash.uc[19];
//                }
//
//
//
//                if (hash.uu[0] == 0)
//                {
//                    continue;
//                }
//#pragma omp critical
//                {
//                    if (IS_COMPRESS)
//                    {
//                        Keys64.push_back(hash.ul[3]);
//                    }
//                    else
//                    {
//                        Keys64.push_back(hash.uu[0].m_hi);
//                        Keys64.push_back(hash.uu[0].m_lo);
//                    }
//                }

            const char* s = line.data();
            size_t n = line.size();

            if (n >= 2 && s[0] == '0' && s[1] == 'x') { s += 2; n -= 2; }

            if (n >= 66 && s[0] == '0' && (s[1] == '2' || s[1] == '3' || s[1] == '4')) { s += 2; n -= 2; }

            if (n < 40) goto next_line;

            unhex40_20(s, hash.uc);

            if (split) {
                fwrite(s, 1, n, outputStream);
                fputc('\n', outputStream);
            }

            if (IS_COMPRESS) {
                hash.ul[3] = fnv1a_64_20(hash.uc);
            }
            else {
                hash.uc[3] = hash.uc[3] & hash.uc[16];
                hash.uc[7] = hash.uc[7] & hash.uc[17];
                hash.uc[11] = hash.uc[11] & hash.uc[18];
                hash.uc[15] = hash.uc[15] & hash.uc[19];
            }

            if (hash.uu[0] == 0) goto next_line;

            if (IS_COMPRESS) {
                localKeys.push_back(hash.ul[3]);
            }
            else {
                localKeys.push_back(hash.uu[0].m_hi);
                localKeys.push_back(hash.uu[0].m_lo);
            }

            if (localKeys.size() >= LOCAL_FLUSH) flush_local();

        next_line:
  //  #pragma omp atomic
            lineCount++;
            line_ct++;
            if (line_ct == 1000000)
            {
                std::cerr << "[+] Processing " << files[fileIdx] << " line: " << lineCount << "\r";
                line_ct = 0;
            }



            if (Keys64.size() >= (XOR_MAX)) {
                {

                    removeDuplicates(Keys64);
                    if (Keys64.size() < (XOR_MAX))
                    {
                        printf("[!] Deleted %llu duplicate hashes, adding new lines\n", XOR_MAX - Keys64.size());
                        continue;
                    }
                    if (ultra_compress)
                    {
                        saveFilter_ultra(Keys64, files[0], filterCount);
                    }
                    else if (hyper)
                    {
                        saveFilter_hyper(Keys64, files[0], filterCount);
                    }
                    else
                    {
                        saveFilter(Keys64, files[0], filterCount);
                    }
                    if (split)
                    {
                        split_count++;
                        std::string split_name = files[0];
                        size_t pos = split_name.find_last_of('.');
                        if (pos != std::string::npos) {
                            split_name = split_name.substr(0, pos);
                        }
                        split_name += "_split_" + std::to_string(split_count) + ".txt";
                        outputStream = fopen(split_name.c_str(), "ab");
                    }
                    filterCount++;
                    Keys64.clear();
                }
            }

        }
        std::cerr << "\n[+] Processed " << files[fileIdx] << " lines: " << lineCount << "\n";

        f.close();
    }


*/