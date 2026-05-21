/*
  Ich brauche hier eine C-API um Intervalle zu verwalten

void mallocstat_hole_report(void* start, size_t length);
void mallocstat_hole_iterate(void (*callback)(void*, size_t));

Hinter dieser C-API soll eine C++ Implementierung stehen, die Löcher verwaltet und diese Löcher beim Insert verschmilzt wenn sie nebeneinander sind. Dazu soll std::map verwendet werden.

    Culprit: Custom Allocator, der nicht auf malloc dependet, sondern sich direkt mit mmap speicher holt und gesammelt zurück gibt. Nur Konstant große Speicherblöcke in einer Free-Liste verwalten.

Das Iterate ruft für jedes verschmolzene Loch einen callback auf.

*/

#include "helper.h"

#include <map>
#include <cstdint>
#include <mutex>
#include <cstddef>
#include <sys/mman.h>
#include <cassert>


#ifdef TEST
#include <vector>
#include <utility>
#define REPORT
#endif

namespace {
    // Basisklasse zur zentralen Verwaltung der mmap-Chunks und Typ-übergreifenden Free-Lists
    struct MmapAllocatorState {
        static constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KB Pages

        struct Chunk {
            Chunk* next;
        };

        struct FreeListRoot {
            void* head = nullptr;
            FreeListRoot* next = nullptr;
        };

        static std::mutex& alloc_mutex() {
            static std::mutex mtx;
            return mtx;
        }

        static Chunk*& chunk_list() {
            static Chunk* head = nullptr;
            return head;
        }

        static FreeListRoot*& registry() {
            static FreeListRoot* head = nullptr;
            return head;
        }

        static void reset_all() {
            std::lock_guard<std::mutex> lock(alloc_mutex());

            // 1. Alle per mmap allozierten Chunks freigeben
            Chunk* current_chunk = chunk_list();
            while (current_chunk) {
                Chunk* next = current_chunk->next;
                munmap(current_chunk, CHUNK_SIZE);
                current_chunk = next;
            }
            chunk_list() = nullptr;

            // 2. Alle typgebundenen Free-Lists (entstanden via std::allocator_traits::rebind) nullen
            FreeListRoot* current_root = registry();
            while (current_root) {
                current_root->head = nullptr;
                current_root = current_root->next;
            }
        }
    };

    // Custom Allocator, der ausschließlich mmap nutzt und Knoten in einer Free-List verwaltet.
    template <typename T>
    class MmapFreeListAllocator : public MmapAllocatorState {
    public:
        using value_type = T;

        MmapFreeListAllocator() = default;
        template <class U> constexpr MmapFreeListAllocator(const MmapFreeListAllocator<U>&) noexcept {}

        T* allocate(std::size_t n) {
            // Fallback für Arrays, obwohl std::map primär Einzelknoten (n=1) allokiert.
            if (n != 1) {
                void* ptr = mmap(nullptr, n * sizeof(T), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (ptr == MAP_FAILED) __builtin_trap();
                return static_cast<T*>(ptr);
            }

            std::lock_guard<std::mutex> lock(alloc_mutex());
            if (!free_list()) {
                refill();
            }
            Node* head = free_list();
            free_list() = head->next;
            return reinterpret_cast<T*>(head);
        }

        void deallocate(T* p, std::size_t n) noexcept {
            if (n != 1) {
                munmap(p, n * sizeof(T));
                return;
            }

            std::lock_guard<std::mutex> lock(alloc_mutex());
            Node* node = reinterpret_cast<Node*>(p);
            node->next = free_list();
            free_list() = node;
        }

        bool operator==(const MmapFreeListAllocator&) const { return true; }
        bool operator!=(const MmapFreeListAllocator&) const { return false; }

        static void clear_all() {
            reset_all();
        }

    private:
        union alignas(alignof(T)) Node {
            Node* next;
            char data[sizeof(T)];
        };

        static Node*& free_list() {
            static FreeListRoot root;
            static bool registered = false;
            // Die Initialisierung ist threadsicher, da Aufrufe stets unter alloc_mutex()-Sperre erfolgen.
            if (!registered) {
                root.next = registry();
                registry() = &root;
                registered = true;
            }
            return reinterpret_cast<Node*&>(root.head);
        }

        static void refill() {
            void* ptr = mmap(nullptr, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) __builtin_trap();

            Chunk* new_chunk = static_cast<Chunk*>(ptr);
            new_chunk->next = chunk_list();
            chunk_list() = new_chunk;

            // Sichern des exakten Alignments für den Node-Typ nach dem Chunk-Header
            size_t header_size = (sizeof(Chunk) + alignof(Node) - 1) & ~(alignof(Node) - 1);
            size_t objects = (CHUNK_SIZE - header_size) / sizeof(Node);

            char* node_start = static_cast<char*>(ptr) + header_size;
            Node* current = reinterpret_cast<Node*>(node_start);

            for (size_t i = 0; i < objects - 1; ++i) {
                current[i].next = &current[i + 1];
            }
            current[objects - 1].next = nullptr;
            free_list() = current;
        }
    };

