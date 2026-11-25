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

} // namespace

int main()
{
    if (!TestStartAllBackYieldPolicy())
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
