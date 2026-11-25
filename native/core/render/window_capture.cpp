#include "render/window_capture.h"

#include <dwmapi.h>
#include <dxgi.h>
#include <roapi.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

namespace atrium {

struct WindowCaptureSignal {
    HANDLE event = nullptr;
    std::atomic<bool> active{false};
    WindowCaptureSignal() = default;
    WindowCaptureSignal(const WindowCaptureSignal&) = delete;
    WindowCaptureSignal& operator=(const WindowCaptureSignal&) = delete;
    ~WindowCaptureSignal()
    {
        if (event)
            CloseHandle(event);
    }
};

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
namespace wgc = ABI::Windows::Graphics::Capture;
namespace wgd = ABI::Windows::Graphics::DirectX;

bool SetHr(HRESULT hr, HRESULT* hrOut)
{
    if (hrOut)
        *hrOut = hr;
    return SUCCEEDED(hr);
}

void CloseClosable(IUnknown* unk)
{
    if (!unk)
        return;
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&closable))) && closable)
        (void)closable->Close();
}

struct CaptureCleanup {
    std::shared_ptr<WindowCaptureSignal> signal;
    ComPtr<wgc::IDirect3D11CaptureFramePool> pool;
    ComPtr<wgc::IGraphicsCaptureSession> session;
    EventRegistrationToken token{};
    bool hasToken = false;

    CaptureCleanup() = default;
    CaptureCleanup(const CaptureCleanup&) = delete;
    CaptureCleanup& operator=(const CaptureCleanup&) = delete;

    ~CaptureCleanup()
    {
        if (signal)
            signal->active.store(false, std::memory_order_release);
        if (pool && hasToken)
            (void)pool->remove_FrameArrived(token);
        CloseClosable(session.Get());
        CloseClosable(pool.Get());
    }
};

void ConfigureSession(wgc::IGraphicsCaptureSession* session)
{
    ComPtr<wgc::IGraphicsCaptureSession2> s2;
    if (SUCCEEDED(session->QueryInterface(IID_PPV_ARGS(&s2))) && s2)
        (void)s2->put_IsCursorCaptureEnabled(FALSE);
    ComPtr<wgc::IGraphicsCaptureSession3> s3;
    if (SUCCEEDED(session->QueryInterface(IID_PPV_ARGS(&s3))) && s3)
        (void)s3->put_IsBorderRequired(FALSE);
}

bool ComputeWindowCrop(HWND window, int maxW, int maxH, int& x, int& y, int& w, int& h)
{
    if (maxW <= 0 || maxH <= 0)
        return false;
    RECT win{}, frame{};
    if (!GetWindowRect(window, &win) || win.right <= win.left || win.bottom <= win.top ||
        FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) || frame.right <= frame.left || frame.bottom <= frame.top) {
        x = 0;
        y = 0;
        w = maxW;
        h = maxH;
        return true;
    }
    const int left = std::clamp<int>(frame.left - win.left, 0, maxW);
    const int top = std::clamp<int>(frame.top - win.top, 0, maxH);
    const int right = std::clamp<int>(frame.right - win.left, 0, maxW);
    const int bottom = std::clamp<int>(frame.bottom - win.top, 0, maxH);
    if (right <= left || bottom <= top)
        return false;
    x = left;
    y = top;
    w = right - left;
    h = bottom - top;
    return true;
}

} // namespace

WindowFrameCapturer::~WindowFrameCapturer()
{
    ResetDevice();
    if (frameSignal_)
        frameSignal_->active.store(false, std::memory_order_release);
    frameSignal_.reset();
}

void WindowFrameCapturer::ResetDevice()
{
    staging_.Reset();
    stagingW_ = 0;
    stagingH_ = 0;
    stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    wgdDevice_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
}

bool WindowFrameCapturer::CheckDeviceHr(HRESULT hr, HRESULT* hrOut)
{
    if (hrOut)
        *hrOut = hr;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == E_FAIL)
        ResetDevice();
    return SUCCEEDED(hr);
}

