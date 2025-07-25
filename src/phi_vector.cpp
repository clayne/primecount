///
/// @file  phi_vector.cpp
/// @brief The PhiCache class calculates the partial sieve function
///        (a.k.a. Legendre-sum) using the recursive formula:
///        phi(x, a) = phi(x, a - 1) - phi(x / primes[a], a - 1).
///        phi(x, a) counts the numbers <= x that are not divisible
///        by any of the first a primes. The algorithm used is an
///        optimized version of the recursive algorithm described in
///        Tomás Oliveira e Silva's paper [2]. I have added 5
///        optimizations to my implementation which speed up the
///        computation by several orders of magnitude.
///
///    [1] In-depth description of primecount's phi(x, a) implementation:
///        https://github.com/kimwalisch/primecount/blob/master/doc/Partial-Sieve-Function.pdf
///    [2] Tomás Oliveira e Silva, Computing pi(x): the combinatorial
///        method, Revista do DETUA, vol. 4, no. 6, March 2006, p. 761.
///        http://sweet.ua.pt/tos/bib/5.4.pdf
///
/// Copyright (C) 2025 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <phi_vector.hpp>
#include <primecount-internal.hpp>
#include <BitSieve240.hpp>
#include <fast_div.hpp>
#include <imath.hpp>
#include <macros.hpp>
#include <min.hpp>
#include <PhiTiny.hpp>
#include <PiTable.hpp>
#include <Vector.hpp>
#include <popcnt.hpp>

#include <stdint.h>
#include <algorithm>
#include <utility>

namespace {

using namespace primecount;

template <typename Primes>
class PhiCache : public BitSieve240
{
public:
  PhiCache(uint64_t x,
           uint64_t a,
           const Primes& primes,
           const PiTable& pi) :
    primes_(primes),
    pi_(pi)
  {
    // We cache phi(x, a) if a <= max_a.
    // The value max_a = 100 has been determined empirically
    // by running benchmarks. Using a smaller or larger
    // max_a with the same amount of memory (max_megabytes)
    // decreases the performance.
    uint64_t max_a = 100;

    // Make sure we cache only frequently used values
    a = a - min(a, 30);
    max_a = min(a, max_a);

    if (max_a <= PhiTiny::max_a())
      return;

    // We cache phi(x, a) if x <= max_x.
    // The value max_x = sqrt(x) has been determined by running
    // S2_hard(x) and D(x) benchmarks from 1e12 to 1e21.
    uint64_t max_x = isqrt(x);

    // The cache (i.e. the sieve array)
    // uses at most max_megabytes per thread.
    uint64_t max_megabytes = 16;
    uint64_t indexes = max_a - PhiTiny::max_a();
    uint64_t max_bytes = max_megabytes << 20;
    uint64_t max_bytes_per_index = max_bytes / indexes;
    uint64_t numbers_per_byte = 240 / sizeof(sieve_t);
    uint64_t cache_limit = max_bytes_per_index * numbers_per_byte;
    max_x = min(max_x, cache_limit);
    max_x_size_ = ceil_div(max_x, 240);

    // For tiny computations caching is not worth it
    if (max_x_size_ < 8)
      return;

    // Make sure that there are no uninitialized
    // bits in the last sieve array element.
    max_x_ = max_x_size_ * 240 - 1;
    max_a_ = max_a;
  }

