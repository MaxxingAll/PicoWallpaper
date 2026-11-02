#include "decoder/mf_video_format.h"
#include "platform/win32/d3d11_presenter.h"
#include "platform/win32/startup.h"

#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace miniwp;
using Microsoft::WRL::ComPtr;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ComPtr<IMFMediaType> MediaType(UINT32 width, UINT32 height) {
    ComPtr<IMFMediaType> type;
    Check(SUCCEEDED(MFCreateMediaType(&type)), "create media type");
    Check(SUCCEEDED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height)),
          "set frame size");
    return type;
}

void Aperture(IMFMediaType* type, REFGUID key, LONG width, LONG height,
              short left = 0, short top = 0, WORD fraction = 0) {
    MFVideoArea area = {};
    area.OffsetX.value = left;
    area.OffsetY.value = top;
    area.OffsetX.fract = fraction;
    area.OffsetY.fract = fraction;
    area.Area = {width, height};
    Check(SUCCEEDED(type->SetBlob(key, reinterpret_cast<const UINT8*>(&area), sizeof(area))),
          "set aperture");
}

bool RectEquals(const RECT& rect, LONG left, LONG top, LONG right, LONG bottom) {
    return rect.left == left && rect.top == top && rect.right == right && rect.bottom == bottom;
}

void VisibleGeometry() {
    auto type = MediaType(1920, 1080);
    VideoFormat format;
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read visible frame size");
    RECT source = {}, destination = {};
    for (int mode = 0; mode < 3; ++mode) {
        Check(CalculateVideoRectangles(format, 1920, 1088, 1920, 1080, mode,
                                        source, destination), "padded texture geometry");
        Check(RectEquals(source, 0, 0, 1920, 1080), "exclude eight padded scanlines");
        Check(RectEquals(destination, 0, 0, 1920, 1080), "preserve visible aspect ratio");
    }

    type = MediaType(1920, 1088);
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 1888, 1060, 16, 8);
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read offset aperture");
    Check(CalculateVideoRectangles(format, 1920, 1088, 1000, 1000, 2,
                                    source, destination), "stretch offset aperture");
    Check(RectEquals(source, 16, 8, 1904, 1068), "keep source within the offset aperture");
    Check(CalculateVideoRectangles(format, 1920, 1088, 1000, 1000, 0,
                                    source, destination), "cover offset aperture");
    Check(RectEquals(source, 430, 8, 1490, 1068), "center the cover crop inside aperture");
    Check(!CalculateVideoRectangles(format, 1000, 1088, 1000, 1000, 0,
                                     source, destination), "reject undersized texture");
    Check(!CalculateVideoRectangles(format, 1920, 1088, 0, 1000, 0,
                                     source, destination), "reject empty output");

    type = MediaType(720, 480);
    Aperture(type.Get(), MF_MT_GEOMETRIC_APERTURE, 704, 480, 8);
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)) && format.display_left == 8,
          "geometric aperture fallback");
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 640, 480, 40);
    Aperture(type.Get(), MF_MT_PAN_SCAN_APERTURE, 600, 400, 60, 40);
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)) && format.display_width == 640,
          "ignore disabled pan-scan and prefer minimum aperture");
    Check(SUCCEEDED(type->SetUINT32(MF_MT_PAN_SCAN_ENABLED, TRUE)), "enable pan-scan");
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)) && format.display_width == 600,
          "enabled pan-scan takes precedence");
    Check(SUCCEEDED(type->DeleteItem(MF_MT_PAN_SCAN_APERTURE)), "remove pan-scan aperture");
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)) && format.display_width == 640,
          "missing pan-scan falls back to minimum aperture");

    type = MediaType(720, 480);
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 704, 470, 8, 4, 32768);
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read fractional aperture");
    Check(format.display_left == 9 && format.display_top == 5 &&
          format.display_width == 703 && format.display_height == 469,
          "fractional aperture rounds inward");

    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 720, 480, 1);
    const auto full_frame = [&] {
        Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "ignore unusable aperture");
        Check(format.display_left == 0 && format.display_top == 0 &&
              format.display_width == 720 && format.display_height == 480,
              "unusable aperture falls back to full frame");
    };
    full_frame();
    const UINT8 truncated[] = {0, 0};
    Check(SUCCEEDED(type->SetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, truncated, sizeof(truncated))),
          "set truncated aperture");
    full_frame();
    const UINT8 oversized[sizeof(MFVideoArea) + 1] = {};
    Check(SUCCEEDED(type->SetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, oversized, sizeof(oversized))),
          "set oversized aperture");
    full_frame();
    Check(SUCCEEDED(type->SetUINT32(MF_MT_MINIMUM_DISPLAY_APERTURE, 1)),
          "set wrongly typed aperture");
    full_frame();
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 704, 480, -1);
    full_frame();
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 0, 480);
    full_frame();
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 1, 1, 0, 0, 32768);
    full_frame();
    Check(SUCCEEDED(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255)),
          "set color metadata alongside invalid aperture");
    Check(SUCCEEDED(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 10, 11)),
          "set aspect ratio alongside invalid aperture");
    full_frame();
    Check(format.range == VideoRange::Full && format.pixel_aspect_numerator == 10 &&
          format.pixel_aspect_denominator == 11, "retain other metadata after aperture fallback");
    type = MediaType(0, 480);
    Check(FAILED(ReadMFVideoFormat(type.Get(), format)), "reject zero frame width");
    type = MediaType(0xffffffffu, 480);
    Check(FAILED(ReadMFVideoFormat(type.Get(), format)), "reject unrepresentable frame width");
    Check(ReadMFVideoFormat(nullptr, format) == E_POINTER, "reject null media type");
}

