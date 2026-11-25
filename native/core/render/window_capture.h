// Single-frame Windows.Graphics.Capture fallback for window snapshots.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <windows.graphics.directx.direct3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ABI::Windows::Graphics::Capture { // NOLINT(readability-identifier-naming): Windows SDK ABI namespace
struct IDirect3D11CaptureFrame;
}

namespace atrium {

struct WindowCaptureSignal;

struct CapturedWindowFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
};

// Single-threaded Windows.Graphics.Capture helper for GPU-composited windows when PrintWindow
// fails or returns blank. Lazily initializes D3D11 on first Capture() call.
class WindowFrameCapturer {
public:
    WindowFrameCapturer() = default;
    ~WindowFrameCapturer();
    WindowFrameCapturer(const WindowFrameCapturer&) = delete;
    WindowFrameCapturer& operator=(const WindowFrameCapturer&) = delete;

    bool Capture(HWND window, HANDLE stopEvent, DWORD timeoutMs, CapturedWindowFrame& out, HRESULT* hrOut = nullptr);

private:
    bool EnsureDevice(HRESULT* hrOut);
    void ResetDevice();
    bool CheckDeviceHr(HRESULT hr, HRESULT* hrOut);
    bool EnsureStaging(int width, int height, DXGI_FORMAT format, HRESULT* hrOut);
    bool ReadFrame(ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame* frame, HWND window, CapturedWindowFrame& out, HRESULT* hrOut);

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice> wgdDevice_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    int stagingW_ = 0;
    int stagingH_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    std::shared_ptr<WindowCaptureSignal> frameSignal_;
};

} // namespace atrium
