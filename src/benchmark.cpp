#include "hyperion.hpp"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <iomanip>

using Clock = std::chrono::high_resolution_clock;

void bench_hyperion(int count) {
    ArenaError ae;
    // 256MB Arena, 2x Slots to minimize load factor effects for fair comparison.
    auto db = Hyperion::create(256ULL * 1024 * 1024, count * 2, ae);
    if (ae != ArenaError::None) { std::cerr << "Hyperion alloc failed\n"; exit(1); }

    std::vector<std::string> keys;
    keys.reserve(count);
    for(int i=0; i<count; ++i) keys.push_back("key:" + std::to_string(i));
    std::string val = "payload:64bytes_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

    // INSERT BENCHMARK
    auto start = Clock::now();
    for(const auto& k : keys) {
        db.put(k, val);
    }
    auto end = Clock::now();
    double dur = std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << "[Hyperion] Insert: " << std::fixed << std::setprecision(2) << (dur / count) << " ns/op\n";

    // READ BENCHMARK
    std::string out;
    start = Clock::now();
    for(const auto& k : keys) {
        db.get(k, out);
    }
    end = Clock::now();
    dur = std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << "[Hyperion] Read  : " << std::fixed << std::setprecision(2) << (dur / count) << " ns/op\n";
}

void bench_std(int count) {
    std::unordered_map<std::string, std::string> m;
    m.reserve(count); 

    std::vector<std::string> keys;
    keys.reserve(count);
    for(int i=0; i<count; ++i) keys.push_back("key:" + std::to_string(i));
    std::string val = "payload:64bytes_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

    // INSERT BENCHMARK
    auto start = Clock::now();
    for(const auto& k : keys) {
        m[k] = val;
    }
    auto end = Clock::now();
    double dur = std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << "[StdMap  ] Insert: " << std::fixed << std::setprecision(2) << (dur / count) << " ns/op\n";

    // READ BENCHMARK
    std::string out;
    start = Clock::now();
    for(const auto& k : keys) {
        out = m[k];
    }
    end = Clock::now();
    dur = std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << "[StdMap  ] Read  : " << std::fixed << std::setprecision(2) << (dur / count) << " ns/op\n";
}

int main() {
    const int N = 1000000; 
    std::cout << "Benchmarking " << N << " operations (Payload: 64B)...\n";
    bench_hyperion(N);
    bench_std(N);
    return 0;
}