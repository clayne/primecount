///
/// @file  pi_meissel.cpp
///
/// Copyright (C) 2014 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include "pmath.hpp"
#include "Pk.hpp"

#include <primecount.hpp>
#include <stdint.h>

namespace primecount {

/// Calculate the number of primes below x using Meissel's formula.
/// Run time: O(x/(log x)^3) operations, O(x^0.5 / log x) space.
///
int64_t pi_meissel(int64_t x, int threads)
{
  if (x < 2)
    return 0;

  int64_t x13 = iroot<3>(x);
  int64_t a = pi_legendre(x13, /* threads = */ 1);
  int64_t sum = 0;

  sum += phi(x, a, threads) + a - 1;
  sum -= P2 (x, a, x13);

  return sum;
}

} // namespace primecount
