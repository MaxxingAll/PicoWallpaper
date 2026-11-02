#include "mf_video_format.h"

#include <mfapi.h>
#include <mferror.h>

#include <cmath>
#include <climits>

namespace miniwp {
namespace {

HRESULT ReadArea(IMFMediaType* media_type, REFGUID key, MFVideoArea& area) {
    UINT32 bytes = 0;
    const HRESULT hr = media_type->GetBlob(
        key, reinterpret_cast<UINT8*>(&area), sizeof(area), &bytes);
    if (FAILED(hr)) return hr;
    return bytes == sizeof(area) ? S_OK : MF_E_INVALIDMEDIATYPE;
}

} // namespace

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
        if (left < 0 || top < 0 || area.Area.cx <= 0 || area.Area.cy <= 0 ||
            left + area.Area.cx > width || top + area.Area.cy > height) {
            return MF_E_INVALIDMEDIATYPE;
        }
        // D3D11 takes integer source rectangles. Round inward so fractional
        // apertures cannot expose pixels outside the valid image.
        format.display_left = static_cast<int>(std::ceil(left));
        format.display_top = static_cast<int>(std::ceil(top));
        format.display_width = static_cast<int>(std::floor(left + area.Area.cx)) -
                               format.display_left;
        format.display_height = static_cast<int>(std::floor(top + area.Area.cy)) -
                                format.display_top;
        if (format.display_width <= 0 || format.display_height <= 0) {
            return MF_E_INVALIDMEDIATYPE;
        }
    } else if (hr != MF_E_ATTRIBUTENOTFOUND) {
        return hr;
    }

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

} // namespace miniwp
