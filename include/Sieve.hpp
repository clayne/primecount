///
/// @file  Sieve.hpp
/// @brief This file implements a highly optimized prime sieving
///        algorithm for computing the special leaves (sometimes named
///        hard special leaves) in the combinatorial prime counting
///        algorithms (e.g. Lagarias-Miller-Odlyzko, Deleglise-Rivat,
///        Gourdon).
///
///        The Sieve class contains a sieve of Eratosthenes
///        implementation with 30 numbers per byte i.e. the 8 bits of
///        each byte correspond to the offsets: { 1, 7, 11, 13, 17,
///        19, 23, 29 }. Unlike a traditional prime sieve this sieve
///        is designed for use in the combinatorial prime counting
///        algorithms: this sieve removes primes as well as multiples
///        of primes and it counts the number of elements that have
///        been crossed off for the first time in the sieve array.
///
///        Since there is a large number of leaves for which we have
///        to count the number of unsieved elements in the sieve
///        array, Lagarias-Miller-Odlyzko have suggested using a
///        binary indexed tree data structure (a.k.a. Fenwick tree) to
///        speedup counting. However using a binary indexed tree is
///        bad for performance as it causes many cache misses and
///        branch mispredictions. For this reason this implementation
///        instead uses a linear counter array whose elements contain
///        the total count of unsieved elements in a certain interval.
///
///        In-depth description of this algorithm:
///        https://github.com/kimwalisch/primecount/blob/master/doc/Hard-Special-Leaves.md
///
/// Copyright (C) 2024 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#ifndef SIEVE_HPP
#define SIEVE_HPP

#include <macros.hpp>
#include <popcnt.hpp>
#include <Vector.hpp>

#include <stdint.h>

#if defined(__ARM_FEATURE_SVE) && \
    __has_include(<arm_sve.h>)
  #include <arm_sve.h>
  #define ENABLE_ARM_SVE

#elif defined(__AVX512F__) && \
      defined(__AVX512VPOPCNTDQ__) && \
      __has_include(<immintrin.h>)
  #include <immintrin.h>
  #define ENABLE_AVX512_VPOPCNT

#elif defined(ENABLE_MULTIARCH_ARM_SVE)
  #include <cpu_supports_arm_sve.hpp>
  #include <arm_sve.h>
  #define ENABLE_DEFAULT

#elif defined(ENABLE_MULTIARCH_AVX512_VPOPCNT)
  #include <cpu_supports_avx512_vpopcnt.hpp>
  #include <immintrin.h>
  #define ENABLE_DEFAULT
#else
  #define ENABLE_DEFAULT
#endif

namespace primecount {

class Sieve
{
public:
  Sieve(uint64_t low, uint64_t segment_size, uint64_t wheel_size);
  void cross_off(uint64_t prime, uint64_t i);
  void cross_off_count(uint64_t prime, uint64_t i);
  static uint64_t get_segment_size(uint64_t size);

  uint64_t get_total_count() const
  {
    return total_count_;
  }

  template <typename T>
  void pre_sieve(const Vector<T>& primes, uint64_t c, uint64_t low, uint64_t high)
  {
    reset_sieve(low, high);
    for (uint64_t i = 4; i <= c; i++)
      cross_off(primes[i], i);

    init_counter(low, high);
  }

  /// Count 1 bits inside [0, stop]
  ALWAYS_INLINE uint64_t count(uint64_t stop)
  {
    ASSERT(stop >= prev_stop_);
    uint64_t start = prev_stop_ + 1;
    prev_stop_ = stop;

    // Quickly count the number of unsieved elements (in
    // the sieve array) up to a value that is close to
    // the stop number i.e. (stop - start) < counter_.dist.
    // We do this using the counter array, each element
    // of the counter array contains the number of
    // unsieved elements in the interval:
    // [i * counter_.dist, (i + 1) * counter_.dist[.
    while (counter_.stop <= stop)
    {
      start = counter_.stop;
      counter_.stop += counter_.dist;
      counter_.sum += counter_[counter_.i++];
      count_ = counter_.sum;
    }

    // Here the remaining distance is relatively small i.e.
    // (stop - start) < counter_.dist, hence we simply
    // count the remaining number of unsieved elements by
    // linearly iterating over the sieve array.
    count_ += count(start, stop);
    return count_;
  }

