// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <new>

#include "common/logging.h"
#include "common/memory_detect.h"
#include "common/thread.h"
#endif

#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#ifdef HAS_NCE
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

DeviceMemory::DeviceMemory()
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize} {
#ifdef _WIN32
    zero_trim_thread = std::jthread{[this](std::stop_token stop) { TrimZeroPages(stop); }};
#endif
}

DeviceMemory::~DeviceMemory() = default;

#ifdef _WIN32
// Windows can't punch holes in the guest memory section, so pages holding only zeros (games clear
// their heaps while booting: Ocarina leaves ~7 GB) stay resident until they leave the working set.
// A pass comes every 5 s while they keep turning up, backing off to once every 5 minutes: after
// boot most passes trim 1-2 MiB for about a second of work. It waits out bulk mapping (loads,
// spawns), whose map calls stalled 10-14 ms while a pass ran.
void DeviceMemory::TrimZeroPages(std::stop_token stop) {
    using namespace std::chrono_literals;
    Common::SetCurrentThreadName("ZeroTrim");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);
    constexpr size_t Busy = size_t{64} << 20;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::chrono::seconds interval = 5s;
    constexpr u64 CommitLogStep = u64{256} << 20;
    u64 logged_committed = 0;
    while (true) {
        {
            std::unique_lock lock{mutex};
            wake.wait_for(lock, stop, interval, [] { return false; });
        }
        if (stop.stop_requested()) {
            return;
        }
        // Busy: over 32 MiB mapped, unmapped or protected in the last half second. At most 10
        // waits (5 s), then the pass runs anyway.
        for (int wait = 0; wait < 10; ++wait) {
            const u64 before = Common::HostMemory::MappedBytes();
            {
                std::unique_lock lock{mutex};
                wake.wait_for(lock, stop, 500ms, [] { return false; });
            }
            if (stop.stop_requested()) {
                return;
            }
            if (Common::HostMemory::MappedBytes() - before < (u64{32} << 20)) {
                break;
            }
        }
        // Read here, not at construction: the beside-application switches file may be applied
        // after the device memory is made.
        // EDEN_NX2_ZERO_TRIM=0: no passes (to measure what they cost guest memory maps).
        static const bool trim_enabled = [] {
            const char* const value = std::getenv("EDEN_NX2_ZERO_TRIM");
            return value == nullptr || value[0] != '0';
        }();
        // EDEN_NX2_STUTTER_TRACE: every pass, with when it starts. Guest memory maps took ~250 us
        // instead of ~10 us for seconds at Ocarina's zone changes, and a pass holds the working
        // set for about a second.
        static const bool trace = [] {
            const char* const value = std::getenv("EDEN_NX2_STUTTER_TRACE");
            return value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        if (!trim_enabled) {
            interval = std::chrono::seconds{300};
            continue;
        }
        if (trace) {
            LOG_INFO(HW_Memory, "Nx2ZeroTrim pass starts");
        }
        const auto start = std::chrono::steady_clock::now();
        std::pair<size_t, size_t> result;
        try {
            result = buffer.TrimZeroPages();
        } catch (const std::bad_alloc&) {
            // A pass needs 32 MiB and more of scratch. When Windows runs out of commit, the
            // exception leaving this thread ended the emulator (#1346); the pass is only a saving.
            interval = std::chrono::seconds{60};
            const Common::ProcessMemoryUsage usage = Common::GetProcessMemoryUsage();
            LOG_WARNING(HW_Memory,
                        "Skipped trimming all-zero guest pages: out of memory. Windows commit is "
                        "{} of {} MiB (this process {} MiB); the limit is RAM plus the page file",
                        usage.system_commit >> 20, usage.system_commit_limit >> 20,
                        usage.commit >> 20);
            continue;
        }
        const auto [resident, trimmed] = result;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
        if (trace) {
            LOG_INFO(HW_Memory, "Nx2ZeroTrim pass ends: {} ms, trimmed {} MiB of {} MiB resident",
                     ms, trimmed >> 20, resident >> 20);
        }
        if (const auto committed = buffer.CommittedBackingBytes();
            committed && *committed >= logged_committed + CommitLogStep) {
            logged_committed = *committed;
            LOG_INFO(HW_Memory, "Guest memory: {} MiB committed so far", *committed >> 20);
        }
        if (trimmed < Busy) {
            interval = (std::min)(interval * 2, std::chrono::seconds{300});
            continue;
        }
        interval = 5s;
        LOG_INFO(HW_Memory,
                 "Trimmed {} MiB of all-zero guest pages out of {} MiB resident in {} ms; the kernel "
                 "zero-filled {} MiB so far",
                 trimmed >> 20, resident >> 20, ms, Common::HostMemory::ZeroFilledBytes() >> 20);
    }
}
#endif

} // namespace Core
