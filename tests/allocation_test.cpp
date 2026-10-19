#include "patchy/bridge.hpp"
#include "patchy/format.hpp"
#include "patchy/channel_map.hpp"
#include <atomic>
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
namespace { std::atomic<unsigned long long> allocations{}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
int main() {
    patchy::Bridge bridge(44100, 48000, 2, 1323, 22050);
    const patchy::Format format{48000, 2, 32, 32, 8, 3, patchy::Encoding::float32};
    const patchy::ChannelMap mapping(format, format, std::array{2u, 0u});
    std::array<float, 882> input{};
    std::array<float, 960> output{}, decoded{};
    std::array<std::byte, 3840> encoded{};
    const auto before = allocations.load();
    for (int n = 0; n < 100; ++n) {
        bridge.push(input.data(), 441);
        bridge.pull(output.data(), 480);
        patchy::encode(output.data(), encoded.data(), 480, format);
        patchy::decode(encoded.data(), decoded.data(), 480, format);
        mapping.process(decoded.data(), output.data(), 480);
        if (n == 50) bridge.reset();
    }
    const auto delta = allocations.load() - before;
    std::cout << "Prepared processing loop ordinary C++ allocations: " << delta << '\n';
    return delta == 0 ? 0 : 1;
}