void PixelAspectRatio() {
    auto type = MediaType(720, 480);
    Aperture(type.Get(), MF_MT_MINIMUM_DISPLAY_APERTURE, 704, 480, 8);
    Check(SUCCEEDED(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 10, 11)),
          "set non-square pixels");
    VideoFormat format;
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read non-square pixels");
    RECT source = {}, destination = {};
    Check(CalculateVideoRectangles(format, 720, 480, 1920, 1080, 1,
                                    source, destination), "fit 4:3 content");
    Check(RectEquals(source, 8, 0, 712, 480), "fit retains visible source");
    Check(RectEquals(destination, 240, 0, 1680, 1080), "fit pillarboxes 4:3 content");
    Check(CalculateVideoRectangles(format, 720, 480, 1920, 1080, 0,
                                    source, destination), "cover non-square content");
    Check(RectEquals(source, 8, 60, 712, 420), "cover crops with pixel aspect ratio");
    Check(CalculateVideoRectangles(format, 720, 480, 1920, 1080, 2,
                                    source, destination), "stretch content");
    Check(RectEquals(destination, 0, 0, 1920, 1080), "stretch fills the output");
}

void ColorMetadata() {
    auto type = MediaType(1920, 1080);
    VideoFormat format;
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read untagged color");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_NV12) ==
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709, "untagged HD fallback");
    Check(SUCCEEDED(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255)),
          "tag full range");
    Check(SUCCEEDED(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601)),
          "tag BT.601 on an HD frame");
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read full-range BT.601");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_NV12) ==
          DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601, "metadata overrides height heuristic");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_B8G8R8A8_UNORM) ==
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, "BGRA is not treated as YUV");

    const UINT32 matrices[] = {MFVideoTransferMatrix_BT709,
                               MFVideoTransferMatrix_BT2020_10, MFVideoTransferMatrix_BT2020_12};
    for (UINT32 matrix : matrices) {
        Check(SUCCEEDED(type->SetUINT32(MF_MT_YUV_MATRIX, matrix)), "set tagged matrix");
        Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read tagged matrix");
        const auto expected = matrix == MFVideoTransferMatrix_BT709
            ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709
            : DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;
        Check(VideoInputColorSpace(format, DXGI_FORMAT_NV12) == expected,
              "preserve full range with tagged matrix");
    }
    Check(SUCCEEDED(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235)),
          "tag studio range");
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read studio range");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_P010) ==
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020, "studio BT.2020 input");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_B8G8R8A8_UNORM) ==
          DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, "studio RGB input");
    type = MediaType(640, 480);
    Check(SUCCEEDED(ReadMFVideoFormat(type.Get(), format)), "read untagged SD");
    Check(VideoInputColorSpace(format, DXGI_FORMAT_NV12) ==
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601, "untagged SD fallback");
    // Re-reading a new type must not retain the previous stream's tags.
    Check(format.range == VideoRange::Unknown && format.matrix == VideoMatrix::Unknown,
          "clear stale color metadata");
}