    using MapAllocator = MmapFreeListAllocator<std::pair<const uintptr_t, size_t>>;
    std::map<uintptr_t, size_t, std::less<uintptr_t>, MapAllocator> holes;
    std::mutex map_mutex;

#ifdef REPORT
    void print_state(const char* operation, uintptr_t addr, size_t length) {
        print_str("[DEBUG] ");
        print_str(operation);
        print_str(" (addr=0x");
        print_hex(addr);
        print_str(", len=0x");
        print_hex(length);
        print_str(")\n");

        size_t count = 0;
        for (const auto& [k, v] : holes) {
            print_str("  Interval ");
            print_int(static_cast<uint32_t>(count++));
            print_str(": [0x");
            print_hex(k);
            print_str(", 0x");
            print_hex(k + v);
            print_str(") Size: 0x");
            print_hex(v);
            print_str("\n");
        }
        print_str("  Total intervals: ");
        print_int(static_cast<uint32_t>(holes.size()));
        print_str("\n\n");
    }
#endif
}

extern "C" {

void mallocstat_hole_report(uintptr_t addr, size_t length) {
    if (!addr || length == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(map_mutex);

    // Finde das erste Intervall, dessen Startadresse >= addr ist
    auto it = holes.lower_bound(addr);
    auto prev = (it != holes.begin()) ? std::prev(it) : holes.end();

    // Prüfe auf Adjazenz zu benachbarten Intervallen
    bool merges_left = (prev != holes.end()) && (prev->first + prev->second == addr);
    bool merges_right = (it != holes.end()) && (addr + length == it->first);

    if (merges_left && merges_right) {
        // Verschmelzung in beide Richtungen: Linkes Intervall erweitern, rechtes löschen
        prev->second += length + it->second;
        holes.erase(it);
    } else if (merges_left) {
        // Verschmelzung nur nach links
        prev->second += length;
    } else if (merges_right) {
        // Verschmelzung nur nach rechts: Knoten extrahieren (C++17), um Reallokation zu vermeiden
        auto node = holes.extract(it);
        node.key() = addr;
        node.mapped() += length;
        holes.insert(std::move(node));
    } else {
        // Keine Verschmelzung: Neues Intervall anlegen
        holes.emplace(addr, length);
    }

#ifdef REPORT
    print_state("Nach report", addr, length);
#endif
}

void mallocstat_hole_iterate(void (*callback)(uintptr_t, size_t, int)) {
    if (!callback) {
        return;
    }

    std::lock_guard<std::mutex> lock(map_mutex);
    for (const auto& [addr, length] : holes) {
      callback(addr, length, 1);
    }
}

void mallocstat_hole_reset() {
    std::lock_guard<std::mutex> lock(map_mutex);
    // 1. Destruktoren evaluieren und Knoten in die Free-List verschieben
    holes.clear();
    // 2. Physischen Speicher der Free-List deallozieren
    MapAllocator::clear_all();
}

} // extern "C"

#ifdef TEST
namespace {
    std::vector<std::pair<uintptr_t, size_t>> test_results;
}

extern "C" void test_callback(uintptr_t start, size_t length, int x) {
    (void) x;
    test_results.emplace_back(start, length);
}

int main() {
    print_str("[DEBUG] Test Start\n\n");

    // Isolierte Intervalle
    mallocstat_hole_report((0x1000), 0x100);
    mallocstat_hole_report((0x1300), 0x100);

    // Verschmelzung nach links
    mallocstat_hole_report((0x1100), 0x50);

    // Verschmelzung nach rechts
    mallocstat_hole_report((0x1250), 0xB0);

    // Bilaterale Verschmelzung
    mallocstat_hole_report((0x1150), 0x100);

    // Iteration und Zustandserfassung
    mallocstat_hole_iterate(test_callback);

    print_str("[DEBUG] Verifiziere ");
    print_int(static_cast<uint32_t>(test_results.size()));
    print_str(" resultierende Intervalle.\n");

    // Verifikation der Verschmelzungslogik
    assert(test_results.size() == 1);
    assert(test_results[0].first == 0x1000);
    assert(test_results[0].second == 0x400);

    // Test des globalen Resets
    print_str("\n[DEBUG] Führe globalen Reset aus...\n");
    mallocstat_hole_reset();

    test_results.clear();
    mallocstat_hole_iterate(test_callback);
    assert(test_results.size() == 0);
    print_str("[DEBUG] Reset verifiziert. Keine Intervalle verbleiben.\n");

    print_str("\n[DEBUG] Test erfolgreich beendet.\n");
    return 0;
}
#endif
