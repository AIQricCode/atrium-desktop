// The backdrop of the glass: one monitor, captured without Atrium's own
// windows. Two ways:
//  - DXGI Desktop Duplication (preferred): Atrium's windows are excluded from
//    it alone (WCA_EXCLUDED_FROM_DDA), so screenshots and recordings that use
//    Windows.Graphics.Capture or GDI still see them;
//  - Windows.Graphics.Capture (fallback): Atrium's windows must then be kept
//    out of every capture (WDA_EXCLUDEFROMCAPTURE), or the glass would sample
//    itself.
// The render thread polls Desktop Duplication; other sources signal frame arrival.
#pragma once

#include "gpu.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <memory>

namespace atrium {

class BackdropSource {
public:
    BackdropSource() = default;
    BackdropSource(const BackdropSource&) = delete;
    BackdropSource& operator=(const BackdropSource&) = delete;
    virtual ~BackdropSource() = default;
    // The newest frame since the last call, or null; valid until Release().
    virtual ID3D11Texture2D* Acquire() = 0;
    virtual void Release() = 0;
    virtual bool RequiresPolling() const { return false; }
    // The source stopped (the display mode changed, the secure desktop came): make a new one.
    virtual bool Lost() const { return false; }
    // A source that sends a frame only when its picture changes (the wallpaper): its last frame
    // again, for a glass that missed it or lost its copy; null for sources that keep sending.
    virtual ID3D11Texture2D* Again() { return nullptr; }
    virtual bool CanReplay() const { return false; }
    // Whether Atrium's windows may show in other captures while this one runs.
    virtual bool KeepsOwnWindowsOut() const = 0;
    virtual void SetPaused(bool) {}
};

class MonitorCapture : public BackdropSource {
public:
    MonitorCapture(Gpu gpu, HMONITOR monitor, HANDLE frameEvent);
    ~MonitorCapture() override;
    MonitorCapture(const MonitorCapture&) = delete;
    MonitorCapture& operator=(const MonitorCapture&) = delete;

    ID3D11Texture2D* Acquire() override;
    void Release() override;
    bool Lost() const override { return lost_; }
    bool KeepsOwnWindowsOut() const override { return false; }
    void SetPaused(bool paused) override;

private:
    bool Start(bool throwOnError);
    void Stop();

    Gpu gpu_;
    HMONITOR monitor_ = nullptr;
    HANDLE frameEvent_ = nullptr;
    bool paused_ = false;
    bool lost_ = false;
    std::shared_ptr<std::atomic<bool>> active_;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::SizeInt32 size_{};
    winrt::event_token token_{};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame_{nullptr};
    winrt::com_ptr<ID3D11Texture2D> texture_;
};

class DuplicationCapture : public BackdropSource {
    struct Tag {
        explicit Tag() = default; // not `{}`: only Create can make one
    };

public:
    // Null when the monitor cannot be duplicated; `why` says why (temporary:
    // E_ACCESSDENIED while the secure desktop shows).
    // elsewhere: DXGI lists the monitor under another GPU (a hybrid laptop
    // that placed this process on the discrete one) - see PreferPowerSavingGpu.
    static std::unique_ptr<DuplicationCapture> Create(const Gpu& gpu, HMONITOR monitor, HRESULT& why, bool& elsewhere);
    explicit DuplicationCapture(Tag) {}
    ~DuplicationCapture() override;
    DuplicationCapture(const DuplicationCapture&) = delete;
    DuplicationCapture& operator=(const DuplicationCapture&) = delete;

    ID3D11Texture2D* Acquire() override;
    void Release() override;
    bool RequiresPolling() const override { return true; }
    bool Lost() const override { return lost_; }
    bool KeepsOwnWindowsOut() const override { return true; }
    static bool Temporary(HRESULT why);

private:
    winrt::com_ptr<IDXGIOutputDuplication> dup_;
    winrt::com_ptr<ID3D11Texture2D> texture_;
    bool acquired_ = false;
    bool lost_ = false;
};

// On a hybrid laptop DXGI lists the screens only under the GPU Windows placed
// the process on. Asks Windows (its per-app graphics preference, as in
// Settings > System > Display > Graphics) to run Atrium on the power-saving
// GPU, which drives the panel; from the next start. Leaves a choice the user
// made alone. True if the preference was written.
bool PreferPowerSavingGpu();

// Atrium's windows: always out of Desktop Duplication (Atrium's own
// backdrop); out of every other capture too unless `visible`. In remote-control
// mode out of none: the glass shows the wallpaper and nothing reads the screen.
void SetCaptureExclusion(HWND hwnd, bool visible);

// Remote-control mode (setting remoteVisible): remote-control apps capture the
// screen with Desktop Duplication, which must then see Atrium too.
void SetRemoteVisible(bool on);
bool RemoteVisible();

} // namespace atrium
