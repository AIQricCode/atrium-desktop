#include "shell/tray_host.h"

#include <chrono>
#include <cstdio>

using namespace atrium;

namespace {

bool TestStartAllBackYieldPolicy()
{
    if (ShouldYieldToStartAllBack(false, false) || ShouldYieldToStartAllBack(false, true) || ShouldYieldToStartAllBack(true, true) ||
        !ShouldYieldToStartAllBack(true, false)) {
        std::fprintf(stderr, "FAIL: ShouldYieldToStartAllBack truth table mismatch\n");
        return false;
    }

    TrayHostYieldPolicy policy;
    if (policy.OnShellPid(100, true, true) || !policy.hasStartAllBackModule || policy.hasStartAllBack || policy.Yielded() ||
        policy.Reason() != TrayHostYieldReason::None) {
        std::fprintf(stderr, "FAIL: OnShellPid(100, true, true) should not yield when disabled for current user\n");
        return false;
    }

    if (policy.OnStartAllBackActive(false)) {
        std::fprintf(stderr, "FAIL: OnStartAllBackActive(false) should be a no-op when already inactive\n");
        return false;
    }

    if (policy.OnStartAllBackActive(true) || policy.shellPid != 100 || !policy.hasStartAllBack || !policy.Yielded() ||
        policy.Reason() != TrayHostYieldReason::StartAllBack) {
        std::fprintf(stderr, "FAIL: OnStartAllBackActive(true) should yield without changing shellPid\n");
        return false;
    }

    if (policy.OnStartAllBackActive(true)) {
        std::fprintf(stderr, "FAIL: OnStartAllBackActive(true) should be a no-op when already active\n");
        return false;
    }

    if (!policy.OnStartAllBackActive(false) || policy.shellPid != 100 || policy.hasStartAllBack || policy.Yielded() ||
        policy.Reason() != TrayHostYieldReason::None) {
        std::fprintf(stderr, "FAIL: OnStartAllBackActive(false) should clear StartAllBack yield without changing shellPid\n");
        return false;
    }

    if (policy.OnShellPid(200, true) || !policy.hasStartAllBackModule || !policy.hasStartAllBack || !policy.Yielded() ||
        policy.Reason() != TrayHostYieldReason::StartAllBack) {
        std::fprintf(stderr, "FAIL: 2-arg OnShellPid(200, true) should yield\n");
        return false;
    }

    if (!policy.OnShellPid(300, true, true) || !policy.hasStartAllBackModule || policy.hasStartAllBack || policy.Yielded() ||
        policy.Reason() != TrayHostYieldReason::None) {
        std::fprintf(stderr, "FAIL: OnShellPid(300, true, true) should resume when disabled for current user\n");
        return false;
    }
    return true;
}

bool TestRapidLossYieldPolicy()
{
    TrayHostYieldPolicy policy;
    bool newlyYielded = false;

    // 100ms bursts within kLossSampleMs (400ms) must not increment rapidLosses.
    if (!policy.OnExplorerFirst(1000, newlyYielded) || newlyYielded || policy.rapidLosses != 1 || policy.lastLossAt != 1000) {
        std::fprintf(stderr, "FAIL: first loss should record rapidLosses=1\n");
        return false;
    }
    for (ULONGLONG t : {1100ULL, 1200ULL, 1399ULL}) {
        if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded || policy.rapidLosses != 1 || policy.lastLossAt != 1000) {
            std::fprintf(stderr, "FAIL: sub-sample burst at %llu should be debounced\n", static_cast<unsigned long long>(t));
            return false;
        }
    }

