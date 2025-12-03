#pragma once

#include <stdint.h>
#include <stdlib.h>

// Если компилируется под MSVC, используем стандартные заголовки
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#elif defined(__clang__)

static inline uint64_t _udiv128(uint64_t nHi, uint64_t nLo, uint64_t d, uint64_t* rem) {
    uint64_t quotient = 0;
    uint64_t r = nHi;
    // Последовательно обрабатываем 64 бит nLo (от старшего к младшему)
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((nLo >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            quotient |= (1ULL << i);
        }
    }
    *rem = r;
    return quotient;
}

#else

#define CHAR_BIT 8
//===============================================================
// Реализация арифметических инстриксиков, имитирующих MSVC (без __uint128_t)
//---------------------------------------------------------------

// _addcarry_u64: складывает два 64-битных числа с переносом.
static inline unsigned char _addcarry_u64(unsigned char c, uint64_t a, uint64_t b, uint64_t* sum) {
    uint64_t temp = a + b;
    unsigned char carry1 = (temp < a) ? 1 : 0;
    uint64_t result = temp + c;
    unsigned char carry2 = (result < temp) ? 1 : 0;
    *sum = result;
    return (carry1 || carry2) ? 1 : 0;
}

// _subborrow_u64: вычитает b и перенос (borrow) из a.
static inline unsigned char _subborrow_u64(unsigned char borrow, uint64_t a, uint64_t b, uint64_t* diff) {
    uint64_t temp = a - b;
    unsigned char borrow1 = (a < b) ? 1 : 0;
    uint64_t result = temp - borrow;
    unsigned char borrow2 = (temp < borrow) ? 1 : 0;
    *diff = result;
    return (borrow1 || borrow2) ? 1 : 0;
}

// _umul128: перемножает два 64-битных числа, возвращая нижние 64 бита, а старшие записывает в *high.
static inline uint64_t _umul128(uint64_t a, uint64_t b, uint64_t* high) {
    // Разбиваем операнды на 32-битные части
    const uint64_t a_lo = a & 0xFFFFFFFFULL;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFFFFFFULL;
    const uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t middle = p1 + p2;
    unsigned char carry = (middle < p1) ? 1 : 0;
    uint64_t middle_low = middle << 32;
    uint64_t low = p0 + middle_low;
    unsigned char carry2 = (low < p0) ? 1 : 0;
    *high = p3 + (middle >> 32) + carry + carry2;
    return low;
}

// __shiftright128: сдвигает 128-битное число (представленное (high, low)) вправо на shift бит,
// возвращает нижние 64 бита результата.
static inline uint64_t __shiftright128(uint64_t low, uint64_t high, uint8_t shift) {
    if (shift == 0)
        return low;
    else if (shift < 64)
        return (low >> shift) | (high << (64 - shift));
    else if (shift < 128)
        return high >> (shift - 64);
    else
        return 0;
}

// __shiftleft128: сдвигает 128-битное число (high, low) влево на shift бит,
// возвращает старшие 64 бита результата.
static inline uint64_t __shiftleft128(uint64_t low, uint64_t high, uint8_t shift) {
    if (shift == 0)
        return high;
    else if (shift < 64)
        return (high << shift) | (low >> (64 - shift));
    else if (shift < 128)
        return low << (shift - 64);
    else
        return 0;
}

// _tzcnt_u64: считает количество нулевых битов справа (если x==0, возвращает 64).
static inline uint64_t _tzcnt_u64(uint64_t x) {
    return x ? __builtin_ctzll(x) : 64;
}

// _lzcnt_u64: считает количество нулевых битов слева (если x==0, возвращает 64).
static inline uint64_t _lzcnt_u64(uint64_t x) {
    return x ? __builtin_clzll(x) : 64;
}

// _bzhi_u64: обнуляет все биты x начиная с позиции n (если n>=64, возвращает x).
static inline uint64_t _bzhi_u64(uint64_t x, uint32_t n) {
    if (n >= 64)
        return x;
    return x & ((1ULL << n) - 1);
}

// _udiv64: делит 64-битное число a на 32-битный делитель b,
// записывает остаток в *remainder и возвращает 32-битный частное.
static inline uint32_t _udiv64(uint64_t a, uint32_t b, uint32_t* remainder) {
    *remainder = (uint32_t)(a % b);
    return (uint32_t)(a / b);
}