  /// Calculate phi(x, a) using the recursive formula:
  /// phi(x, a) = phi(x, a - 1) - phi(x / primes[a], a - 1)
  ///
  template <int SIGN>
  int64_t phi(int64_t x, int64_t a)
  {
    if (x <= (int64_t) primes_[a])
      return SIGN;
    else if (is_phi_tiny(a))
      return phi_tiny(x, a) * SIGN;
    else if (is_pix(x, a))
      return (pi_[x] - a + 1) * SIGN;

    // Cache small phi(x, i) results with i <= min(a, max_a_)
    if (max_a_cached_ < min(a, max_a_) &&
        (uint64_t) x <= max_x_)
      init_cache(min(a, max_a_));

    if (is_cached(x, a))
      return phi_cache(x, a) * SIGN;

    int64_t sum;
    int64_t c = PhiTiny::max_a();
    int64_t larger_c = min(max_a_cached_, a);
    larger_c = std::max(c, larger_c);
    ASSERT(c < a);

    // Usually our algorithm starts at c because phi(x, c) can be
    // computed in O(1) time using phi_tiny(x, c). However, if a
    // larger value of c is cached, then it is better to start at that
    // value, since phi_cache(x, larger_c) also takes O(1) time.
    if (is_cached(x, larger_c))
      sum = phi_cache(x, (c = larger_c)) * SIGN;
    else
      sum = phi_tiny(x, c) * SIGN;

    int64_t sqrtx = isqrt(x);
    int64_t i;

    for (i = c + 1; i <= a; i++)
    {
      // phi(x / prime[i], i - 1) = 1 if x / prime[i] <= prime[i-1].
      // However we can do slightly better:
      // If prime[i] > sqrt(x) and prime[i-1] <= sqrt(x) then
      // phi(x / prime[i], i - 1) = 1 even if x / prime[i] > prime[i-1].
      // This works because in this case there is no other prime
      // inside the interval ]prime[i-1], x / prime[i]].
      if_unlikely(primes_[i] > sqrtx)
        goto phi_1;

      int64_t xp = fast_div(x, primes_[i]);

      // All remaining loop iterations can be computed
      // in O(1) time using the pi(x) lookup table.
      if (is_pix(xp, i - 1))
      {
        sum += (pi_[xp] - i + 2) * -SIGN;
        i += 1; break;
      }

      if (is_cached(xp, i - 1))
        sum += phi_cache(xp, i - 1) * -SIGN;
      else
        sum += phi<-SIGN>(xp, i - 1);
    }

    for (; i <= a; i++)
    {
      if_unlikely(primes_[i] > sqrtx)
        goto phi_1;

      // If a >= pi(sqrt(x)): phi(x, a) = pi(x) - a + 1
      // phi(xp, i - 1) = pi(xp) - (i - 1) + 1
      // phi(xp, i - 1) = pi(xp) - i + 2
      int64_t xp = fast_div(x, primes_[i]);
      ASSERT(is_pix(xp, i - 1));
      sum += (pi_[xp] - i + 2) * -SIGN;
    }

    phi_1:

    // For i in ]pi(sqrt(x)), a]:
    // phi(x / prime[i], i - 1) = 1
    sum += (a + 1 - i) * -SIGN;
    return sum;
  }

private:
  /// phi(x, a) counts the numbers <= x that are not divisible by any of
  /// the first a primes. If a >= pi(sqrt(x)) then phi(x, a) counts the
  /// number of primes <= x, minus the first a primes, plus the number 1.
  /// Hence if a >= pi(sqrt(x)): phi(x, a) = pi(x) - a + 1.
  ///
  bool is_pix(uint64_t x, uint64_t a) const
  {
    return x < pi_.size() &&
           x < isquare(primes_[a + 1]);
  }

  bool is_cached(uint64_t x, uint64_t a) const
  {
    return x <= max_x_ &&
           a <= max_a_cached_ &&
           a > PhiTiny::max_a();
  }

  int64_t phi_cache(uint64_t x, uint64_t a) const
  {
    ASSERT(is_cached(x, a));
    uint64_t count = sieve_[a][x / 240].count;
    uint64_t bits = sieve_[a][x / 240].bits;
    uint64_t bitmask = unset_larger_[x % 240];
    return count + popcnt64(bits & bitmask);
  }

