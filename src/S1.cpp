///
/// @file  S1.cpp
/// @brief Calculate the contribution of the ordinary leaves
///        in the Lagarias-Miller-Odlyzko and Deleglise-
///        Rivat prime counting algorithms.
///
/// Copyright (C) 2017 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <S1.hpp>
#include <primecount-internal.hpp>
#include <calculator.hpp>
#include <PhiTiny.hpp>
#include <generate.hpp>
#include <imath.hpp>
#include <json.hpp>

#include <stdint.h>
#include <vector>
#include <fstream>

#ifdef _OPENMP
  #include <omp.h>
#endif

using namespace std;
using namespace primecount;
using namespace nlohmann;

namespace {

template <typename T>
void backup(T x,
            int64_t y,
            int64_t c,
            T s1,
            double time)
{
  ifstream ifs("primecount.backup");
  json j;

  if (ifs.is_open())
  {
    ifs >> j;
    ifs.close();
  }

  j["S1"]["x"] = to_string(x);
  j["S1"]["y"] = y;
  j["S1"]["c"] = c;
  j["S1"]["s1"] = to_string(s1);
  j["S1"]["percent"] = 100.0;
  j["S1"]["seconds"] = get_wtime() - time;

  ofstream ofs("primecount.backup");
  ofs << setw(4) << j << endl;
}

template <typename T>
bool resume(T x,
            int64_t y,
            int64_t c,
            T& s1,
            double& time)
{
  ifstream ifs("primecount.backup");
  json j;

  if (ifs.is_open())
  {
    ifs >> j;
    ifs.close();
  }

  if (j.find("S1") != j.end() &&
      x == calculator::eval<T>(j["S1"]["x"]) &&
      y == j["S1"]["y"] &&
      c == j["S1"]["c"])
  {
    double percent = j["S1"]["percent"];
    double seconds = j["S1"]["seconds"];

    s1 = calculator::eval<T>(j["S1"]["s1"]);
    time = get_wtime() - seconds;

    if (is_print())
    {
      if (!print_variables())
        cout << endl;

      cout << "=== Resuming from primecount.backup ===" << endl;
      cout << "s1 = " << s1 << endl;
      cout << "Seconds: " << fixed << setprecision(3) << seconds << endl << endl;
      cout << "Status: " << fixed << setprecision(get_status_precision(x)) << percent << '%' << flush;
    }

    return true;
  }

  return false;
}

/// Recursively iterate over the square free numbers coprime
/// to the first b primes and calculate the sum of the
/// ordinary leaves. This algorithm is described in section
/// 2.2 of the paper: Douglas Staple, "The Combinatorial
/// Algorithm For Computing pi(x)", arXiv:1503.01839, 6
/// March 2015.
///
template <int MU, typename T, typename P>
T S1_thread(T x,
            int64_t y,
            int64_t b,
            int64_t c,
            T square_free,
            vector<P>& primes)
{
  T s1 = 0;

  for (b += 1; b < (int64_t) primes.size(); b++)
  {
    T next = square_free * primes[b];
    if (next > y) break;
    s1 += MU * phi_tiny(x / next, c);
    s1 += S1_thread<-MU>(x, y, b, c, next, primes);
  }

  return s1;
}

/// Parallel computation of the ordinary leaves.
/// Run time: O(y * log(log(y)))
/// Memory usage: O(y / log(y))
///
template <typename X, typename Y>
X S1_OpenMP(X x,
            Y y,
            int64_t c,
            int threads)
{
  auto primes = generate_primes<Y>(y);
  int64_t pi_y = primes.size();
  X s1 = phi_tiny(x, c);

  int64_t thread_threshold = ipow(10, 6);
  threads = ideal_num_threads(threads, y, thread_threshold);

  #pragma omp parallel for schedule(static, 1) num_threads(threads) reduction (+: s1)
  for (int64_t b = c + 1; b < pi_y; b++)
  {
    s1 -= phi_tiny(x / primes[b], c);
    s1 += S1_thread<1>(x, y, b, c, (X) primes[b], primes);
  }

  return s1;
}

} // namespace

namespace primecount {

int64_t S1(int64_t x,
           int64_t y,
           int64_t c,
           int threads)
{
  print("");
  print("=== S1(x, y) ===");
  print("Computation of the ordinary leaves");
  print(x, y, c, threads);

  double time = get_wtime();
  int64_t s1;

  if (!resume(x, y, c, s1, time))
  {
    s1 = S1_OpenMP(x, y, c, threads);
    backup(x, y, c, s1, time);
  }

  print("S1", s1, time);
  return s1;
}

#ifdef HAVE_INT128_T

int128_t S1(int128_t x,
            int64_t y,
            int64_t c,
            int threads)
{
  print("");
  print("=== S1(x, y) ===");
  print("Computation of the ordinary leaves");
  print(x, y, c, threads);

  double time = get_wtime();
  int128_t s1;

  if (!resume(x, y, c, s1, time))
  {
    // uses less memory
    if (y <= numeric_limits<uint32_t>::max())
      s1 = S1_OpenMP(x, (uint32_t) y, c, threads);
    else
      s1 = S1_OpenMP(x, y, c, threads);

    backup(x, y, c, s1, time);
  }

  print("S1", s1, time);
  return s1;
}

#endif

} // namespace
