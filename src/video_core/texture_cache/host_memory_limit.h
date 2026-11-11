// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <limits>

#include "common/common_types.h"

namespace VideoCommon {

/// Commit the texture cache leaves to the rest of the process and the system.
constexpr u64 HostCommitReserve = u64{2} << 30;

/// The cache is not shrunk below this for host memory: a scene needs about this much, and under
/// it the collector only deletes and decodes the same textures again.
constexpr u64 MinHostCriticalMemory = u64{3} << 30;

/// Cards whose device memory figure (Device::GetDeviceLocalMemory: the budget at boot less 1/8)
/// is under this are 8 GB cards and smaller.
constexpr u64 SmallCardMemory = u64{7} << 30;

/// The most device memory the collector lets the texture cache fill before it collects in
/// earnest on a small card. At 5 GiB an RX 6650 XT (6.3 GiB figure, a 6.0-7.1 GB budget) ran
/// out entering a busy town in the green-hat kid adventure: device memory went 3.9 -> 5.6 GB in
/// 11 s, a 288 MiB shadow map ended up in host memory and the driver refused a staging buffer.
/// At 4 GiB (expected 3.25 GiB) the same town and the zones past it ran 26 minutes with device
/// memory at 3.4-4.1 GB, no memory pressure, the same fps and ~40% more ASTC decodes.
constexpr u64 SmallCardCriticalMemory = u64{4} << 30;

/// The same on larger cards, where 5/8 of the driver's budget is used when that is more: at
/// 5 GiB title 1's races recreate and upload ~8 MiB of render targets a frame (16 GB card: 53 ->
/// 59 fps at 10 GiB).
constexpr u64 LargeCardCriticalMemory = u64{5} << 30;

/// Cap on the collector's critical threshold for a card with `device_local` memory (Eden's
/// figure) and the driver's `budget` (0 when unknown).
constexpr u64 MaxCriticalMemory(u64 device_local, u64 budget) {
    if (device_local < SmallCardMemory) {
        return SmallCardCriticalMemory;
    }
    return (std::max)(LargeCardCriticalMemory, budget / 8 * 5);
}

/// The most device memory the texture cache may hold before it collects in earnest, as host
/// memory allows it. Windows charges device memory to the system's commit limit (RAM plus the
/// page files) like any other memory, since each allocation has a backing store there. A 16 GB
/// card's budget alone let the cache fill 9.7 GiB on a PC with 16 GiB of RAM and a 16 GiB page
/// file, where the guest's memory already takes 14 GiB of commit: the driver then refused a
/// 2.6 MiB image in every heap, host memory included, and the session ended (Ocarina of Time,
/// RX 7600 XT). `device_usage` is the device memory in use now and `commit_room` the commit the
/// system can still hand out: their sum is what the cache would hold with all of it.
constexpr u64 HostCriticalMemory(u64 device_usage, u64 commit_room) {
    const u64 limit = commit_room > (std::numeric_limits<u64>::max)() - device_usage
                          ? (std::numeric_limits<u64>::max)()
                          : device_usage + commit_room;
    return (std::max)(limit - (std::min)(limit, HostCommitReserve), MinHostCriticalMemory);
}

} // namespace VideoCommon
