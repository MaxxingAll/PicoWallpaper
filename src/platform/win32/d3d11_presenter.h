#pragma once

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <vector>

#include "decoder/video_decoder.h"

namespace miniwp {

bool CalculateVideoRectangles(const VideoFormat& format, UINT texture_width,
                              UINT texture_height, int output_width, int output_height,
                              int scaling_mode, RECT& source, RECT& destination);
DXGI_COLOR_SPACE_TYPE VideoInputColorSpace(const VideoFormat& format, DXGI_FORMAT texture_format);
bool SelectSupportedVideoColorSpace(ID3D11VideoProcessorEnumerator1* enumerator,
                                    DXGI_FORMAT input_format,
                                    DXGI_COLOR_SPACE_TYPE requested,
                                    DXGI_COLOR_SPACE_TYPE& selected);

class D3D11Presenter {
public:
    ~D3D11Presenter();

    bool Init(HWND window, int width, int height);
    bool RenderFrame(const VideoFrame& frame, int scaling_mode = 0);
    HRESULT Present(UINT sync_interval = 0);
    bool Resize(int width, int height);
    void ReleaseFrameResources();
    void Cleanup();

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    HANDLE GetWaitableObject() const { return m_frame_waitable; }

private:
    bool CreateSwapChain();
    bool CreateVideoProcessor(UINT input_width, UINT input_height);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swap_chain;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_video_device;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_video_context;
    Microsoft::WRL::ComPtr<ID3D11VideoContext1> m_video_context1;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> m_enumerator1;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_processor;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_output_view;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_input_texture;
    std::vector<Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>> m_input_views;

    HWND m_window = nullptr;
    HANDLE m_frame_waitable = nullptr;
    int m_width = 0;
    int m_height = 0;
    UINT m_input_width = 0;
    UINT m_input_height = 0;
    DXGI_FORMAT m_color_input_format = DXGI_FORMAT_UNKNOWN;
    DXGI_COLOR_SPACE_TYPE m_requested_color_space = DXGI_COLOR_SPACE_CUSTOM;
    DXGI_COLOR_SPACE_TYPE m_input_color_space = DXGI_COLOR_SPACE_CUSTOM;
};

} // namespace miniwp
