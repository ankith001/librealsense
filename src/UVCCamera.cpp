#include "UVCCamera.h"

////////////////
// UVC Camera //
////////////////

namespace rs
{
    
UVCCamera::UVCCamera(uvc_context_t * c, uvc_device_t * h, int idx) : rs_camera(idx), hardware(h), internalContext(c)
{
    uvc_device_descriptor_t * desc;
    uvc_error_t status = uvc_get_device_descriptor(h, &desc);
    if(status < 0) {} // Handle error

    cameraName = desc->product;
    uvc_free_device_descriptor(desc);
}

UVCCamera::~UVCCamera()
{
    for (auto & stream : streams)
    {
        if(stream && stream->uvcHandle)
        {
            uvc_close(stream->uvcHandle);
        }
    }
    uvc_unref_device(hardware);
}

void UVCCamera::EnableStream(int stream, int width, int height, int fps, FrameFormat format)
{
    int cameraNumber;
    switch(stream)
    {
    case RS_DEPTH: cameraNumber = GetDepthCameraNumber(); break;
    case RS_COLOR: cameraNumber = GetColorCameraNumber(); break;
    default: throw std::invalid_argument("unsupported stream");
    }

    streams[stream].reset(new StreamInterface());
    streams[stream]->camera = this;
    CheckUVC("uvc_open2", uvc_open2(hardware, &streams[stream]->uvcHandle, cameraNumber));
    uvc_print_stream_ctrl(&streams[stream]->ctrl, stdout); // Debugging

    RetrieveCalibration();

    // TODO: Check formats and the like
    streams[stream]->fmt = static_cast<uvc_frame_format>(format);
    CheckUVC("uvc_get_stream_ctrl_format_size", uvc_get_stream_ctrl_format_size(streams[stream]->uvcHandle, &streams[stream]->ctrl, streams[stream]->fmt, width, height, fps));

    switch(stream)
    {
    case RS_DEPTH:
        switch(format)
        {
        case FrameFormat::Z16:
        case FrameFormat::INVR:
        case FrameFormat::INVZ:
            streams[stream]->buffer.resize(width * height * sizeof(uint16_t));
            vertices.resize(width * height * 3);
            break;
        default: throw std::runtime_error("invalid frame format");
        }
        break;
    case RS_COLOR:
        switch(format)
        {
        case FrameFormat::YUYV:
            streams[stream]->buffer.resize(width * height * 3); break;
        default: throw std::runtime_error("invalid frame format");
        }
        break;
    }
}

void UVCCamera::StartStreaming()
{
    GetUSBInfo(hardware, usbInfo);
    std::cout << "Serial Number: " << usbInfo.serial << std::endl;
    std::cout << "USB VID: " << usbInfo.vid << std::endl;
    std::cout << "USB PID: " << usbInfo.pid << std::endl;

    SetStreamIntent(!!streams[RS_DEPTH], !!streams[RS_COLOR]);

    for(auto & stream : streams)
    {
        auto callback = [](uvc_frame_t * frame, void * ptr)
        {
            auto stream = static_cast<StreamInterface*>(ptr);
            switch(stream->fmt)
            {
            case UVC_FRAME_FORMAT_Z16:
            case UVC_FRAME_FORMAT_INVR:
            case UVC_FRAME_FORMAT_INVZ:
                memcpy(stream->buffer.back_data(), frame->data, (frame->width * frame->height - 1) * sizeof(uint16_t));
                break;
            case UVC_FRAME_FORMAT_YUYV:
                convert_yuyv_rgb((uint8_t *)frame->data, frame->width, frame->height, stream->buffer.back_data());
                break;
            default: throw std::runtime_error("bad format");
            }
            stream->buffer.swap_back();
        };

        if (stream && stream->uvcHandle) CheckUVC("uvc_start_streaming", uvc_start_streaming(stream->uvcHandle, &stream->ctrl, callback, stream.get(), 0));
    }
}

void UVCCamera::StopStreaming()
{

}
    
void UVCCamera::WaitAllStreams()
{
    if(streams[RS_COLOR])
    {
        streams[RS_COLOR]->buffer.swap_front();
    }

    if(streams[RS_DEPTH])
    {
        if(streams[RS_DEPTH]->buffer.swap_front())
        {
            ComputeVertexImage();
        }
    }
}

} // end namespace rs