  /// Cache phi(x, i) results with: x <= max_x && i <= a.
  /// Eratosthenes-like sieving algorithm that removes the first a primes
  /// and their multiples from the sieve array. Additionally this
  /// algorithm counts the numbers that are not divisible by any of the
  /// first a primes after sieving has completed. After sieving and
  /// counting has finished phi(x, a) results can be retrieved from the
  /// cache in O(1) using the phi_cache(x, a) method.
  ///
  void init_cache(uint64_t a)
  {
    ASSERT(a > PhiTiny::max_a());
    ASSERT(a <= max_a_);

    if (sieve_.empty())
    {
      ASSERT(max_a_ >= 3);
      sieve_.resize(max_a_ + 1);
      sieve_[3].resize(max_x_size_);
      std::fill(sieve_[3].begin(), sieve_[3].end(), sieve_t{0, ~0ull});
      max_a_cached_ = 3;
    }

    uint64_t i = max_a_cached_ + 1;
    ASSERT(a > max_a_cached_);
    max_a_cached_ = a;

    for (; i <= a; i++)
    {
      // Initalize phi(x, i) with phi(x, i - 1)
      if (i - 1 <= PhiTiny::max_a())
        sieve_[i] = std::move(sieve_[i - 1]);
      else
      {
        sieve_[i].resize(sieve_[i - 1].size());
        std::copy(sieve_[i - 1].begin(), sieve_[i - 1].end(), sieve_[i].begin());
      }

      // Remove prime[i] and its multiples.
      // Each bit in the sieve array corresponds to an integer that
      // is not divisible by 2, 3 and 5. The 8 bits of each byte
      // correspond to the offsets { 1, 7, 11, 13, 17, 19, 23, 29 }.
      uint64_t prime = primes_[i];
      if (prime <= max_x_)
        sieve_[i][prime / 240].bits &= unset_bit_[prime % 240];
      for (uint64_t n = prime * prime; n <= max_x_; n += prime * 2)
        sieve_[i][n / 240].bits &= unset_bit_[n % 240];

      if (i > PhiTiny::max_a())
      {
        // Fill an array with the cumulative 1 bit counts.
        // sieve[i][j] contains the count of numbers < j * 240 that
        // are not divisible by any of the first i primes.
        uint64_t count = 0;
        for (auto& sieve : sieve_[i])
        {
          sieve.count = (uint32_t) count;
          count += popcnt64(sieve.bits);
        }
      }
    }
  }

  uint64_t max_x_ = 0;
  uint64_t max_x_size_ = 0;
  uint64_t max_a_cached_ = 0;
  uint64_t max_a_ = 0;

  /// Packing sieve_t increases the cache's capacity by 25%
  /// which improves performance by up to 10%.
  #pragma pack(push, 1)
  struct sieve_t
  {
    uint32_t count;
    uint64_t bits;
  };
  #pragma pack(pop)

  /// sieve[a] contains only numbers that are not divisible
  /// by any of the the first a primes. sieve[a][i].count
  /// contains the count of numbers < i * 240 that are not
  /// divisible by any of the first a primes.
  Vector<Vector<sieve_t>> sieve_;
  const Primes& primes_;
  const PiTable& pi_;
};

/// Returns a vector with phi(x, i - 1) values such that
/// phi[i] = phi(x, i - 1) for 1 <= i <= a.
/// phi(x, a) counts the numbers <= x that are not
/// divisible by any of the first a primes.
///
template <typename Primes>
Vector<int64_t> phi_vector(int64_t x,
                           int64_t a,
                           const Primes& primes,
                           const PiTable& pi)
{
  int64_t size = a + 1;
  Vector<int64_t> phi(size);
  phi[0] = 0;

  if (size > 1)
  {
    if ((int64_t) primes[a] > x)
      a = pi[x];

    phi[1] = x;
    int64_t i = 2;
    int64_t sqrtx = isqrt(x);
    PhiCache<Primes> cache(x, a, primes, pi);

    // 2 <= i <= pi(sqrt(x)) + 1
    for (; i <= a && primes[i - 1] <= sqrtx; i++)
      phi[i] = phi[i - 1] + cache.template phi<-1>(x / primes[i - 1], i - 2);

    // pi(sqrt(x)) + 1 < i <= a
    for (; i <= a; i++)
      phi[i] = phi[i - 1] - (x > 0);

    // a < i < size
    for (; i < size; i++)
      phi[i] = x > 0;
  }

  return phi;
}

} // namespace

namespace primecount {

/// Returns a vector with phi(x, i - 1) values such that
/// phi[i] = phi(x, i - 1) for 1 <= i <= a.
/// phi(x, a) counts the numbers <= x that are not
/// divisible by any of the first a primes.
///
Vector<int64_t> phi_vector(int64_t x,
                           int64_t a,
                           const Vector<uint32_t>& primes,
                           const PiTable& pi)
{
  return ::phi_vector(x, a, primes, pi);
}

/// Returns a vector with phi(x, i - 1) values such that
/// phi[i] = phi(x, i - 1) for 1 <= i <= a.
/// phi(x, a) counts the numbers <= x that are not
/// divisible by any of the first a primes.
///
Vector<int64_t> phi_vector(int64_t x,
                           int64_t a,
                           const Vector<int64_t>& primes,
                           const PiTable& pi)
{
  return ::phi_vector(x, a, primes, pi);
}

} // namespace