  /// Count 1 bits inside [start, stop]
  ALWAYS_INLINE uint64_t count(uint64_t start, uint64_t stop) const
  {
    #if defined(ENABLE_ARM_SVE)
      return count_arm_sve(start, stop);
    #elif defined(ENABLE_AVX512_VPOPCNT)
      return count_avx512(start, stop);
    #elif defined(ENABLE_MULTIARCH_ARM_SVE)
      return cpu_supports_sve ? count_arm_sve(start, stop) : count_default(start, stop);
    #elif defined(ENABLE_MULTIARCH_AVX512_VPOPCNT)
      return cpu_supports_avx512_vpopcnt ? count_avx512(start, stop) : count_default(start, stop);
    #else
      return count_default(start, stop);
    #endif
  }

private:

#if defined(ENABLE_DEFAULT)

  /// Count 1 bits inside [start, stop].
  /// The distance [start, stop] is small here < sqrt(segment_size),
  /// hence we simply count the number of unsieved elements
  /// by linearly iterating over the sieve array.
  ///
  ALWAYS_INLINE uint64_t count_default(uint64_t start, uint64_t stop) const
  {
    if (start > stop)
      return 0;

    ASSERT(stop - start < segment_size());
    uint64_t start_idx = start / 240;
    uint64_t stop_idx = stop / 240;
    uint64_t m1 = unset_smaller[start % 240];
    uint64_t m2 = unset_larger[stop % 240];

    // Branchfree bitmask calculation:
    // m1 = (start_idx != stop_idx) ? m1 : m1 & m2;
    m1 = (m1 * (start_idx != stop_idx)) | ((m1 & m2) * (start_idx == stop_idx));
    // m2 = (start_idx != stop_idx) ? m2 : 0;
    m2 *= (start_idx != stop_idx);

    const uint64_t* sieve64 = (const uint64_t*) sieve_.data();
    uint64_t start_bits = sieve64[start_idx] & m1;
    uint64_t stop_bits = sieve64[stop_idx] & m2;
    uint64_t cnt = popcnt64(start_bits);
    cnt += popcnt64(stop_bits);

    for (uint64_t i = start_idx + 1; i < stop_idx; i++)
      cnt += popcnt64(sieve64[i]);

    return cnt;
  }

#endif

#if defined(ENABLE_AVX512_VPOPCNT) || \
    defined(ENABLE_MULTIARCH_AVX512_VPOPCNT)

  /// Count 1 bits inside [start, stop].
  /// The distance [start, stop] is small here < sqrt(segment_size),
  /// hence we simply count the number of unsieved elements
  /// by linearly iterating over the sieve array.
  ///
  #if defined(ENABLE_MULTIARCH_AVX512_VPOPCNT)
    __attribute__ ((target ("avx512f,avx512vpopcntdq")))
  #endif
  uint64_t count_avx512(uint64_t start, uint64_t stop) const
  {
    if (start > stop)
      return 0;

    ASSERT(stop - start < segment_size());
    uint64_t start_idx = start / 240;
    uint64_t stop_idx = stop / 240;
    uint64_t m1 = unset_smaller[start % 240];
    uint64_t m2 = unset_larger[stop % 240];

    // Branchfree bitmask calculation:
    // m1 = (start_idx != stop_idx) ? m1 : m1 & m2;
    m1 = (m1 * (start_idx != stop_idx)) | ((m1 & m2) * (start_idx == stop_idx));
    // m2 = (start_idx != stop_idx) ? m2 : 0;
    m2 *= (start_idx != stop_idx);

    const uint64_t* sieve64 = (const uint64_t*) sieve_.data();
    uint64_t start_bits = sieve64[start_idx] & m1;
    uint64_t stop_bits = sieve64[stop_idx] & m2;
    __m512i vec = _mm512_set_epi64(0, 0, 0, 0, 0, 0, stop_bits, start_bits);
    __m512i vcnt = _mm512_popcnt_epi64(vec);
    uint64_t i = start_idx + 1;

    // Compute this for loop using AVX512.
    // for (i = start_idx + 1; i < stop_idx; i++)
    //   cnt += popcnt64(sieve64[i]);

    for (; i + 8 < stop_idx; i += 8)
    {
      vec = _mm512_loadu_epi64(&sieve64[i]);
      vec = _mm512_popcnt_epi64(vec);
      vcnt = _mm512_add_epi64(vcnt, vec);
    }

    __mmask8 mask = (__mmask8) (0xff >> (i + 8 - stop_idx));
    vec = _mm512_maskz_loadu_epi64(mask, &sieve64[i]);
    vec = _mm512_popcnt_epi64(vec);
    vcnt = _mm512_add_epi64(vcnt, vec);
    return _mm512_reduce_add_epi64(vcnt);
  }

#elif defined(ENABLE_ARM_SVE) || \
      defined(ENABLE_MULTIARCH_ARM_SVE)

