///
/// @file   alpha_lmo.cpp
/// @brief  Test the alpha tuning factor with the LMO algorithm.
///         y = alpha * x^(1/3)
///         By computing pi(x) using different alpha tuning
///         factors we can make sure that all array sizes
///         (and other bounds) are accurate.
///
/// Copyright (C) 2023 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <primecount.hpp>
#include <primecount-internal.hpp>
#include <imath.hpp>

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include <random>

using namespace primecount;

void check(bool OK)
{
  std::cout << "   " << (OK ? "OK" : "ERROR") << "\n";
  if (!OK)
    std::exit(1);
}

int main()
{
  int threads = get_num_threads();

  // Test small values of x
  {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> dist(100, 1000);

    for (int i = 0; i < 100; i++)
    {
      int64_t x = dist(gen);
      int64_t res1 = pi_cache(x);

      for (double alpha = 1; alpha <= iroot<6>(x); alpha++)
      {
        set_alpha(alpha);
        int64_t res2 = pi_lmo_parallel(x, threads);

        std::cout << "pi_lmo_parallel(" << x << ") = " << res2;
        check(res1 == res2);
      }
    }
  }

  // Test medium values of x
  {
    int64_t min = (int64_t) 1e7;
    int64_t max = min * 2;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> dist(min, max);

    for (int i = 0; i < 50; i++)
    {
      int64_t x = dist(gen);
      int64_t res1 = pi_meissel(x, threads);

      for (double alpha = 1; alpha <= iroot<6>(x); alpha++)
      {
        set_alpha(alpha);
        int64_t res2 = pi_lmo_parallel(x, threads);

        std::cout << "pi_lmo_parallel(" << x << ") = " << res2;
        check(res1 == res2);
      }
    }
  }

  std::cout << std::endl;
  std::cout << "All tests passed successfully!" << std::endl;

  return 0;
}