bool WindowFrameCapturer::EnsureDevice(HRESULT* hrOut)
{
    if (!frameSignal_) {
        auto sig = std::make_shared<WindowCaptureSignal>();
        sig->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!sig->event)
            return SetHr(HRESULT_FROM_WIN32(GetLastError()), hrOut);
        frameSignal_ = std::move(sig);
    }
    if (d3dDevice_ && d3dContext_ && wgdDevice_)
        return true;
    ResetDevice();
    HRESULT hr = E_FAIL;
    for (const D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice_, nullptr, &d3dContext_);
        if (SUCCEEDED(hr))
            break;
    }
    if (!CheckDeviceHr(hr, hrOut))
        return false;
    ComPtr<IDXGIDevice> dxgi;
    if (!CheckDeviceHr(d3dDevice_.As(&dxgi), hrOut)) {
        ResetDevice();
        return false;
    }
    ComPtr<IInspectable> insp;
    if (!CheckDeviceHr(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &insp), hrOut) || !CheckDeviceHr(insp.As(&wgdDevice_), hrOut)) {
        ResetDevice();
        return false;
    }
    return true;
}

bool WindowFrameCapturer::EnsureStaging(int width, int height, DXGI_FORMAT format, HRESULT* hrOut)
{
    if (staging_ && stagingW_ == width && stagingH_ == height && stagingFormat_ == format)
        return true;
    staging_.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!CheckDeviceHr(d3dDevice_->CreateTexture2D(&desc, nullptr, &staging_), hrOut) || !staging_)
        return false;
    stagingW_ = width;
    stagingH_ = height;
    stagingFormat_ = format;
    return true;
}

bool WindowFrameCapturer::ReadFrame(wgc::IDirect3D11CaptureFrame* frame, HWND window, CapturedWindowFrame& out, HRESULT* hrOut)
{
    struct FrameCloser {
        IUnknown* unk;
        explicit FrameCloser(IUnknown* u) : unk(u) {}
        FrameCloser(const FrameCloser&) = delete;
        FrameCloser& operator=(const FrameCloser&) = delete;
        ~FrameCloser() { CloseClosable(unk); }
    } closer(frame);

    ComPtr<wgd::Direct3D11::IDirect3DSurface> surface;
    if (!CheckDeviceHr(frame->get_Surface(&surface), hrOut) || !surface)
        return false;
    ABI::Windows::Graphics::SizeInt32 contentSize{};
    if (!CheckDeviceHr(frame->get_ContentSize(&contentSize), hrOut))
        return false;
    ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    if (!CheckDeviceHr(surface.As(&access), hrOut) || !access)
        return false;
    ComPtr<ID3D11Texture2D> srcTex;
    if (!CheckDeviceHr(access->GetInterface(IID_PPV_ARGS(&srcTex)), hrOut) || !srcTex)
        return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcTex->GetDesc(&srcDesc);
    const int maxW = std::min<int>(contentSize.Width, static_cast<int>(srcDesc.Width));
    const int maxH = std::min<int>(contentSize.Height, static_cast<int>(srcDesc.Height));
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    if (!ComputeWindowCrop(window, maxW, maxH, cropX, cropY, cropW, cropH))
        return SetHr(E_FAIL, hrOut);
    if (!EnsureStaging(cropW, cropH, srcDesc.Format, hrOut))
        return false;

    const D3D11_BOX box{UINT(cropX), UINT(cropY), 0, UINT(cropX + cropW), UINT(cropY + cropH), 1};
    d3dContext_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, srcTex.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!CheckDeviceHr(d3dContext_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), hrOut))
        return false;
    const size_t rowBytes = static_cast<size_t>(cropW) * 4;
    if (mapped.RowPitch < rowBytes) {
        d3dContext_->Unmap(staging_.Get(), 0);
        return SetHr(E_UNEXPECTED, hrOut);
    }
    out.width = cropW;
    out.height = cropH;
    out.bgra.resize(rowBytes * static_cast<size_t>(cropH));
    const auto* srcBytes = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < cropH; ++y)
        std::memcpy(out.bgra.data() + static_cast<size_t>(y) * rowBytes, srcBytes + static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
    d3dContext_->Unmap(staging_.Get(), 0);
    return true;
}

