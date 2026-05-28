#include <chrono>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <set>
#include  <algorithm>
#include "mallocstat_intervals.cc"

// Signaturen der zu testenden C-API
extern "C" {
    void mallocstat_hole_report(uintptr_t addr, size_t length);
    void mallocstat_hole_iterate(void (*callback)(uintptr_t, size_t, int));
    void mallocstat_hole_reset();
}

namespace {
    struct BenchmarkMetrics {
        size_t total_operations;
        double total_time_ns;
        double avg_time_per_op_ns;
    };

    // Callback zur Verifikation des finalen Zustands
    size_t global_interval_count = 0;
    uintptr_t global_size = 0;

    void verification_callback(uintptr_t addr, size_t length, int active) {
        if (active) {
            global_interval_count++;
            global_size += length;
        }
    }
}

int main() {
    // Parameter Definitionen für den fixen Adressraum
    constexpr uintptr_t BASE_ADDRESS = 0x500000000000;
    constexpr size_t INTERVAL_SIZE   = 0x1000;  // 4 KB Chunks
    constexpr size_t NUM_INTERVALS   = 1000000;   // Anzahl der initialen Fragmente
    constexpr size_t MAX_INTERVALS   = 1500000;   // Anzahl der initialen Fragmente


    std::cout << "========================================================\n"
              << "BENCHMARK: Mallocstat Hole Management API\n"
              << "========================================================\n"
              << "Konfiguration:\n"
              << "  Basisadresse:      0x" << std::hex << BASE_ADDRESS << std::dec << "\n"
              << "  Intervallgroeße:   " << INTERVAL_SIZE << " Bytes\n"
              << "  Anzahl Intervalle: " << NUM_INTERVALS << "\n\n";

    std::set<uintptr_t> benchmark_addresses_set;
    srandom(23);
    while (benchmark_addresses_set.size() <= NUM_INTERVALS) {
        int i = random() % MAX_INTERVALS;
        benchmark_addresses_set.insert(BASE_ADDRESS + i * INTERVAL_SIZE);
    }
    std::vector<uintptr_t> addrs(benchmark_addresses_set.begin(),
                                 benchmark_addresses_set.end());
    // std::sort(addrs.begin(), addrs.end(), std::greater<int>());

    std::cout << "Starte Benchmark (" << NUM_INTERVALS << " Verschmelzungen)..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < NUM_INTERVALS; ++i) {
        mallocstat_hole_report(addrs[i], INTERVAL_SIZE);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    
    // ----------------------------------------------------------------
    // Auswertung & Verifikation
    // ----------------------------------------------------------------
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
    BenchmarkMetrics metrics{
        NUM_INTERVALS,
        static_cast<double>(duration),
        static_cast<double>(duration) / NUM_INTERVALS
    };

    // Validierung des Zustands über die Iterations-API
    mallocstat_hole_iterate(verification_callback);

    std::cout << "\n==================== ERGEBNISSE ========================\n";
    std::cout << "Zeitmessung:\n"
              << "  Gesamtzeit:        " << metrics.total_time_ns / 1e6 << " ms\n"
              << "  Operationen:       " << metrics.total_operations << "\n"
              << "  Mittelwert/Op:     " << metrics.avg_time_per_op_ns << " ns\n\n";

    std::cout << "Strukturelle Verifikation:\n"
              << "  Erwartete Intervalle nach Merge: 1\n"
              << "  Gefundene Intervalle:            " << global_interval_count << "\n";

    const size_t expected_total_size = NUM_INTERVALS * INTERVAL_SIZE;
    const size_t actual_total_size = global_size;

    if (actual_total_size == expected_total_size) {
        std::cout << "  Status:                          PASSED (Vollstaendige Verschmelzung)\n";
    } else {
        std::cout << "  Status:                          FAILED\n"
                  << "  Erwartete Groeße:                " << expected_total_size << " Bytes\n"
                  << "  Tatsaechliche Groeße:            " << actual_total_size << " Bytes\n";
    }
    std::cout << "========================================================\n";

    // Bereinigung des Allokators
    mallocstat_hole_reset();
    return 0;
}
