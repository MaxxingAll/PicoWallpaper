#pragma once

#include <mfobjects.h>

#include "video_decoder.h"

namespace picowp {

// Read visible image geometry and color metadata independently of GPU allocation.
HRESULT ReadMFVideoFormat(IMFMediaType* media_type, VideoFormat& format);

} // namespace picowp
