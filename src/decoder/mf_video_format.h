#pragma once

#include <mfobjects.h>

#include "video_decoder.h"

namespace picowp {

// Read visible image geometry and color metadata independently of GPU allocation.
HRESULT ReadMFVideoFormat(IMFMediaType* media_type, VideoFormat& format);

// Frame rate from the first and last of `samples` consecutive timestamps (100 ns).
// Within 1.5 % of a whole number it snaps to it (WebM uses millisecond timestamps).
// Returns 0 when there is no usable rate.
double MeasuredFrameRate(long long first_100ns, long long last_100ns, int samples);

} // namespace picowp