// _udiv128: делит 128-битное число, представленное парами (nHi, nLo), на 64-битный делитель d.
// Возвращает 64-битное частное и записывает остаток в *rem.
static inline uint64_t _udiv128(uint64_t nHi, uint64_t nLo, uint64_t d, uint64_t* rem) {
    uint64_t quotient = 0;
    uint64_t r = nHi;
    // Последовательно обрабатываем 64 бит nLo (от старшего к младшему)
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((nLo >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            quotient |= (1ULL << i);
        }
    }
    *rem = r;
    return quotient;
}

//===============================================================
// Реализация SSE-интринсиков для работы с __m128, __m128i, __m128d
//---------------------------------------------------------------

typedef union {
    float f[4];
} __m128;

typedef union {
    int i[4];
    int32_t i32[4];
    int64_t ll[2];
    uint32_t u32[4];
    uint64_t ull[2];
} __m128i;

typedef union {
    double d[2];
    int64_t ll[2];
} __m128d;

// _mm_set_ss: создаёт __m128, устанавливая нижние 32 бита равными x, остальные – 0.
static inline __m128 _mm_set_ss(float x) {
    __m128 r;
    r.f[0] = x;
    r.f[1] = 0.0f;
    r.f[2] = 0.0f;
    r.f[3] = 0.0f;
    return r;
}

// _mm_castps_si128: без изменения битового представления переводит __m128 в __m128i.
static inline __m128i _mm_castps_si128(__m128 a) {
    __m128i r;
    // Прямое копирование битов (только нижние 128 бит)
    r.i[0] = *(int*)&a.f[0];
    r.i[1] = *(int*)&a.f[1];
    r.i[2] = *(int*)&a.f[2];
    r.i[3] = *(int*)&a.f[3];
    return r;
}

static inline __m128i _mm_cvtsi64_si128(int64_t a) {
    __m128i r;
    r.ll[0] = a;
    r.ll[1] = 0;
    return r;
}

// _mm_cvtsi128_si32: возвращает нижние 32 бита из __m128i.
static inline int _mm_cvtsi128_si32(__m128i a) {
    return a.i[0];
}

// _mm_set_sd: создаёт __m128d, устанавливая нижние 64 бита равными a, остальные – 0.0.
static inline __m128d _mm_set_sd(double a) {
    __m128d r;
    r.d[0] = a;
    r.d[1] = 0.0;
    return r;
}

// _mm_castpd_si128: без изменения битового представления переводит __m128d в __m128i.
static inline __m128i _mm_castpd_si128(__m128d a) {
    __m128i r;
    // Копируем 128 бит
    r.ll[0] = *((const int64_t*)&a.d[0]);
    r.ll[1] = *((const int64_t*)&a.d[1]);
    return r;
}

// _mm_cvtsi128_si64: возвращает нижние 64 бита из __m128i.
static inline int64_t _mm_cvtsi128_si64(__m128i a) {
    return a.ll[0];
}

// _mm_cvtsi32_si128: превращает int в __m128i (нижние 32 бита равны a, остальные 0).
static inline __m128i _mm_cvtsi32_si128(int a) {
    __m128i r;
    r.i[0] = a;
    r.i[1] = 0;
    r.i[2] = 0;
    r.i[3] = 0;
    return r;
}

// _mm_castsi128_ps: без изменения битового представления переводит __m128i в __m128.
static inline __m128 _mm_castsi128_ps(__m128i a) {
    __m128 r;
    r.f[0] = *(float*)&a.i[0];
    r.f[1] = *(float*)&a.i[1];
    r.f[2] = *(float*)&a.i[2];
    r.f[3] = *(float*)&a.i[3];
    return r;
}

// _mm_cvtss_f32: возвращает нижнее значение float из __m128.
static inline float _mm_cvtss_f32(__m128 a) {
    return a.f[0];
}

// _mm_castsi128_pd: без изменения битового представления переводит __m128i в __m128d.
static inline __m128d _mm_castsi128_pd(__m128i a) {
    __m128d r;
    r.ll[0] = a.ll[0];
    r.ll[1] = a.ll[1];
    return r;
}


// _mm_cvtsd_f64: возвращает нижнее значение double из __m128d.
static inline double _mm_cvtsd_f64(__m128d a) {
    return a.d[0];
}

#endif  // _MSC_VER