    // Samples >= 400ms apart accumulate to kRapidLossLimit (6) -> 10s backoff (step 0 -> 1).
    ULONGLONG t = 1400;
    for (int i = 2; i < TrayHostYieldPolicy::kRapidLossLimit; ++i, t += 400) {
        if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded || policy.rapidLosses != i) {
            std::fprintf(stderr, "FAIL: expected rapidLosses=%d at %llu\n", i, static_cast<unsigned long long>(t));
            return false;
        }
    }
    if (policy.OnExplorerFirst(t, newlyYielded) || !newlyYielded || policy.Reason() != TrayHostYieldReason::RapidLoss || policy.retryAt != t + 10000 ||
        policy.backoffStep != 1) {
        std::fprintf(stderr, "FAIL: 6th sampled loss should trigger 10s RapidLoss backoff\n");
        return false;
    }
    if (policy.OnExplorerFirst(t + 9999, newlyYielded) || newlyYielded) {
        std::fprintf(stderr, "FAIL: should stay yielded during 10s backoff\n");
        return false;
    }

    // Cycle 2 after 10s expiry without OnFirst(): escalates to 30s backoff (step 1 -> 2).
    t += 10000;
    if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded || policy.rapidLosses != 1 || policy.backoffStep != 1) {
        std::fprintf(stderr, "FAIL: post-10s recovery should keep backoffStep=1\n");
        return false;
    }
    for (int i = 2; i < TrayHostYieldPolicy::kRapidLossLimit; ++i) {
        t += 400;
        if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded) {
            std::fprintf(stderr, "FAIL: cycle 2 sample %d failed\n", i);
            return false;
        }
    }
    t += 400;
    if (policy.OnExplorerFirst(t, newlyYielded) || !newlyYielded || policy.Reason() != TrayHostYieldReason::RapidLoss || policy.retryAt != t + 30000 ||
        policy.backoffStep != 2) {
        std::fprintf(stderr, "FAIL: cycle 2 should escalate to 30s backoff\n");
        return false;
    }

    // Cycle 3 after 30s expiry without OnFirst(): escalates to 60s backoff (capped at step 2).
    t += 30000;
    if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded || policy.rapidLosses != 1) {
        std::fprintf(stderr, "FAIL: post-30s recovery failed\n");
        return false;
    }
    for (int i = 2; i < TrayHostYieldPolicy::kRapidLossLimit; ++i) {
        t += 400;
        if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded) {
            std::fprintf(stderr, "FAIL: cycle 3 sample %d failed\n", i);
            return false;
        }
    }
    t += 400;
    if (policy.OnExplorerFirst(t, newlyYielded) || !newlyYielded || policy.Reason() != TrayHostYieldReason::RapidLoss ||
        policy.retryAt != t + TrayHostYieldPolicy::kRapidBackoffMs || policy.backoffStep != 2) {
        std::fprintf(stderr, "FAIL: cycle 3 should escalate to 60s backoff\n");
        return false;
    }

    // OnFirst() resets backoffStep and rapid-loss state back to 10s on future cycles.
    if (!policy.OnFirst() || policy.Yielded() || policy.backoffStep != 0 || policy.lastLossAt != 0 || policy.rapidLosses != 0) {
        std::fprintf(stderr, "FAIL: OnFirst() should reset RapidLoss backoff state\n");
        return false;
    }
    t += 70000;
    for (int i = 1; i < TrayHostYieldPolicy::kRapidLossLimit; ++i, t += 400) {
        if (!policy.OnExplorerFirst(t, newlyYielded) || newlyYielded) {
            std::fprintf(stderr, "FAIL: post-OnFirst sample %d failed\n", i);
            return false;
        }
    }
    if (policy.OnExplorerFirst(t, newlyYielded) || !newlyYielded || policy.retryAt != t + 10000 || policy.backoffStep != 1) {
        std::fprintf(stderr, "FAIL: after OnFirst() reset, backoff should start at 10s again\n");
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!TestStartAllBackYieldPolicy() || !TestRapidLossYieldPolicy())
        return 1;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        TrayHost host(nullptr, WM_APP, 0, 60000);
        if (host.Running()) {
            std::fprintf(stderr, "FAIL: host reported Running() before delayed start finished\n");
            return 1;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (elapsed > 5000) {
        std::fprintf(stderr, "FAIL: start/stop loop took too long (%lld ms)\n", static_cast<long long>(elapsed));
        return 1;
    }
    std::printf("ok: 20 delayed-start/timeout cycles exited in %lld ms\n", static_cast<long long>(elapsed));
    return 0;
}
