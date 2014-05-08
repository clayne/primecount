///
/// @file  Pk.cpp
/// @brief Partial sieve functions.
///
/// Copyright (C) 2014 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include "pi_bsearch.hpp"
#include "pmath.hpp"

#include <primecount.hpp>
#include <primesieve.hpp>
#include <algorithm>
#include <stdint.h>
#include <vector>

#ifdef _OPENMP
  #include <omp.h>
  #include "get_omp_threads.hpp"
#endif

using namespace std;

namespace primecount {

/// 2nd partial sieve function.
/// P2(x, a) counts the numbers <= x that have exactly 2 prime
/// factors each exceeding the a-th prime.
/// Space complexity: O((x / primes[a])^(1/2)).
///
int64_t P2(int64_t x, int64_t a)
{
  vector<int32_t> primes;
  primes.push_back(0);
  primesieve::generate_n_primes(a, &primes);
  int64_t limit = (a > 0) ? x / primes[a] : x;
  int64_t sqrt_limit = isqrt(limit);
  int64_t sum = 0; // \sum_{i=a+1}^{b} pi(x / primes[i]) - (i - 1)
  int64_t pix = 1; // start counting primes at 3
  int64_t sqrtx = isqrt(x);
  int64_t b = pi_legendre(sqrtx, 1);
  int64_t segment_size = next_power_of_2(sqrt_limit);

  if (b <= a)
    return sum;

  primesieve::iterator iter(sqrtx + 1);
  int64_t stop = x / iter.previous_prime();
  vector<char> sieve(segment_size);
  vector<int64_t> next(primes.size());

  for (size_t i = 1; i < primes.size(); i++)
    next[i] = (isquare(primes[i]) - /* low */ 3) % segment_size;

  // segmented sieve of Eratosthenes
  for (int64_t low = 3; low <= limit; low += segment_size)
  {
    fill(sieve.begin(), sieve.end(), 1);

    // current segment = interval [low, high]
    int64_t high = min(low + segment_size - 1, limit);
    int64_t sqrt_high = isqrt(high);

    for (size_t i = 2; i < primes.size(); i++)
    {
      if (primes[i] > sqrt_high)
        break;
      int64_t k = next[i];
      for (int64_t p2 = primes[i] * 2; k < segment_size; k += p2)
        sieve[k] = 0;
      next[i] = k - segment_size;
    }

    int64_t j = ~low % 2;
    for (; stop <= high; stop = x / iter.previous_prime())
    {
      for (; j <= stop - low; j += 2)
        pix += sieve[j];
      // sum += pi(x / primes[b]) - (b - 1)
      sum += pix - (b - 1);
      if (--b <= a) return sum;
    }

    for (; j <= high - low; j += 2)
      pix += sieve[j];
  }

  return sum;
}

/// 2nd partial sieve function.
/// P2_lehmer(x, a) counts the numbers <= x that have exactly 2 prime
/// factors each exceeding the a-th prime. This implementation is
/// optimized for small values of a < pi(x^(1/3)) which requires
/// sieving up to a large limit (x / primes[a]). Sieving is done in
/// parallel using primesieve (segmented sieve of Eratosthenes).
/// Space complexity: O(pi(sqrt(x))).
///
int64_t P2_lehmer(int64_t x, int64_t a, int threads)
{
  vector<int32_t> primes;
  vector<int64_t> counts;
  primes.push_back(0);
  primesieve::generate_primes(isqrt(x), &primes);
  counts.resize(primes.size());

  int64_t b = pi_bsearch(primes, isqrt(x));
  int64_t sum = 0;
  int64_t pix = 0;

#ifdef _OPENMP
  #pragma omp parallel for num_threads(get_omp_threads(threads)) \
      schedule(dynamic)
#endif
  for (int64_t i = b; i > a; i--)
  {
    int64_t prev = (i == b) ? 0 : x / primes[i + 1] + 1;
    int64_t xi = x / primes[i];
    counts[i] = primesieve::count_primes(prev, xi);
  }

  for (int64_t i = b; i > a; i--)
  {
    pix += counts[i];
    sum += pix - (i - 1);
  }

  return sum;
}

/// 3rd partial sieve function.
/// P3(x, a) counts the numbers <= x that have exactly 3 prime
/// factors each exceeding the a-th prime.
/// Space complexity: O(pi(sqrt(x))).
///
int64_t P3(int64_t x, int64_t a, int threads)
{
  vector<int32_t> primes;
  primes.push_back(0);
  primesieve::generate_primes(isqrt(x), &primes);

  int64_t y = iroot<3>(x);
  int64_t pi_y = pi_bsearch(primes, y);
  int64_t sum = 0;

#ifdef _OPENMP
  #pragma omp parallel for num_threads(get_omp_threads(threads)) \
      schedule(dynamic) reduction(+: sum)
#endif
  for (int64_t i = a + 1; i <= pi_y; i++)
  {
    int64_t xi = x / primes[i];
    int64_t bi = pi_bsearch(primes, isqrt(xi));

    for (int64_t j = i; j <= bi; j++)
      sum += pi_bsearch(primes, xi / primes[j]) - (j - 1);
  }

  return sum;
}

} // namespace primecount
