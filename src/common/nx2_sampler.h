// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace Nx2Sampler {

// EDEN_NX2_SAMPLE=<thread names>: a sampling profiler for named host threads (Windows only).
// Names are comma separated; a name ending in '*' matches by prefix (CPUCore_*). From
// EDEN_NX2_SAMPLE_START seconds (default 60) for EDEN_NX2_SAMPLE_SECONDS (default 30), each
// thread is paused about 1000 times a second and its call stack recorded; the counts per
// function (where the thread is, and everything on its stack) go to EDEN_NX2_SAMPLE_OUT
// (default nx2_samples.txt).
//
// EDEN_NX2_SAMPLE_STALL=<seconds>: sample when the game stops presenting frames instead of at a
// fixed time, for stalls that come whenever the player gets there (a loading screen). Once 600
// frames have been presented, each time no frame comes for that long the threads (CPUCore_* when
// EDEN_NX2_SAMPLE is not set) are sampled for EDEN_NX2_SAMPLE_SECONDS (default 20); the reports
// go to <EDEN_NX2_SAMPLE_OUT>.stall<N>.txt, eight at most.
void StartFromEnvironment();

inline std::atomic<long long> g_last_frame_ns{};
inline std::atomic<unsigned long long> g_frames{};

inline long long NowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// A frame was presented (see EDEN_NX2_SAMPLE_STALL).
inline void NoteFrame() noexcept {
    g_last_frame_ns.store(NowNs(), std::memory_order_relaxed);
    g_frames.fetch_add(1, std::memory_order_relaxed);
}

/// Samples the named threads for `seconds` now (blocking), writes the report to `out` (and
/// the folded stacks to `out`.folded) and returns the report.
std::string Sample(const char* names, double seconds, const std::string& out);

} // namespace Nx2Sampler
