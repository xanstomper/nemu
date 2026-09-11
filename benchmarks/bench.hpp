#pragma once

#include "core/types.hpp"
#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <algorithm>

namespace nemu::bench {

class BenchmarkTimer {
public:
    BenchmarkTimer(std::string name, u64 iterations)
        : name_(std::move(name)), iterations_(iterations) {
        start_time_ = std::chrono::steady_clock::now();
    }

    ~BenchmarkTimer() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count();
        double duration_ms = static_cast<double>(duration_ns) / 1'000'000.0;
        double ns_per_op = static_cast<double>(duration_ns) / static_cast<double>(iterations_);
        double ops_per_sec = (static_cast<double>(iterations_) / static_cast<double>(duration_ns)) * 1'000'000'000.0;

        std::cout << "---------------------------------------------------------" << std::endl;
        std::cout << "BENCHMARK: " << name_ << std::endl;
        std::cout << "  Iterations:   " << iterations_ << std::endl;
        std::cout << "  Total Time:   " << std::fixed << std::setprecision(3) << duration_ms << " ms" << std::endl;
        std::cout << "  Latency:      " << std::fixed << std::setprecision(2) << ns_per_op << " ns/op" << std::endl;
        std::cout << "  Throughput:   " << std::fixed << std::setprecision(2) << ops_per_sec / 1'000'000.0 << " M ops/sec" << std::endl;
        std::cout << "---------------------------------------------------------" << std::endl;
    }

    [[nodiscard]] double ElapsedMilliseconds() const {
        auto cur = std::chrono::steady_clock::now();
        return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(cur - start_time_).count()) / 1'000'000.0;
    }

private:
    std::string name_;
    u64 iterations_{0};
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace nemu::bench