// The selector uses the real COM method contract; this stub controls driver
// answers without requiring video processing hardware on the CI runner.
class ConversionEnumerator final : public ID3D11VideoProcessorEnumerator1 {
public:
    DXGI_COLOR_SPACE_TYPE preferred = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;
    bool preferred_supported = true;
    bool fallback_supported = true;
    HRESULT preferred_result = S_OK;
    bool arguments_valid = true;
    std::vector<DXGI_COLOR_SPACE_TYPE> checked;

    HRESULT STDMETHODCALLTYPE CheckVideoProcessorFormatConversion(
        DXGI_FORMAT input, DXGI_COLOR_SPACE_TYPE space, DXGI_FORMAT output,
        DXGI_COLOR_SPACE_TYPE output_space, BOOL* supported) override {
        arguments_valid = arguments_valid &&
            (input == DXGI_FORMAT_NV12 || input == DXGI_FORMAT_B8G8R8A8_UNORM) &&
            output == DXGI_FORMAT_B8G8R8A8_UNORM &&
            output_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        checked.push_back(space);
        *supported = (space == preferred ? preferred_supported : fallback_supported) ? TRUE : FALSE;
        return space == preferred ? preferred_result : S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override { *device = nullptr; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetVideoProcessorContentDesc(
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CheckVideoProcessorFormat(DXGI_FORMAT, UINT*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetVideoProcessorCaps(D3D11_VIDEO_PROCESSOR_CAPS*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetVideoProcessorRateConversionCaps(
        UINT, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetVideoProcessorCustomRate(
        UINT, UINT, D3D11_VIDEO_PROCESSOR_CUSTOM_RATE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetVideoProcessorFilterRange(
        D3D11_VIDEO_PROCESSOR_FILTER, D3D11_VIDEO_PROCESSOR_FILTER_RANGE*) override {
        return E_NOTIMPL;
    }
};

void DriverColorSupport() {
    ConversionEnumerator driver;
    DXGI_COLOR_SPACE_TYPE selected = DXGI_COLOR_SPACE_CUSTOM;
    Check(SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_NV12, driver.preferred, selected) &&
          selected == driver.preferred && driver.checked.size() == 1,
          "use the requested supported conversion");
    driver.preferred_supported = false;
    driver.checked.clear();
    Check(SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_NV12, driver.preferred, selected) &&
          selected == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 && driver.checked.size() == 2,
          "unsupported tagged YUV falls back to supported limited BT.709");
    driver.preferred_supported = true;
    driver.preferred_result = E_INVALIDARG;
    Check(SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_NV12, driver.preferred, selected) &&
          selected == DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709,
          "query errors fall back even when the driver sets supported true");
    driver.fallback_supported = false;
    selected = DXGI_COLOR_SPACE_CUSTOM;
    Check(!SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_NV12, driver.preferred, selected) &&
          selected == DXGI_COLOR_SPACE_CUSTOM, "do not choose an unsupported fallback");
    driver.preferred = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    driver.preferred_result = S_OK;
    driver.preferred_supported = false;
    driver.fallback_supported = true;
    Check(SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_B8G8R8A8_UNORM,
                                         driver.preferred, selected) &&
          selected == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
          "RGB fallback remains RGB");
    driver.preferred = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    driver.checked.clear();
    Check(!SelectSupportedVideoColorSpace(&driver, DXGI_FORMAT_NV12, driver.preferred, selected) &&
          driver.checked.size() == 1, "do not query identical candidates twice");
    Check(!SelectSupportedVideoColorSpace(nullptr, DXGI_FORMAT_NV12, driver.preferred, selected),
          "missing enumerator is not treated as support");
    Check(driver.arguments_valid, "query the actual input/output format and color-space pair");
}

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"LiteWallpaper-MF";

// Redirect HKCU only inside this test process, so no real startup value is touched.
class IsolatedRegistry {
public:
    IsolatedRegistry() {
        m_path = L"Software\\LiteWallpaperMF-Tests-" + std::to_wstring(GetCurrentProcessId()) +
                 L"-" + std::to_wstring(GetTickCount64());
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, m_path.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &m_root,
                              nullptr) == ERROR_SUCCESS, "create isolated test registry");
        const LSTATUS status = RegOverridePredefKey(HKEY_CURRENT_USER, m_root);
        if (status != ERROR_SUCCESS) {
            RegCloseKey(m_root);
            RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
            throw std::runtime_error("redirect test HKCU");
        }
    }
    ~IsolatedRegistry() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(m_root);
        RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
    }
    IsolatedRegistry(const IsolatedRegistry&) = delete;
    IsolatedRegistry& operator=(const IsolatedRegistry&) = delete;
