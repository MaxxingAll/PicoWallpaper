#include "mf_video_format.h"

#include <mfapi.h>
#include <mferror.h>

#include <cmath>
#include <climits>

namespace picowp {
namespace {

HRESULT ReadArea(IMFMediaType* media_type, REFGUID key, MFVideoArea& area) {
    UINT32 bytes = 0;
    const HRESULT hr = media_type->GetBlob(
        key, reinterpret_cast<UINT8*>(&area), sizeof(area), &bytes);
    if (FAILED(hr)) return hr;
    return bytes == sizeof(area) ? S_OK : MF_E_INVALIDMEDIATYPE;
}

} // namespace

double MeasuredFrameRate(long long first_100ns, long long last_100ns, int samples) {
    if (samples < 2 || last_100ns <= first_100ns) return 0.0;
    const double fps = 10000000.0 * (samples - 1) / static_cast<double>(last_100ns - first_100ns);
    if (fps < 1.0 || fps > 240.0) return 0.0;
    const double whole = std::round(fps);
    return std::abs(fps - whole) <= whole * 0.015 ? whole : fps;
}

HRESULT ReadMFVideoFormat(IMFMediaType* media_type, VideoFormat& format) {
    if (!media_type) return E_POINTER;
    format = {};
    UINT32 width = 0, height = 0;
    HRESULT hr = MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) return hr;
    if (!width || !height || width > INT_MAX || height > INT_MAX) {
        return MF_E_INVALIDMEDIATYPE;
    }
    format.width = static_cast<int>(width);
    format.height = static_cast<int>(height);
    format.display_width = format.width;
    format.display_height = format.height;

    MFVideoArea area = {};
    hr = MF_E_ATTRIBUTENOTFOUND;
    if (MFGetAttributeUINT32(media_type, MF_MT_PAN_SCAN_ENABLED, FALSE)) {
        hr = ReadArea(media_type, MF_MT_PAN_SCAN_APERTURE, area);
    }
    if (hr == MF_E_ATTRIBUTENOTFOUND) {
        hr = ReadArea(media_type, MF_MT_MINIMUM_DISPLAY_APERTURE, area);
    }
    if (hr == MF_E_ATTRIBUTENOTFOUND) {
        hr = ReadArea(media_type, MF_MT_GEOMETRIC_APERTURE, area);
    }
    if (SUCCEEDED(hr)) {
        const double left = area.OffsetX.value + area.OffsetX.fract / 65536.0;
        const double top = area.OffsetY.value + area.OffsetY.fract / 65536.0;
        if (left >= 0 && top >= 0 && area.Area.cx > 0 && area.Area.cy > 0 &&
            left + area.Area.cx <= width && top + area.Area.cy <= height) {
            // D3D11 takes integer source rectangles. Round valid fractional
            // apertures inward; unusable optional metadata keeps the full frame.
            const int x = static_cast<int>(std::ceil(left));
            const int y = static_cast<int>(std::ceil(top));
            const int right = static_cast<int>(std::floor(left + area.Area.cx));
            const int bottom = static_cast<int>(std::floor(top + area.Area.cy));
            if (right > x && bottom > y) {
                format.display_left = x;
                format.display_top = y;
                format.display_width = right - x;
                format.display_height = bottom - y;
            }
        }
    }
    // Missing, malformed, oversized or wrongly typed apertures are advisory.
    // Only an invalid mandatory frame size prevents playback.

    UINT32 numerator = 0, denominator = 0;
    if (SUCCEEDED(MFGetAttributeRatio(media_type, MF_MT_PIXEL_ASPECT_RATIO,
                                     &numerator, &denominator)) && numerator && denominator) {
        format.pixel_aspect_numerator = numerator;
        format.pixel_aspect_denominator = denominator;
    }

    const UINT32 range = MFGetAttributeUINT32(
        media_type, MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Unknown);
    if (range == MFNominalRange_0_255) format.range = VideoRange::Full;
    if (range == MFNominalRange_16_235) format.range = VideoRange::Limited;

    switch (MFGetAttributeUINT32(media_type, MF_MT_YUV_MATRIX,
                                MFVideoTransferMatrix_Unknown)) {
        case MFVideoTransferMatrix_BT601: format.matrix = VideoMatrix::BT601; break;
        case MFVideoTransferMatrix_BT709: format.matrix = VideoMatrix::BT709; break;
        case MFVideoTransferMatrix_BT2020_10:
        case MFVideoTransferMatrix_BT2020_12: format.matrix = VideoMatrix::BT2020; break;
        default: break;
    }
    return S_OK;
}

} // namespace picowp