  /// Count 1 bits inside [start, stop].
  /// The distance [start, stop] is small here < sqrt(segment_size),
  /// hence we simply count the number of unsieved elements
  /// by linearly iterating over the sieve array.
  ///
  #if defined(ENABLE_MULTIARCH_ARM_SVE)
    __attribute__ ((target ("arch=armv8-a+sve")))
  #endif
  uint64_t count_arm_sve(uint64_t start, uint64_t stop) const
  {
    if (start > stop)
      return 0;

    ASSERT(stop - start < segment_size());
    uint64_t start_idx = start / 240;
    uint64_t stop_idx = stop / 240;
    uint64_t m1 = unset_smaller[start % 240];
    uint64_t m2 = unset_larger[stop % 240];

    // Branchfree bitmask calculation:
    // m1 = (start_idx != stop_idx) ? m1 : m1 & m2;
    m1 = (m1 * (start_idx != stop_idx)) | ((m1 & m2) * (start_idx == stop_idx));
    // m2 = (start_idx != stop_idx) ? m2 : 0;
    m2 *= (start_idx != stop_idx);

    const uint64_t* sieve64 = (const uint64_t*) sieve_.data();
    uint64_t start_bits = sieve64[start_idx] & m1;
    uint64_t stop_bits = sieve64[stop_idx] & m2;
    ASSERT(svcntd() >= 2);
    svuint64_t vec = svinsr_n_u64(svdup_u64(start_bits), stop_bits);
    svuint64_t vcnt = svcnt_u64_z(svwhilelt_b64(0, 2), vec);
    uint64_t i = start_idx + 1;

    // Compute this for loop using ARM SVE.
    // for (i = start_idx + 1; i < stop_idx; i++)
    //   cnt += popcnt64(sieve64[i]);

    for (; i + svcntd() < stop_idx; i += svcntd())
    {
      vec = svld1_u64(svptrue_b64(), &sieve64[i]);
      vec = svcnt_u64_x(svptrue_b64(), vec);
      vcnt = svadd_u64_x(svptrue_b64(), vcnt, vec);
    }

    svbool_t pg = svwhilelt_b64(i, stop_idx);
    vec = svld1_u64(pg, &sieve64[i]);
    vec = svcnt_u64_z(pg, vec);
    vcnt = svadd_u64_x(svptrue_b64(), vcnt, vec);
    return svaddv_u64(svptrue_b64(), vcnt);
  }

#endif

  void add(uint64_t prime);
  void allocate_counter(uint64_t low);
  void init_counter(uint64_t low, uint64_t high);
  void reset_counter();
  void reset_sieve(uint64_t low, uint64_t high);
  uint64_t segment_size() const;
  static const Array<uint64_t, 240> unset_smaller;
  static const Array<uint64_t, 240> unset_larger;

  struct Wheel
  {
    uint32_t multiple;
    uint32_t index;

    Wheel() = default;
    Wheel(uint32_t m, uint32_t i)
      : multiple(m),
        index(i)
    { }
  };

  struct Counter
  {
    uint64_t stop = 0;
    uint64_t dist = 0;
    uint64_t log2_dist = 0;
    uint64_t sum = 0;
    uint64_t i = 0;
    Vector<uint32_t> counter;

    uint32_t& operator[](std::size_t pos)
    {
      return counter[pos];
    }

    uint32_t operator[](std::size_t pos) const
    {
      return counter[pos];
    }
  };

  uint64_t start_ = 0;
  uint64_t prev_stop_ = 0;
  uint64_t count_ = 0;
  uint64_t total_count_ = 0;
  Vector<uint8_t> sieve_;
  Vector<Wheel> wheel_;
  Counter counter_;
};

} // namespace

#endif