private:
    HKEY m_root = nullptr;
    std::wstring m_path;
};

std::wstring ReadStartup() {
    wchar_t value[32768] = {};
    DWORD bytes = sizeof(value);
    Check(RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ, nullptr,
                       value, &bytes) == ERROR_SUCCESS, "read startup command");
    return value;
}

void StartupReconciliation() {
    IsolatedRegistry registry;
    bool enabled = true;
    Check(ReconcileRunAtStartup(enabled) && !enabled, "respect a missing Run key");
    Check(SetRunAtStartup(true), "enable startup in isolated registry");
    const std::wstring expected = ReadStartup();
    Check(expected.front() == L'\"' && expected.ends_with(L"\" --startup"),
          "quote the startup executable");
    enabled = true;
    Check(ReconcileRunAtStartup(enabled) && enabled && ReadStartup() == expected,
          "preserve matching startup entry");

    HKEY run = nullptr;
    Check(RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS,
          "open isolated Run key");
    constexpr wchar_t moved[] = L"\"C:\\old location\\LiteWallpaper-MF.exe\" --startup";
    const LSTATUS write = RegSetValueExW(run, kValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(moved), sizeof(moved));
    RegCloseKey(run);
    Check(write == ERROR_SUCCESS, "simulate moved executable");
    enabled = false;
    Check(ReconcileRunAtStartup(enabled) && !enabled && ReadStartup() == moved,
          "disabled setting does not rewrite registry");
    enabled = true;
    Check(ReconcileRunAtStartup(enabled) && enabled && ReadStartup() == expected,
          "repair startup path after executable move");
    Check(SetRunAtStartup(false), "remove isolated startup value");
    enabled = true;
    Check(ReconcileRunAtStartup(enabled) && !enabled, "respect deleted Run value");
    DWORD bytes = 0;
    Check(RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ, nullptr,
                       nullptr, &bytes) == ERROR_FILE_NOT_FOUND, "deleted value stays deleted");

    Check(RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS,
          "open key for invalid type");
    const DWORD invalid = 1;
    const LSTATUS invalid_write = RegSetValueExW(run, kValue, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&invalid), sizeof(invalid));
    RegCloseKey(run);
    Check(invalid_write == ERROR_SUCCESS, "simulate invalid registry type");
    enabled = true;
    Check(!ReconcileRunAtStartup(enabled) && enabled, "registry error preserves setting");
}

} // namespace

int main() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        CoUninitialize();
        return 1;
    }
    int failures = 0;
    const auto run = [&failures](const char* name, void (*test)()) {
        try {
            test();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            ++failures;
            std::fprintf(stderr, "FAIL %s: %s\n", name, error.what());
        }
    };
    run("visible geometry", VisibleGeometry);
    run("pixel aspect ratio", PixelAspectRatio);
    run("color metadata", ColorMetadata);
    run("driver color support", DriverColorSupport);
    run("startup reconciliation", StartupReconciliation);
    MFShutdown();
    CoUninitialize();
    return failures ? 1 : 0;
}
