#include "threadpool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

using namespace threadpool;
using Clock     = std::chrono::high_resolution_clock;
using Micros    = std::chrono::microseconds;
using Millis    = std::chrono::milliseconds;

static void print_separator() {
    std::cout << std::string(60, '-') << '\n';
}

// ===========================================================================
// Experiment 1 — Basic Correctness
//
// Goal: verify that tasks execute and results are written correctly.
// Method: N tasks computing sums of disjoint arrays; compare against a
//         single-threaded reference.
// ===========================================================================
void experiment1() {
    std::cout << "\n=== Experiment 1: Basic Correctness ===\n";

    constexpr int N        = 10; 
    constexpr int ARR_SIZE = 100;  

    std::vector<std::vector<int>> arrays(N, std::vector<int>(ARR_SIZE));
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < ARR_SIZE; ++j)
            arrays[i][j] = i * ARR_SIZE + j;

    std::vector<long long> expected(N);
    for (int i = 0; i < N; ++i)
        expected[i] = std::accumulate(arrays[i].begin(), arrays[i].end(), 0LL);

    bool all_passed = true;

    for (size_t tc : {size_t{1}, size_t{2}, size_t{4}, static_cast<size_t>(N)}) {
        std::vector<long long> results(N, 0LL);
        {
            ThreadPool pool(tc);
            for (int i = 0; i < N; ++i) {
                pool.AddTask([&arrays, &results, i]() {
                    results[i] = std::accumulate(
                        arrays[i].begin(), arrays[i].end(), 0LL);
                });
            }
            pool.WaitAll();
        }

        bool ok = (results == expected);
        all_passed &= ok;
        std::cout << "  threads=" << tc << ": " << (ok ? "PASS" : "FAIL") << '\n';
    }

    print_separator();
    std::cout << "  Result: " << (all_passed ? "PASS" : "FAIL") << '\n';
}

// ===========================================================================
// Experiment 2 — Wait and IsSubmitted Correctness
//
// Goal: Wait(id) returns only after the task completes;
//       IsSubmitted reflects the actual completion state.
// ===========================================================================
void experiment2() {
    std::cout << "\n=== Experiment 2: Wait and IsSubmitted Correctness ===\n";

    ThreadPool pool(2);

    auto id = pool.AddTask([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    });

    bool before_wait = pool.IsSubmitted(id);

    pool.Wait(id);

    bool after_wait = pool.IsSubmitted(id);

    bool ok1 = !before_wait;   // expected false
    bool ok2 =  after_wait;    // expected true

    std::cout << "  IsSubmitted right after AddTask : "
              << (before_wait ? "true" : "false")
              << " (expected false) => " << (ok1 ? "PASS" : "FAIL") << '\n';
    std::cout << "  IsSubmitted right after Wait    : "
              << (after_wait ? "true" : "false")
              << " (expected true)  => " << (ok2 ? "PASS" : "FAIL") << '\n';

    print_separator();
    std::cout << "  Result: " << ((ok1 && ok2) ? "PASS" : "FAIL") << '\n';
}

