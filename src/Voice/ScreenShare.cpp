#include "pch.h"
#include "ScreenShare.h"

#include <d3d11.h>
#include <mftransform.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wil/com.h>

#include <codecapi.h>
#include <d3d10.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <shobjidl_core.h>
#include <strmif.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wmcodecdsp.h>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace DiscordWin3::Voice
{
    class ScreenShareImpl
    {
    public:
        using FrameCallback = std::function<void(uint8_t const* annexB, size_t length, uint32_t timestamp90k)>;

        ScreenShareImpl() = default;
        ~ScreenShareImpl();

        // Shows the system picker (screens and windows). Null if cancelled. UI thread.
        static winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Graphics::Capture::GraphicsCaptureItem> PickAsync(HWND owner);

        bool Start(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item, FrameCallback onFrame,
                   uint32_t width = 1280, uint32_t height = 720, uint32_t fps = 30, uint32_t bitrate = 2500000);
        void Stop();
        bool Running() const { return m_running; }
        std::wstring const& Error() const { return m_error; }

    private:
        bool InitDevice();
        bool InitEncoder();
        void OnFrame(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& pool);
        bool EnsureProcessor(uint32_t inputWidth, uint32_t inputHeight);
        void Encode(uint8_t const* nv12, int64_t time100ns);
        void Drain(int64_t time100ns);

        FrameCallback m_onFrame;
        std::atomic<bool> m_running{ false };
        std::mutex m_lock;
        std::wstring m_error;
        uint32_t m_width = 1280, m_height = 720, m_fps = 30, m_bitrate = 2500000;
        int64_t m_lastFrame = 0;
        int64_t m_firstFrame = -1;

        // Capture
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{ nullptr };
        winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
        winrt::Windows::Graphics::SizeInt32 m_poolSize{};

        // GPU scaling / conversion
        wil::com_ptr_nothrow<ID3D11Device> m_device;
        wil::com_ptr_nothrow<ID3D11DeviceContext> m_context;
        wil::com_ptr_nothrow<ID3D11VideoDevice> m_videoDevice;
        wil::com_ptr_nothrow<ID3D11VideoContext> m_videoContext;
        wil::com_ptr_nothrow<ID3D11VideoProcessorEnumerator> m_enumerator;
        wil::com_ptr_nothrow<ID3D11VideoProcessor> m_processor;
        wil::com_ptr_nothrow<ID3D11Texture2D> m_input;        // copy of the captured frame (BGRA)
        wil::com_ptr_nothrow<ID3D11VideoProcessorInputView> m_inputView;
        wil::com_ptr_nothrow<ID3D11Texture2D> m_output;       // NV12, encoder size
        wil::com_ptr_nothrow<ID3D11VideoProcessorOutputView> m_outputView;
        wil::com_ptr_nothrow<ID3D11Texture2D> m_staging;      // NV12, CPU readable
        uint32_t m_inputWidth = 0, m_inputHeight = 0;
        std::vector<uint8_t> m_nv12;

        // Encoder
        wil::com_ptr_nothrow<IMFTransform> m_encoder;
        bool m_encoderProvidesSamples = false;
        uint32_t m_outputBufferSize = 0;
        std::vector<uint8_t> m_sequenceHeader;                // SPS/PPS (Annex B) re-sent before every IDR
        std::vector<uint8_t> m_frame;
    };
}

namespace DiscordWin3::Voice
{
    namespace
    {
        int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        bool IsIdr(uint8_t const* data, size_t length, bool& hasSps)
        {
            bool idr = false;
            hasSps = false;
            for (size_t i = 0; i + 3 < length; ++i)
            {
                if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
                {
                    uint8_t type = data[i + 3] & 0x1F;
                    if (type == 5) idr = true;
                    if (type == 7) hasSps = true;
                    i += 2;
                }
            }
            return idr;
        }
    }

    ScreenShareImpl::~ScreenShareImpl()
    {
        Stop();
    }

    winrt::Windows::Foundation::IAsyncOperation<GraphicsCaptureItem> ScreenShareImpl::PickAsync(HWND owner)
    {
        GraphicsCapturePicker picker;
        // Desktop apps must parent the picker to their window (classic COM interop interface).
        wil::com_ptr_nothrow<IInitializeWithWindow> init;
        if (SUCCEEDED(reinterpret_cast<::IUnknown*>(winrt::get_abi(picker))->QueryInterface(IID_PPV_ARGS(&init))))
        {
            init->Initialize(owner);
        }
        co_return co_await picker.PickSingleItemAsync();
    }