bool WindowFrameCapturer::Capture(HWND window, HANDLE stopEvent, DWORD timeoutMs, CapturedWindowFrame& out, HRESULT* hrOut)
{
    out = {};
    if (hrOut)
        *hrOut = S_OK;
    if (!IsWindow(window) || IsIconic(window) || !IsWindowVisible(window))
        return SetHr(E_INVALIDARG, hrOut);
    if (!EnsureDevice(hrOut))
        return false;

    ComPtr<IGraphicsCaptureItemInterop> interop;
    HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get(), IID_PPV_ARGS(&interop));
    if (!CheckDeviceHr(hr, hrOut))
        return false;
    ComPtr<wgc::IGraphicsCaptureItem> item;
    if (!CheckDeviceHr(interop->CreateForWindow(window, IID_PPV_ARGS(&item)), hrOut) || !item)
        return false;

    ABI::Windows::Graphics::SizeInt32 size{};
    if (!CheckDeviceHr(item->get_Size(&size), hrOut))
        return false;
    if (size.Width <= 0 || size.Height <= 0 || size.Width > 16384 || size.Height > 16384)
        return SetHr(E_INVALIDARG, hrOut);

    ComPtr<wgc::IDirect3D11CaptureFramePoolStatics2> poolStatics;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get(), IID_PPV_ARGS(&poolStatics));
    if (!CheckDeviceHr(hr, hrOut))
        return false;

    CaptureCleanup cleanup;
    hr = poolStatics->CreateFreeThreaded(wgdDevice_.Get(), wgd::DirectXPixelFormat_B8G8R8A8UIntNormalized, 1, size, &cleanup.pool);
    if (!CheckDeviceHr(hr, hrOut) || !cleanup.pool)
        return false;

    frameSignal_->active.store(true, std::memory_order_release);
    cleanup.signal = frameSignal_;
    auto handler = Callback<ABI::Windows::Foundation::ITypedEventHandler<wgc::Direct3D11CaptureFramePool*, IInspectable*>>(
        [sig = frameSignal_](wgc::IDirect3D11CaptureFramePool*, IInspectable*) -> HRESULT {
            if (sig->active.load(std::memory_order_acquire) && sig->event)
                SetEvent(sig->event);
            return S_OK;
        });
    if (!handler)
        return SetHr(E_OUTOFMEMORY, hrOut);
    if (!CheckDeviceHr(cleanup.pool->add_FrameArrived(handler.Get(), &cleanup.token), hrOut))
        return false;
    cleanup.hasToken = true;

    if (!CheckDeviceHr(cleanup.pool->CreateCaptureSession(item.Get(), &cleanup.session), hrOut) || !cleanup.session)
        return false;
    ConfigureSession(cleanup.session.Get());
    ResetEvent(frameSignal_->event);
    if (!CheckDeviceHr(cleanup.session->StartCapture(), hrOut))
        return false;

    HANDLE handles[2] = {frameSignal_->event, stopEvent};
    const DWORD wait = WaitForMultipleObjects(stopEvent ? 2 : 1, handles, FALSE, timeoutMs);
    if (wait != WAIT_OBJECT_0 || (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0))
        return SetHr(wait == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : E_ABORT, hrOut);

    ComPtr<wgc::IDirect3D11CaptureFrame> frame;
    hr = cleanup.pool->TryGetNextFrame(&frame);
    if (!CheckDeviceHr(hr, hrOut))
        return false;
    if (!frame)
        return SetHr(E_FAIL, hrOut);
    return ReadFrame(frame.Get(), window, out, hrOut);
}

} // namespace atrium