// ===========================================================================
// Experiment 3 — Throughput Benchmark
//
// Goal: measure pool throughput as a function of thread count.
// Method: 10 000 tasks (atomic counter increment).
// ===========================================================================
void experiment3() {
    std::cout << "\n=== Experiment 3: Throughput ===\n";

    constexpr int NUM_TASKS = 10'000;

    std::cout << "  tasks=" << NUM_TASKS << "\n\n";
    std::cout << "  threads  |  tasks/sec  |  counter\n";
    std::cout << "  ---------|-------------|----------\n";

    for (size_t tc : {size_t{1}, size_t{2}, size_t{4}, size_t{8}, size_t{16}}) {
        std::atomic<int> counter{0};

        auto start = Clock::now();
        {
            ThreadPool pool(tc);
            for (int i = 0; i < NUM_TASKS; ++i) {
                pool.AddTask([&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
            pool.WaitAll();
        }
        auto elapsed_us = std::chrono::duration_cast<Micros>(
                              Clock::now() - start).count();

        double throughput = (elapsed_us > 0)
            ? static_cast<double>(NUM_TASKS) / elapsed_us * 1e6
            : 0.0;

        std::cout << "  " << tc
                  << "        |  " << static_cast<long long>(throughput)
                  << "\t|  " << counter.load() << "/" << NUM_TASKS << '\n';
    }
    print_separator();
}

// ===========================================================================
// Experiment 4 — Pool vs Spawn-per-task
//
// Goal: quantify the advantage of a thread pool over naive thread creation.
// Method: N=1000 tasks x 10 runs; metrics — mean and p95 latency.
// ===========================================================================
void experiment4() {
    std::cout << "\n=== Experiment 4: Pool vs Spawn-per-task (N=1000) ===\n";

    constexpr int N    = 1000;
    constexpr int RUNS = 10;
    const size_t hw    = std::thread::hardware_concurrency();

    auto task = []() {
        volatile long x = 0;
        for (int i = 0; i < 2000; ++i) x += i;
    };

    std::vector<double> pool_ms;
    pool_ms.reserve(RUNS);
    for (int r = 0; r < RUNS; ++r) {
        auto start = Clock::now();
        {
            ThreadPool pool(hw);
            for (int i = 0; i < N; ++i) pool.AddTask(task);
            pool.WaitAll();
        }
        auto us = std::chrono::duration_cast<Micros>(Clock::now() - start).count();
        pool_ms.push_back(us / 1000.0);
    }

    std::vector<double> spawn_ms;
    spawn_ms.reserve(RUNS);
    for (int r = 0; r < RUNS; ++r) {
        auto start = Clock::now();
        {
            std::vector<std::thread> threads;
            threads.reserve(N);
            for (int i = 0; i < N; ++i) threads.emplace_back(task);
            for (auto& t : threads) t.join();
        }
        auto us = std::chrono::duration_cast<Micros>(Clock::now() - start).count();
        spawn_ms.push_back(us / 1000.0);
    }

    auto avg = [](const std::vector<double>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    auto p95 = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[static_cast<size_t>(v.size() * 0.95)];
    };

    double pool_avg  = avg(pool_ms);
    double spawn_avg = avg(spawn_ms);
    double speedup   = (pool_avg > 0.0) ? spawn_avg / pool_avg : 0.0;

    std::cout << "  hw_threads=" << hw << ",  N=" << N << ",  runs=" << RUNS << "\n\n";
    std::cout << "  Method          |  avg (ms)  |  p95 (ms)\n";
    std::cout << "  ----------------|------------|----------\n";
    std::cout << "  ThreadPool      |  " << pool_avg  << "\t|  " << p95(pool_ms)  << '\n';
    std::cout << "  Spawn-per-task  |  " << spawn_avg << "\t|  " << p95(spawn_ms) << '\n';
    std::cout << "\n  Speedup (spawn/pool): " << speedup << "x\n";

    print_separator();
    std::cout << "  Criterion (speedup >= 2): "
              << (speedup >= 2.0 ? "PASS" : "FAIL (hardware-specific)") << '\n';
}

// ===========================================================================
// Experiment 5 — Concurrent Producers (TSan scenario)
//
// Goal: verify absence of data races when AddTask is called concurrently.
// Method: multiple producer threads submit tasks simultaneously.
// Note: TSan (-fsanitize=thread) is available on Linux/macOS with GCC/Clang.
// ===========================================================================
void experiment5() {
    std::cout << "\n=== Experiment 5: Concurrent Producers (TSan scenario) ===\n";

    constexpr int PRODUCERS         = 4;
    constexpr int TASKS_PER_PRODUCER = 250;
    constexpr int TOTAL             = PRODUCERS * TASKS_PER_PRODUCER;

    std::atomic<int> counter{0};

    {
        ThreadPool pool(4);

        std::vector<std::thread> producers;
        producers.reserve(PRODUCERS);
        for (int p = 0; p < PRODUCERS; ++p) {
            producers.emplace_back([&pool, &counter]() {
                for (int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                    pool.AddTask([&counter]() {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }

        for (auto& t : producers) t.join();

        pool.WaitAll();
    }

    bool ok = (counter.load() == TOTAL);
    std::cout << "  Producers: " << PRODUCERS
              << ",  tasks per producer: " << TASKS_PER_PRODUCER << '\n';
    std::cout << "  counter=" << counter.load() << "/" << TOTAL
              << " => " << (ok ? "PASS" : "FAIL") << '\n';
    std::cout << "  (to detect data races compile with -fsanitize=thread"
                 " on Linux/macOS)\n";
    print_separator();
}

// ===========================================================================
// Experiment 6 — Destructor Correctness with Pending Tasks
//
// Goal: ensure the destructor neither loses tasks nor hangs.
// Method: 2 threads, 100 tasks; pool is destroyed without an explicit WaitAll.
// ===========================================================================
void experiment6() {
    std::cout << "\n=== Experiment 6: Destructor Correctness ===\n";

    constexpr int TASKS = 100;
    std::atomic<int> counter{0};

    {
        ThreadPool pool(2);
        for (int i = 0; i < TASKS; ++i) {
            pool.AddTask([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }

    bool ok = (counter.load() == TASKS);
    std::cout << "  counter=" << counter.load() << "/" << TASKS
              << " => " << (ok ? "PASS" : "FAIL") << '\n';
    print_separator();
}

// ===========================================================================
// Experiment 7 — CPU-bound Scaling (Monte Carlo pi)
//
// Goal: evaluate parallelisation efficiency on compute-intensive workloads.
// Method: 10^8 Monte Carlo iterations split into equal chunks across threads.
// ===========================================================================
void experiment7() {
    std::cout << "\n=== Experiment 7: CPU-bound Scaling (Monte Carlo pi) ===\n";

    constexpr long long TOTAL_ITERS = 100'000'000LL;

    auto monte_carlo_chunk = [](long long iters, unsigned seed) -> long long {
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        long long inside = 0;
        for (long long i = 0; i < iters; ++i) {
            double x = dist(rng);
            double y = dist(rng);
            if (x * x + y * y <= 1.0) ++inside;
        }
        return inside;
    };

    auto t_ref = Clock::now();
    long long inside_ref = monte_carlo_chunk(TOTAL_ITERS, 42u);
    double T1 = std::chrono::duration_cast<Millis>(Clock::now() - t_ref).count();
    double pi_ref = 4.0 * inside_ref / TOTAL_ITERS;

    std::cout << "  TOTAL_ITERS=" << TOTAL_ITERS << "\n\n";
    std::cout << "  threads  |  T (ms)  |  speedup  |  pi≈\n";
    std::cout << "  ---------|----------|-----------|--------\n";
    std::cout << "  1        |  " << T1 << "\t|  1.00     |  " << pi_ref << '\n';

    for (size_t tc : {size_t{2}, size_t{4}, size_t{8}}) {
        long long chunk = TOTAL_ITERS / static_cast<long long>(tc);
        std::vector<long long> partials(tc, 0LL);

        auto t_start = Clock::now();
        {
            ThreadPool pool(tc);
            for (size_t i = 0; i < tc; ++i) {
                pool.AddTask([&partials, &monte_carlo_chunk, i, chunk, tc]() {
                    long long iters = (i + 1 == tc)
                        ? TOTAL_ITERS - chunk * static_cast<long long>(tc - 1)
                        : chunk;
                    partials[i] = monte_carlo_chunk(iters, 42u + static_cast<unsigned>(i));
                });
            }
            pool.WaitAll();
        }
        double Tn = std::chrono::duration_cast<Millis>(Clock::now() - t_start).count();

        long long total_inside = std::accumulate(partials.begin(), partials.end(), 0LL);
        double pi_n    = 4.0 * total_inside / TOTAL_ITERS;
        double speedup = (Tn > 0.0) ? T1 / Tn : 0.0;

        std::cout << "  " << tc
                  << "        |  " << Tn
                  << "\t|  " << speedup
                  << "     |  " << pi_n << '\n';
    }

    print_separator();
    std::cout << "  Criterion: speedup >= 0.8 * threads_count on physical cores\n";
}

int main() {
    std::cout << "ThreadPool Experiments\n";
    std::cout << "Logical cores: " << std::thread::hardware_concurrency() << '\n';
    print_separator();

    experiment1();
    experiment2();
    experiment3();
    experiment4();
    experiment5();
    experiment6();
    experiment7();

    std::cout << "\nAll experiments completed.\n";
    return 0;
}