    bool ScreenShareImpl::InitDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                     &m_device, nullptr, &m_context)))
        {
            m_error = L"D3D11 device";
            return false;
        }
        if (auto multithread = m_device.try_query<ID3D10Multithread>()) multithread->SetMultithreadProtected(TRUE);
        m_videoDevice = m_device.try_query<ID3D11VideoDevice>();
        m_videoContext = m_context.try_query<ID3D11VideoContext>();
        if (!m_videoDevice || !m_videoContext)
        {
            m_error = L"GPU video processor unavailable";
            return false;
        }

        auto dxgi = m_device.try_query<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> inspectable;
        if (!dxgi || FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put())))
        {
            m_error = L"WinRT D3D device";
            return false;
        }
        m_winrtDevice = inspectable.as<IDirect3DDevice>();
        return true;
    }

    bool ScreenShareImpl::InitEncoder()
    {
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_encoder))))
        {
            m_error = L"H.264 encoder";
            return false;
        }

        wil::com_ptr_nothrow<IMFMediaType> output;
        MFCreateMediaType(&output);
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        output->SetUINT32(MF_MT_AVG_BITRATE, m_bitrate);
        MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, m_width, m_height);
        MFSetAttributeRatio(output.get(), MF_MT_FRAME_RATE, m_fps, 1);
        MFSetAttributeRatio(output.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);   // widest decoder support
        if (FAILED(m_encoder->SetOutputType(0, output.get(), 0)))
        {
            m_error = L"encoder output type";
            return false;
        }

        wil::com_ptr_nothrow<IMFMediaType> input;
        MFCreateMediaType(&input);
        input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(input.get(), MF_MT_FRAME_SIZE, m_width, m_height);
        MFSetAttributeRatio(input.get(), MF_MT_FRAME_RATE, m_fps, 1);
        MFSetAttributeRatio(input.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(m_encoder->SetInputType(0, input.get(), 0)))
        {
            m_error = L"encoder input type";
            return false;
        }

        if (auto codec = m_encoder.try_query<ICodecAPI>())
        {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_UI4;
            v.ulVal = eAVEncCommonRateControlMode_CBR;
            codec->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);
            v.ulVal = m_bitrate;
            codec->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v);
            v.ulVal = m_fps * 2;   // keyframe every 2 s so late viewers can start decoding
            codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);
            v.ulVal = 0;
            codec->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &v);
            v.vt = VT_BOOL;
            v.boolVal = VARIANT_TRUE;
            codec->SetValue(&CODECAPI_AVLowLatencyMode, &v);
        }

        // SPS/PPS, prepended to IDR frames that don't carry them.
        wil::com_ptr_nothrow<IMFMediaType> current;
        if (SUCCEEDED(m_encoder->GetOutputCurrentType(0, &current)))
        {
            UINT32 size = 0;
            if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size)
            {
                m_sequenceHeader.resize(size);
                current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, m_sequenceHeader.data(), size, nullptr);
            }
        }

        MFT_OUTPUT_STREAM_INFO info{};
        m_encoder->GetOutputStreamInfo(0, &info);
        m_encoderProvidesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
        m_outputBufferSize = std::max<uint32_t>(info.cbSize, m_width * m_height);
        m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    bool ScreenShareImpl::Start(GraphicsCaptureItem const& item, FrameCallback onFrame, uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate)
    {
        Stop();
        std::lock_guard guard{ m_lock };
        m_onFrame = std::move(onFrame);
        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;
        m_firstFrame = -1;
        m_nv12.resize(static_cast<size_t>(width) * height * 3 / 2);
        if (!InitDevice() || !InitEncoder()) return false;

        try
        {
            m_item = item;
            m_poolSize = item.Size();
            m_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(m_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_poolSize);
            m_session = m_pool.CreateCaptureSession(item);
            try { m_session.IsCursorCaptureEnabled(true); } catch (...) {}
            try { m_session.IsBorderRequired(false); } catch (...) {}   // Windows 11: no yellow border
            m_pool.FrameArrived([this](Direct3D11CaptureFramePool const& pool, auto&&) { OnFrame(pool); });
            m_item.Closed([this](auto&&, auto&&) { m_running = false; });   // shared window closed
            m_running = true;
            m_session.StartCapture();
        }
        catch (hresult_error const& e)
        {
            m_error = e.message();
            m_running = false;
            return false;
        }
        return true;
    }

    void ScreenShareImpl::Stop()
    {
        m_running = false;
        std::lock_guard guard{ m_lock };
        if (m_session) { try { m_session.Close(); } catch (...) {} m_session = nullptr; }
        if (m_pool) { try { m_pool.Close(); } catch (...) {} m_pool = nullptr; }
        m_item = nullptr;
        if (m_encoder)
        {
            m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            m_encoder.reset();
        }
        m_inputView.reset(); m_input.reset(); m_outputView.reset(); m_output.reset(); m_staging.reset();
        m_processor.reset(); m_enumerator.reset(); m_videoContext.reset(); m_videoDevice.reset();
        m_context.reset(); m_device.reset(); m_winrtDevice = nullptr;
        m_inputWidth = m_inputHeight = 0;
        m_nv12 = {};
        m_frame = {};
    }

    bool ScreenShareImpl::EnsureProcessor(uint32_t inputWidth, uint32_t inputHeight)
    {
        if (m_processor && inputWidth == m_inputWidth && inputHeight == m_inputHeight) return true;
        m_inputWidth = inputWidth;
        m_inputHeight = inputHeight;
        m_inputView.reset(); m_input.reset(); m_outputView.reset(); m_output.reset(); m_staging.reset();
        m_processor.reset(); m_enumerator.reset();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = { m_fps, 1 };
        content.InputWidth = inputWidth;
        content.InputHeight = inputHeight;
        content.OutputFrameRate = { m_fps, 1 };
        content.OutputWidth = m_width;
        content.OutputHeight = m_height;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&content, &m_enumerator))) return false;
        if (FAILED(m_videoDevice->CreateVideoProcessor(m_enumerator.get(), 0, &m_processor))) return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = inputWidth;
        desc.Height = inputHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_input))) return false;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        if (FAILED(m_videoDevice->CreateVideoProcessorInputView(m_input.get(), m_enumerator.get(), &inputDesc, &m_inputView))) return false;

        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = DXGI_FORMAT_NV12;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_output))) return false;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (FAILED(m_videoDevice->CreateVideoProcessorOutputView(m_output.get(), m_enumerator.get(), &outputDesc, &m_outputView))) return false;

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_staging))) return false;

        // Letterbox: keep the aspect ratio, black bars.
        double scale = std::min(static_cast<double>(m_width) / inputWidth, static_cast<double>(m_height) / inputHeight);
        LONG w = static_cast<LONG>(inputWidth * scale) & ~1, h = static_cast<LONG>(inputHeight * scale) & ~1;
        RECT dest{ static_cast<LONG>((m_width - w) / 2), static_cast<LONG>((m_height - h) / 2), 0, 0 };
        dest.right = dest.left + w;
        dest.bottom = dest.top + h;
        m_videoContext->VideoProcessorSetStreamDestRect(m_processor.get(), 0, TRUE, &dest);
        D3D11_VIDEO_COLOR black{};
        black.YCbCr = { 0.0625f, 0.5f, 0.5f, 1.0f };
        m_videoContext->VideoProcessorSetOutputBackgroundColor(m_processor.get(), TRUE, &black);
        m_videoContext->VideoProcessorSetStreamFrameFormat(m_processor.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        return true;
    }

    void ScreenShareImpl::OnFrame(Direct3D11CaptureFramePool const& pool)
    {
        auto frame = pool.TryGetNextFrame();
        if (!frame || !m_running) return;
        std::lock_guard guard{ m_lock };
        if (!m_encoder) return;

        // Frame rate cap (Windows delivers a frame per screen refresh when content changes).
        auto now = NowMs();
        if (now - m_lastFrame < static_cast<int64_t>(1000 / m_fps) - 2) return;
        m_lastFrame = now;
        if (m_firstFrame < 0) m_firstFrame = now;

        auto size = frame.ContentSize();
        if (size.Width != m_poolSize.Width || size.Height != m_poolSize.Height)
        {
            // Shared window resized: recreate the pool at the new size, skip this frame.
            m_poolSize = size;
            pool.Recreate(m_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            return;
        }

        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(&texture)))) return;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (!EnsureProcessor(desc.Width, desc.Height)) return;

        m_context->CopyResource(m_input.get(), texture.get());
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = m_inputView.get();
        if (FAILED(m_videoContext->VideoProcessorBlt(m_processor.get(), m_outputView.get(), 0, 1, &stream))) return;
        m_context->CopyResource(m_staging.get(), m_output.get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(m_staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
        auto src = static_cast<uint8_t const*>(mapped.pData);
        for (uint32_t y = 0; y < m_height; ++y)   // Y plane
            memcpy(m_nv12.data() + static_cast<size_t>(y) * m_width, src + static_cast<size_t>(y) * mapped.RowPitch, m_width);
        auto uvSrc = src + static_cast<size_t>(mapped.RowPitch) * m_height;
        auto uvDst = m_nv12.data() + static_cast<size_t>(m_width) * m_height;
        for (uint32_t y = 0; y < m_height / 2; ++y)   // interleaved UV plane
            memcpy(uvDst + static_cast<size_t>(y) * m_width, uvSrc + static_cast<size_t>(y) * mapped.RowPitch, m_width);
        m_context->Unmap(m_staging.get(), 0);

        Encode(m_nv12.data(), (now - m_firstFrame) * 10000);
    }

    void ScreenShareImpl::Encode(uint8_t const* nv12, int64_t time100ns)
    {
        wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(m_nv12.size()), &buffer))) return;
        BYTE* data = nullptr;
        buffer->Lock(&data, nullptr, nullptr);
        memcpy(data, nv12, m_nv12.size());
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(m_nv12.size()));

        wil::com_ptr_nothrow<IMFSample> sample;
        MFCreateSample(&sample);
        sample->AddBuffer(buffer.get());
        sample->SetSampleTime(time100ns);
        sample->SetSampleDuration(10000000 / m_fps);
        if (FAILED(m_encoder->ProcessInput(0, sample.get(), 0))) return;
        Drain(time100ns);
    }

    void ScreenShareImpl::Drain(int64_t time100ns)
    {
        for (;;)
        {
            wil::com_ptr_nothrow<IMFSample> outSample;
            if (!m_encoderProvidesSamples)
            {
                wil::com_ptr_nothrow<IMFMediaBuffer> outBuffer;
                MFCreateMemoryBuffer(m_outputBufferSize, &outBuffer);
                MFCreateSample(&outSample);
                outSample->AddBuffer(outBuffer.get());
            }
            MFT_OUTPUT_DATA_BUFFER output{};
            output.pSample = outSample.get();
            DWORD status = 0;
            HRESULT hr = m_encoder->ProcessOutput(0, 1, &output, &status);
            if (output.pEvents) output.pEvents->Release();
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            if (FAILED(hr)) return;

            IMFSample* produced = m_encoderProvidesSamples ? output.pSample : outSample.get();
            wil::com_ptr_nothrow<IMFMediaBuffer> contiguous;
            if (produced && SUCCEEDED(produced->ConvertToContiguousBuffer(&contiguous)))
            {
                BYTE* bytes = nullptr;
                DWORD length = 0;
                contiguous->Lock(&bytes, nullptr, &length);
                bool hasSps = false;
                bool idr = IsIdr(bytes, length, hasSps);
                m_frame.clear();
                if (idr && !hasSps) m_frame.insert(m_frame.end(), m_sequenceHeader.begin(), m_sequenceHeader.end());
                m_frame.insert(m_frame.end(), bytes, bytes + length);
                contiguous->Unlock();
                auto timestamp = static_cast<uint32_t>(time100ns * 9 / 1000);   // 100 ns -> 90 kHz
                if (m_onFrame && !m_frame.empty()) m_onFrame(m_frame.data(), m_frame.size(), timestamp);
            }
            if (m_encoderProvidesSamples && output.pSample) output.pSample->Release();
        }
    }
}

namespace DiscordWin3::Voice
{
    ScreenShare::ScreenShare() : m_impl(std::make_unique<ScreenShareImpl>()) {}
    ScreenShare::~ScreenShare() = default;

    winrt::Windows::Foundation::IAsyncOperation<GraphicsCaptureItem> ScreenShare::PickAsync(HWND owner)
    {
        return ScreenShareImpl::PickAsync(owner);
    }

    bool ScreenShare::Start(GraphicsCaptureItem const& item, FrameCallback onFrame, uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate)
    {
        return m_impl->Start(item, std::move(onFrame), width, height, fps, bitrate);
    }

    void ScreenShare::Stop() { m_impl->Stop(); }
    bool ScreenShare::Running() const { return m_impl->Running(); }
    std::wstring const& ScreenShare::Error() const { return m_impl->Error(); }
}
