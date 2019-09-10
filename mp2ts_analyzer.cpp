/*
    Original code by Mike Cancilla (https://github.com/mikecancilla)
    2019

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any
    damages arising from the use of this software.

    Permission is granted to anyone to use this software for any
    purpose, including commercial applications, and to alter it and
    redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product documentation
    would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
    must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

/*
    This program is still under development!

    Known Issues:

    - Only handles the first Video and Audio stream
    - GUI does not talk to decoder
    - Needs to feed a decoder
*/

#define DUMP_OUTPUT_FILE 0

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>

// OpenGL
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// GLM
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

// ImGUI
#include "imgui.h"
#include "imgui_impl_glfw_gl3.h"

// FFMPEG
extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/common.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/threadmessage.h>
    #include <libavutil/time.h>
    #include <libavutil/audio_fifo.h>
    #include <libswresample/swresample.h>
    #include <libavutil/frame.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
}

// My OpenGL Classes
#include "Renderer.h"
#include "Texture.h"
#include "VertexBufferLayout.h"
#include "TexturePresenter.h"

#include "mp2ts_xml.h"

extern void DoMyXMLTest(char *pXMLFile);
extern void DoMyXMLTest2();
extern int DoFFMpegOpenGLTest(const std::string &inFileName);

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 900

#define KEY_RIGHT_ARROW 262
#define KEY_LEFT_ARROW 263

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

enum PlayState
{
    eStopped,
    ePlaying,
    eSeeking
};

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
//    AVCodecContext *enc_ctx;
    AVAudioFifo *audio_fifo;
} StreamContext;

typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
} FilteringContext;

#if DUMP_OUTPUT_FILE
static FILE *g_fpTemp = NULL;
#endif

static FILE *inputFile = NULL;

// Describes the input file
static AVFormatContext  *g_ifmt_ctx = NULL;
static StreamContext    *g_stream_ctx = NULL;
static AVFrame          *g_pFrame = NULL;
static GLFWwindow       *g_window = NULL;
static TexturePresenter *g_pTexturePresenter = NULL;
static FilteringContext *g_filter_ctx = NULL;

static char             g_error[AV_ERROR_MAX_STRING_SIZE] = {0};

static int InitFilter(FilteringContext *fctx,
                      AVStream *st,
                      AVCodecContext *dec_ctx,
                      //AVCodecContext *enc_ctx,
                      const char *filter_spec);
static int InitFilters();

template <typename T>
void IncAndClamp(T &value, T &incBy, T min, T max)

{
    value += incBy;

    if(value < min)
    {
        value = min;
        incBy *= (T) -1;
    }
    else if(value > max)
    {
        value = max;
        incBy *= (T) -1;
    }
}

static int OpenInputFile(MpegTS_XML &mpts)
{
    int ret;
    g_ifmt_ctx = NULL;
    std::string inFileName = mpts.m_mpegTSDescriptor.fileName;

    av_log_set_level(AV_LOG_VERBOSE);

    if ((ret = avformat_open_input(&g_ifmt_ctx, inFileName.c_str(), NULL, NULL)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file: %s\n", av_make_error_string(g_error, AV_ERROR_MAX_STRING_SIZE, ret));
        return ret;
    }

    if ((ret = avformat_find_stream_info(g_ifmt_ctx, NULL)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    g_stream_ctx = (StreamContext *)av_mallocz_array(g_ifmt_ctx->nb_streams, sizeof(*g_stream_ctx));
    if (!g_stream_ctx)
        return AVERROR(ENOMEM);

    bool bWantVideoIndex = true;
    bool bWantAudioIndex = true;

    for(unsigned int streamIndex = 0; streamIndex < g_ifmt_ctx->nb_streams; streamIndex++)
    {
        if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if(bWantVideoIndex)
            {
                mpts.m_videoStreamIndex = streamIndex;
                bWantVideoIndex = false;
                break;
            }
        }

        if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if(bWantAudioIndex)
            {
                mpts.m_audioStreamIndex = streamIndex;
                bWantAudioIndex = false;
                break;
            }
        }
    }

    if(-1 == mpts.m_videoStreamIndex)
    {
        fprintf(stderr, "Error: No video stream found in source file!!\n");
        return AVERROR_STREAM_NOT_FOUND;
    }
        
    AVStream *stream = g_ifmt_ctx->streams[mpts.m_videoStreamIndex];
        
    AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *codec_ctx = NULL;

    if (!dec)
        return AVERROR_DECODER_NOT_FOUND;

    codec_ctx = avcodec_alloc_context3(dec);
    if (!codec_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", mpts.m_videoStreamIndex);
        return AVERROR(ENOMEM);
    }

    ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
            "for stream #%u\n", mpts.m_videoStreamIndex);
        return ret;
    }

    codec_ctx->framerate = av_guess_frame_rate(g_ifmt_ctx, stream, NULL);

    // Open decoder
    ret = avcodec_open2(codec_ctx, dec, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", mpts.m_videoStreamIndex);
        return ret;
    }

    g_stream_ctx[mpts.m_videoStreamIndex].dec_ctx = codec_ctx;

    av_dump_format(g_ifmt_ctx, 0, inFileName.c_str(), 0);

#if DUMP_OUTPUT_FILE
    g_fpTemp = fopen("C:\\Temp\\temp.mpg", "wb");
#endif

    return 0;
}

static int CloseInputFile(MpegTS_XML &mpts)
{
    for(unsigned int i = 0; i < g_ifmt_ctx->nb_streams; i++)
    {
        if(&(g_stream_ctx[i]))
            avcodec_free_context(&g_stream_ctx[i].dec_ctx);
    }

    avformat_close_input(&g_ifmt_ctx);

#if DUMP_OUTPUT_FILE
    if(g_fpTemp)
        fclose(g_fpTemp);
#endif

    return 0;
}

static int InitOpenGL(std::string windowTitle)
{
    /* Initialize the library */
    if(!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    /* Create a windowed mode window and its OpenGL context */
    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, windowTitle.c_str(), NULL, NULL);
    if (!g_window)
    {
        fprintf(stderr, "Could not create GL window! Continuing without a GUI!\n");
        glfwTerminate();
        return -1;
    }

    /* Make the window's context current */
    glfwMakeContextCurrent(g_window);

    glfwSwapInterval(1);

    if(glewInit() != GLEW_OK)
    {
        fprintf(stderr, "Glew Initialization Error! Continuing without a GUI!\n");
        glfwTerminate();
        return -1;
    }

    return 0;
}

static int CloseOpenGL()
{
    glfwTerminate();
    return 0;
}

static void FlipAvFrame(AVFrame *pFrame)
{
    // We need to flip the video image
    //  See: https://lists.ffmpeg.org/pipermail/ffmpeg-user/2011-May/000976.html
    pFrame->data[0] += pFrame->linesize[0] * (pFrame->height - 1);
    pFrame->linesize[0] = -(pFrame->linesize[0]);

    pFrame->data[1] += pFrame->linesize[1] * ((pFrame->height/2) - 1);
    pFrame->linesize[1] = -(pFrame->linesize[1]);

    pFrame->data[2] += pFrame->linesize[2] * ((pFrame->height/2) - 1);
    pFrame->linesize[2] = -(pFrame->linesize[2]);
}

static uint8_t *dst_data[4];
static int dst_linesize[4];

static bool WriteFrame(AVCodecContext *dec_ctx,
                       AVFrame *frame,
                       int frame_num,
                       TexturePresenter *pTexturePresenter,
                       bool &bNewFrame)
{
    enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGBA;
    struct SwsContext *sws_ctx = NULL;

    if(bNewFrame)
    {
        if(dst_data[0])
            av_freep(&dst_data[0]);

        // create scaling context
        sws_ctx = sws_getContext(frame->width, frame->height, (enum AVPixelFormat) frame->format,
                                 frame->width, frame->height, dst_pix_fmt,
                                 SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx)
        {
            fprintf(stderr,
                    "Impossible to create scale context for the conversion "
                    "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                    av_get_pix_fmt_name((enum AVPixelFormat) frame->format), frame->width, frame->height,
                    av_get_pix_fmt_name(dst_pix_fmt), frame->width, frame->height);
            return false;
        }

        int dst_bufsize = 0;

        // buffer is going to be rawvideo file, no alignment
        if ((dst_bufsize = av_image_alloc(dst_data, dst_linesize,
                                          frame->width, frame->height, dst_pix_fmt, 1)) < 0)
        {
            fprintf(stderr, "Could not allocate destination image\n");
            return false;
        }

        /* convert to destination format */
        sws_scale(sws_ctx,
                  (const uint8_t * const*) frame->data,
                  frame->linesize,
                  0,
                  frame->height,
                  dst_data,
                  dst_linesize);

        // Save the frame to disk, only works with 24 bpp
        //SaveFrame(dst_data[0], frame->width, frame->height, dst_linesize[0], frame_num);
        //return true;

        /*
        // Determine required buffer size and allocate buffer
        int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24,
                                          frame->width,
                                          frame->height);

        uint8_t *buffer = (uint8_t*) malloc(numBytes);

        DeStride(dst_data[0], frame->width, frame->height, dst_linesize[0], buffer);
        */

        bNewFrame = false;
    }

	pTexturePresenter->Render(dst_data[0], dec_ctx->width, dec_ctx->height);

    sws_freeContext(sws_ctx);

    return true;
}

static size_t inline increment_ptr(uint8_t *&p, size_t bytes)
{
    p += bytes;
    return bytes;
}

static inline uint16_t read_2_bytes(uint8_t *p)
{
	uint16_t ret = *p++;
	ret <<= 8;
	ret |= *p++;

	return ret;
}

static inline uint32_t read_4_bytes(uint8_t *p)
{
    uint32_t ret = 0;
    uint32_t val = *p++;
    ret = val<<24;
    val = *p++;
    ret |= val << 16;
    val = *p++;
    ret |= val << 8;
    ret |= *p;

    return ret;
}

inline uint8_t process_adaptation_field(uint8_t *&p)
{
    uint8_t adaptation_field_length = *p;
    return adaptation_field_length+1;
}

static int FindData(uint8_t *packet, int packetSize)
{
    uint8_t *p = packet;

    if (0x47 != *p)
    {
        fprintf(stderr, "Error: Packet does not start with 0x47\n");
        return -1;
    }

    // Skip the sync byte 0x47
    increment_ptr(p, 1);

    uint16_t PID = read_2_bytes(p);
    increment_ptr(p, 2);

    uint8_t transport_error_indicator = (PID & 0x8000) >> 15;
    uint8_t payload_unit_start_indicator = (PID & 0x4000) >> 14;

    uint8_t transport_priority = (PID & 0x2000) >> 13;

    PID &= 0x1FFF;

    // Move beyond the 32 bit header
    uint8_t final_byte = *p;
    increment_ptr(p, 1);

    uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
    uint8_t adaptation_field_control = (final_byte & 0x30) >> 4;
    uint8_t continuity_counter = (final_byte & 0x0F) >> 4;

    /*
        Table 2-5 – Adaptation field control values
            Value  Description
             00    Reserved for future use by ISO/IEC
             01    No adaptation_field, payload only
             10    Adaptation_field only, no payload
             11    Adaptation_field followed by payload
    */

    uint8_t adaptation_field_length = 0;

    if(2 == adaptation_field_control)
    {
        return packetSize;
    }
    else if(3 == adaptation_field_control)
    {
        adaptation_field_length = process_adaptation_field(p);
    }

    increment_ptr(p, adaptation_field_length);

    /*
    http://dvd.sourceforge.net/dvdinfo/mpeghdrs.html

    TODO: When demuxing to an elementary stream with the -f rawvideo flag,
    FFMPEG removes start codes outside the range of 0x00-0xB8.
    I'm just doing what they do.  It makes debugging my output easier.
    This way I have something to compare against.

    This step is not techinically necessary, a decoder will handle this data
    if it is in the stream.
    */

    uint32_t fourBytes = read_4_bytes(p);
    uint32_t startCodePrefix = (fourBytes & 0xFFFFFF00) >> 8;
    if(0x000001 == startCodePrefix)
    {
        uint8_t startCode = fourBytes & 0xFF;

        // 0xB9-0xFF are stream ids, don't need them
        while(startCode > 0xB8 && (p+1 - packet < packetSize))
        {
            startCodePrefix = 0;
            while(startCodePrefix != 0x000001 && (p+1 - packet < packetSize))
            {
                increment_ptr(p, 1);
                fourBytes = read_4_bytes(p);
                startCodePrefix = (fourBytes & 0xFFFFFF00) >> 8;
            }

            startCode = fourBytes & 0xFF;
        }
    }

    return p - packet;
}

static unsigned int FrameNumberFromBytePos(uint64_t &bytePos, std::vector<AccessUnit> &accessUnitList)
{
    // If searching for beginning of file, just return 0
    if(0 == bytePos)
        return 0;

    // Look for bytePos in the list of AUs
    for(std::vector<AccessUnit>::iterator i = accessUnitList.begin(); i < accessUnitList.end(); i++)
    {
        if(i->accessUnitElements[0].startByteLocation >= bytePos)
        {
            bytePos = i->accessUnitElements[0].startByteLocation;
            return i->frameNumber;
        }
    }

    // Didn't fine one? Return the frame number of the last AU
    return accessUnitList.back().frameNumber;
}

static unsigned int FrameNumberFromBytePosInternal(uint64_t &bytePos, std::vector<AccessUnit> &accessUnitList)
{
    // If searching for beginning of file, just return 0
    if(0 == bytePos)
        return 0;

    // Look for bytePos in the list of AUs
    for(std::vector<AccessUnit>::iterator i = accessUnitList.begin(); i < accessUnitList.end(); i++)
    {
        if(i->accessUnitElements[0].startByteLocation >= bytePos && i->frameType == "I")
        {
            bytePos = i->accessUnitElements[0].startByteLocation;
            return i->frameNumber;
        }
    }

    // Didn't fine one? Return the frame number of the last AU
    return accessUnitList.back().frameNumber;
}

uint8_t g_pVideoData[1000000];

static void WriteAllFramesToFile(MpegTS_XML &mpts)
{
#if DUMP_OUTPUT_FILE
    int packetSize = mpts.m_mpegTSDescriptor.packetSize;
    uint8_t *buffer = (uint8_t*) alloca(packetSize);

    for(unsigned int f=0; f<mpts.m_videoAccessUnitsDecode.size(); f++ )
    {
        for(auto aue : mpts.m_videoAccessUnitsDecode[f].accessUnitElements)
        {
            // Seek to aue.startByteLocation
            fseek(inputFile, aue.startByteLocation, SEEK_SET);

            for(unsigned int i=0; i<aue.numPackets; i++)
            {
                fread(buffer, packetSize, 1, inputFile);

                uint8_t count = FindData(buffer, packetSize);

                if(count)
                    fwrite(buffer+count, packetSize-count, 1, g_fpTemp);
            }
        }
    }
#endif
}

static void FlushDecoder()
{
    int ret = avcodec_send_packet(g_stream_ctx[0].dec_ctx, NULL);

    while (ret >= 0) {
        AVFrame *frame = av_frame_alloc();
        ret = avcodec_receive_frame(g_stream_ctx[0].dec_ctx, frame);
        av_frame_free(&frame);
    }

    //avformat_flush(g_ifmt_ctx);
    //avio_flush(g_ifmt_ctx->pb);
    avcodec_flush_buffers(g_stream_ctx[0].dec_ctx);
}

static AVFrame* GetNextVideoFrameInternal(MpegTS_XML &mpts, uint64_t &bytePos, int seekFrame = -1)
{
    AVFrame *pFrame = NULL;
    unsigned int packetSize = mpts.m_mpegTSDescriptor.packetSize;
    uint8_t *buffer = (uint8_t*) alloca(packetSize +  4);

    static int currentFrame = 0;

    if(-1 != seekFrame)
        currentFrame = seekFrame;

    while(1)
    {
        if(currentFrame > mpts.m_videoAccessUnitsDecode.size()-1)
            return NULL;

        unsigned int numBytes = 0;

        for(auto aue : mpts.m_videoAccessUnitsDecode[currentFrame].accessUnitElements)
        {
            // Seek to aue.startByteLocation
            fseek(inputFile, aue.startByteLocation, SEEK_SET);

            for(unsigned int i=0; i<aue.numPackets; i++)
            {
                fread(buffer, packetSize, 1, inputFile);

                uint8_t count = FindData(buffer, packetSize);

                if(count)
                {
                    memcpy(g_pVideoData+numBytes, buffer+count, packetSize-count);
                    numBytes += packetSize-count;
                }
            }
        }

        AVPacket packet;
        av_init_packet(&packet);
        
        if(mpts.m_videoAccessUnitsDecode[currentFrame].frameType == "I")
            packet.flags = AV_PKT_FLAG_KEY;

        //memset(&packet, 0, sizeof(packet));

        packet.buf = av_buffer_alloc(numBytes);
        memcpy(packet.buf->data, g_pVideoData, numBytes);

        packet.data = packet.buf->data;
        packet.size = packet.buf->size;
        packet.dts = mpts.m_videoAccessUnitsDecode[currentFrame].dts;
        packet.pts = mpts.m_videoAccessUnitsDecode[currentFrame].pts;
        packet.pos = mpts.m_videoAccessUnitsDecode[currentFrame].accessUnitElements[0].startByteLocation;

        currentFrame++;

        int streamIndex = packet.stream_index;

        //If the packet is from the video stream
        //if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // Send a packet to the decoder
            int ret = avcodec_send_packet(g_stream_ctx[streamIndex].dec_ctx, &packet);

            // Unref the packet
            av_packet_unref(&packet);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            pFrame = av_frame_alloc();

            if (!pFrame)
            {
                av_log(NULL, AV_LOG_ERROR, "Decode thread could not allocate frame\n");
                ret = AVERROR(ENOMEM);
                break;
            }

            // Get a frame from the decoder
            ret = avcodec_receive_frame(g_stream_ctx[streamIndex].dec_ctx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                av_frame_free(&pFrame);
                continue;
            }
            else if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                av_frame_free(&pFrame);
                break;
            }

            if (ret >= 0)
                break;
        }
    }

    if(pFrame)
        FlipAvFrame(pFrame);

    return pFrame;
}

static AVFrame* GetNextVideoFrame()
{
    AVPacket packet;
    AVFrame *pFrame = NULL;

    while(av_read_frame(g_ifmt_ctx, &packet) >= 0)
    {
        int streamIndex = packet.stream_index;

        //If the packet is from the video stream
        if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // Send a packet to the decoder
            int ret = avcodec_send_packet(g_stream_ctx[streamIndex].dec_ctx, &packet);

            // Unref the packet
            av_packet_unref(&packet);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            pFrame = av_frame_alloc();

            if (!pFrame)
            {
                av_log(NULL, AV_LOG_ERROR, "Decode thread could not allocate frame\n");
                ret = AVERROR(ENOMEM);
                break;
            }

            // Get a frame from the decoder
            ret = avcodec_receive_frame(g_stream_ctx[streamIndex].dec_ctx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                av_frame_free(&pFrame);
                continue;
            }
            else if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                av_frame_free(&pFrame);
                break;
            }

            if (ret >= 0)
                break;
        }
    }

    if(pFrame)
        FlipAvFrame(pFrame);

    return pFrame;
}

static void DoSeekTest()
{
    int videoStreamIndex = -1;
    float duration = 0;

    for(unsigned int streamIndex = 0; streamIndex < g_ifmt_ctx->nb_streams; streamIndex++)
        if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            AVStream *stream = g_ifmt_ctx->streams[streamIndex];
            duration = (float) stream->duration;
            videoStreamIndex = streamIndex;
            break;
        }

    ImGui_ImplGlfwGL3_NewFrame();

    ImGui::SetNextWindowContentSize(ImVec2(50.f, 2.f));
    ImGui::Begin("Seek bar"); // Create a window called "Seek bar" and append into it.

    static int seekValueLast = 0;
    int seekValue = seekValueLast;
    ImGui::SliderInt("Seek", &seekValue, 1, 100);

    if(seekValueLast != seekValue)
    {
        seekValueLast = seekValue;

        float percent = (float) seekValue / 100.f;
        uint64_t seekTS = (uint64_t) (duration * percent);

        avformat_flush(g_ifmt_ctx);
        avio_flush(g_ifmt_ctx->pb);
        avformat_seek_file(g_ifmt_ctx, videoStreamIndex, seekTS, seekTS, seekTS, 0);

        if(g_pFrame)
            av_frame_free(&g_pFrame);

        g_pFrame = GetNextVideoFrame();
    }

    bool bNewFrame = true;
    if(g_pFrame)
        WriteFrame(g_stream_ctx[videoStreamIndex].dec_ctx, g_pFrame, 0, g_pTexturePresenter, bNewFrame);

    ImGui::End();
}

static int64_t BytePosOfLastAU(std::vector<AccessUnit> &accessUnitList)
{
    AccessUnit lastAccessUnit = accessUnitList.back();
    return lastAccessUnit.accessUnitElements[0].startByteLocation;
}

static bool RunGUI(MpegTS_XML &mpts)
{
    static PlayState g_playState = eStopped;

    std::string windowTitle = "Mpeg2-Ts Parser GUI: ";
    windowTitle += mpts.m_mpegTSDescriptor.fileName;
    if(0 != InitOpenGL(windowTitle))
        return false;

    g_pTexturePresenter = new TexturePresenter(g_window, "");

    if(NULL == g_pTexturePresenter)
    {
        CloseOpenGL();
        return false;
    }

    unsigned int err = GLFW_NO_ERROR;

    ImGui::CreateContext();
	ImGui_ImplGlfwGL3_Init(g_window, true);
	ImGui::StyleColorsDark();

    int ret = 0;
    int frame_num = 0;
    bool bNeedFrame = false;

    unsigned int frameDisplaying = 0;
    uint64_t fileBytePos = 0;
    size_t framesToDecode = 0;
    size_t framesDecoded = 0;

    float red = 114.f;
    float redInc = -3.f;
    float green = 144.f;
    float greenInc = 5.f;
    float blue = 154.f;
    float blueInc = 7.f;

    unsigned int numVideoFrames = mpts.m_videoAccessUnitsPresentation.size()-1;
    size_t bytePosOfLastAU = BytePosOfLastAU(mpts.m_videoAccessUnitsPresentation);

    Renderer renderer;

#if DUMP_OUTPUT_FILE
    WriteAllFramesToFile(mpts);
    return true;
#endif

#define FFMPEG_DEMUX 0
#if FFMPEG_DEMUX
    avformat_seek_file(g_ifmt_ctx, mpts.m_videoStreamIndex, 0, 0, 0, AVSEEK_FLAG_BYTE);
    g_pFrame = GetNextVideoFrame();
#else
    g_pFrame = GetNextVideoFrameInternal(mpts, fileBytePos);
#endif

    if(!g_pFrame)
    {
        fprintf(stderr, "Error: Unable to decode %s\n", mpts.m_mpegTSDescriptor.fileName.c_str());
        return false;
    }

    bool bNewFrame = true;

    while(!glfwWindowShouldClose(g_window))
    {
		glClearColor(0.f, 0.f, 0.f, 1.f);
        renderer.Clear();

#define RUN_TEST 0
#if RUN_TEST
        DoSeekTest();
#else

        ImGui_ImplGlfwGL3_NewFrame();

        ImGui::Begin("Playback Controls");

        static int seekValueLast = 0;
        int seekValue = seekValueLast;
        ImGui::SliderInt("##Seek", &seekValue, 0, 100);

        ImGui::SameLine();

//        ImVec4 color = ImVec4(red/255.0f, green/255.0f, blue/255.0f, 1.f);
//        ImGui::ColorButton("ColorButton", *(ImVec4*)&color, 0x00020000, ImVec2(20,20));

        // Keyboard or Play Button
        if ((ImGui::GetIO().KeysDownDuration[32] > 0.f && ImGui::GetIO().KeysDownDuration[32] < 0.04f) ||
            ImGui::ArrowButton("Play", ImGuiDir_Right))
        {
            if(eStopped == g_playState)
            {
                framesToDecode = numVideoFrames - frameDisplaying;
                g_playState = ePlaying;
                bNeedFrame = true;
            }
            else
            {
                g_playState = eStopped;
                bNeedFrame = false;
            }
        }

        // Right Arrow Key
        if (ImGui::IsKeyPressed(KEY_RIGHT_ARROW))
        {
            g_playState = eStopped;
            bNeedFrame = true;
            framesDecoded = 0;
            framesToDecode = 1;
        }

        bool bForceSeek = false;

        // Left Arrow Key
        if (ImGui::IsKeyPressed(KEY_LEFT_ARROW))
        {
            seekValue--;
            seekValue = MAX(0, seekValue);

            if(seekValueLast == seekValue &&
               frameDisplaying != 0)
                bForceSeek = true;
        }

        fileBytePos = (uint64_t) ((float) bytePosOfLastAU * ((float) seekValue / 100.f));

        static size_t lastFileBytePos = 0;

        if(seekValueLast != seekValue ||
           bForceSeek)
        {
            g_playState = eStopped;
            seekValueLast = seekValue;
            lastFileBytePos = fileBytePos;

            //avformat_flush(g_ifmt_ctx);
            //avio_flush(g_ifmt_ctx->pb);

            /* Timestamp based testing with cars_2_toy_story
            size_t lastTimestamp = 33495336;
            size_t timeStampPos = (size_t) ((float) lastTimestamp * ((float) seekValue / 100.f));
            av_seek_frame(g_ifmt_ctx, mpts.m_videoStreamIndex, timeStampPos, 0);
            */

            if(g_pFrame)
                av_frame_free(&g_pFrame);

#if FFMPEG_DEMUX
            frameDisplaying =  FrameNumberFromBytePos(fileBytePos, mpts.m_videoAccessUnitsPresentation);
            avformat_seek_file(g_ifmt_ctx, mpts.m_videoStreamIndex, fileBytePos, fileBytePos, fileBytePos, AVSEEK_FLAG_BYTE);
            g_pFrame = GetNextVideoFrame();
#else
            FlushDecoder();
            //frameDisplaying = FrameNumberFromBytePosInternal(fileBytePos, mpts.m_videoAccessUnitsPresentation);
            frameDisplaying = FrameNumberFromBytePosInternal(fileBytePos, mpts.m_videoAccessUnitsDecode);
            g_pFrame = GetNextVideoFrameInternal(mpts, fileBytePos, frameDisplaying);
#endif

            printf("----------\n");

            while(g_pFrame && AV_PICTURE_TYPE_I != g_pFrame->pict_type)
            {
                switch(g_pFrame->pict_type)
                {
                    case AV_PICTURE_TYPE_P:
                        printf("%d: P Frame\n", frameDisplaying);
                    break;

                    case AV_PICTURE_TYPE_B:
                        printf("%d: B Frame\n", frameDisplaying);
                    break;
                }

                frameDisplaying++;

                av_frame_free(&g_pFrame);

#if FFMPEG_DEMUX
                g_pFrame = GetNextVideoFrame();
#else
                g_pFrame = GetNextVideoFrameInternal(mpts, fileBytePos);
#endif
            }

            bNeedFrame = false;
            bNewFrame = true;
        }
        else
        {
            // If still frames to read
            if(bNeedFrame)
            {
                if(g_pFrame)
                    av_frame_free(&g_pFrame);

#if FFMPEG_DEMUX
                g_pFrame = GetNextVideoFrame();
#else
                g_pFrame = GetNextVideoFrameInternal(mpts, fileBytePos);
#endif

                if (g_pFrame)
                {
                    framesDecoded++;

                    //IncAndClamp(red, redInc, 0.f, 255.f);
                    //IncAndClamp(green, greenInc, 0.f, 255.f);
                    //IncAndClamp(blue, blueInc, 0.f, 255.f);

                    frameDisplaying++;

                    if(framesDecoded == framesToDecode)
                        bNeedFrame = false;

                    float percent = ((float) frameDisplaying / (float)numVideoFrames) *100.f;
                    seekValue = (int) percent;
                    seekValueLast = seekValue;
                    bNewFrame = true;
                }
            }
        }

        if(g_pFrame)
            WriteFrame(g_stream_ctx[mpts.m_videoStreamIndex].dec_ctx, g_pFrame, 0, g_pTexturePresenter, bNewFrame);

        char frameType = ' ';

        if(g_pFrame)
        {
            switch(g_pFrame->pict_type)
            {
                case AV_PICTURE_TYPE_I:
                    frameType = 'I';
                break;
                case AV_PICTURE_TYPE_P:
                    frameType = 'P';
                break;
                case AV_PICTURE_TYPE_B:
                    frameType = 'B';
                break;
            }
        }

        ImGui::Text("Displaying:%d of %d, Type:%c, Seek Frame:%d, Seek Byte:%llu", frameDisplaying, numVideoFrames, frameType, frameDisplaying, (int64_t) fileBytePos);

        ImGui::End(); // Seek

        ImGui::Begin("Frames");

        unsigned int frame = frameDisplaying;

        unsigned int high = frame + 30; // TODO: Make this GOP size

        high = MIN(high, numVideoFrames);

        if(mpts.m_videoAccessUnitsPresentation.size())
//        if(mpts.m_videoAccessUnitsDecode.size())
        {
            if (ImGui::CollapsingHeader(mpts.m_videoAccessUnitsPresentation[0].esd.name.c_str()))
//            if (ImGui::CollapsingHeader(mpts.m_videoAccessUnitsDecode[0].esd.name.c_str()))
            {
                for(unsigned int i = frame; i < high; i++)
                {
                    AccessUnit &au = mpts.m_videoAccessUnitsPresentation[i];
//                    AccessUnit &au = mpts.m_videoAccessUnitsDecode[i];

                    size_t numPackets = 0;
                    for (std::vector<AccessUnitElement>::iterator j = au.accessUnitElements.begin(); j < au.accessUnitElements.end(); j++)
                        numPackets += j->numPackets;

                    if(ImGui::TreeNode((void*) au.frameNumber, "Frame:%u, Type:%s, PTS:%ld, Packets:%llu, PID:%ld", au.frameNumber, au.frameType.c_str(), au.pts, numPackets, au.esd.pid))
                    {
                        if (ImGui::SmallButton("View"))
                        {
                            if(frame > frameDisplaying)
                            {
                                framesToDecode = frame - frameDisplaying;
                                bNeedFrame = true;
                                framesDecoded = 0;
                            }
                        };

                        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                        for (std::vector<AccessUnitElement>::iterator j = au.accessUnitElements.begin(); j < au.accessUnitElements.end(); j++)
                        {
                            ImGui::TreeNodeEx((void*)(intptr_t)frame, nodeFlags, "Byte Location:%llu, Num Packets:%llu", j->startByteLocation, j->numPackets);
                        }

                        ImGui::TreePop();
                    }

                    frame++;
                }
            }
        }

/*
        frame = 1;
        if (ImGui::CollapsingHeader("Audio"))
        {
            for(std::vector<AccessUnit>::iterator i = mpts.m_audioAccessUnits.begin(); i < mpts.m_audioAccessUnits.end(); i++)
            {
                size_t numPackets = 0;
                for (std::vector<AccessUnitElement>::iterator j = i->accessUnitElements.begin(); j < i->accessUnitElements.end(); j++)
                    numPackets += j->numPackets;

                if(ImGui::TreeNode((void*) frame, "Frame:%d, Name:%s, Packets:%d, PID:%ld", frame, i->esd.name.c_str(), numPackets, i->esd.pid))
                {
                    for (std::vector<AccessUnitElement>::iterator j = i->accessUnitElements.begin(); j < i->accessUnitElements.end(); j++)
                    {
                        if (ImGui::TreeNode((void*)(intptr_t)frame, "Byte Location:%d, Num Packets:%d", j->startByteLocation, j->numPackets))
                            ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }

                frame++;
            }
        }
*/

        ImGui::End(); // Frames
#endif

		ImGui::Render();
		ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        /* Swap front and back buffers */
        glfwSwapBuffers(g_window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    if(g_pFrame)
        av_frame_free(&g_pFrame);

    if(g_pTexturePresenter)
        delete g_pTexturePresenter;

  	ImGui_ImplGlfwGL3_Shutdown();
	ImGui::DestroyContext();

    CloseOpenGL();

    return true;
}

static int InitFilter(FilteringContext *fctx,
                      AVStream *st,
                      AVCodecContext *dec_ctx,
                      //AVCodecContext *enc_ctx,
                      const char *filter_spec)
{
    char args[512];
    int ret = 0;

    const AVFilter *cur_filter = NULL;
    AVFilterContext *cur_ctx = NULL;
    AVFilterContext *prev_ctx = NULL;

//    AVCodecContext *codec_ctx;
//    ret = avcodec_parameters_to_context(codec_ctx, st->codecpar);

    AVFilterGraph *filter_graph = avfilter_graph_alloc();
    if (!filter_graph)
    {
        av_log(NULL, AV_LOG_ERROR, "Can not create filter graph\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    fctx->filter_graph = filter_graph;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        // Create Video Source
        //////////////////////
        cur_filter = avfilter_get_by_name("buffer");

        if (!cur_filter)
        {
            av_log(NULL, AV_LOG_ERROR, "Filter video source not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
            st->codecpar->width, st->codecpar->height,
            st->codecpar->format,
            st->time_base.num, st->time_base.den,
            st->codecpar->sample_aspect_ratio.num, st->codecpar->sample_aspect_ratio.den,
            st->r_frame_rate.num, st->r_frame_rate.den);

        ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "src",
                                           args, NULL, filter_graph);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot create video source\n");
            goto end;
        }

        fctx->buffersrc_ctx = cur_ctx;
        prev_ctx = cur_ctx;

        // dec_ctx num and den are flipped at this point, store into dst un-flipped
        AVRational dst;
        dst.num = dec_ctx->time_base.den;
        dst.den = dec_ctx->time_base.num;

#define DO_DEINTERLACING 0
#if DO_DEINTERLACING

        // Perform deinterlacing?
        /////////////////////////
        if( IsDeinterlacing(fr_code) )
        {
            cur_filter = avfilter_get_by_name("yadif");

            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter yadif not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "yadif",
                                               NULL, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create yadif filter\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link yadif filter\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

#define DO_FRAME_RATE_CONVERSION 0
#if DO_FRAME_RATE_CONVERSION

        // Perform frame rate conversion?
        /////////////////////////////////
        if(st->r_frame_rate.num != dst.num ||
           st->r_frame_rate.den != dst.den)
        {
            /*
            st->codec->field_order

            enum AVFieldOrder {
                AV_FIELD_UNKNOWN,
                AV_FIELD_PROGRESSIVE,
                AV_FIELD_TT,          //< Top coded_first, top displayed first
                AV_FIELD_BB,          //< Bottom coded first, bottom displayed first
                AV_FIELD_TB,          //< Top coded first, bottom displayed first
                AV_FIELD_BT,          //< Bottom coded first, top displayed first
            };
            */

            // All of the following code is based on CSourceAssembly::ConfigureAVISynthFRConverter()

            /*
            if (m_bSourceIsVFR)
            {
                // TODO: For the time being, don't convert VFR between PAL and NTSC.
                // Currently the VFR->CFR is done in the MC FR Converter.
                // Converting between NTSC/PAL here would lose sync.
                m_hr = E_INVALIDARG;
                SetError(ERR_UnsupportedFrameRate, L"BuildVideoPreprocessingPath", L"VFR unsupported in PAL<->NTSC conversion");
                goto ConfigureAVISynthFRConverter_Failed;
            }
            */

            // TODO: Pull this in from the json.
            bool bIsTelecine = false;
            EFrameRateConversionCode fr_code = CalculateFrameRateConversion(st->r_frame_rate, dst, bIsTelecine);

            if(fr_code == kNTSCInverseTelecine_to_PAL ||
               fr_code == kNTSCInverseTelecine_to_NTSCFilm ||
               fr_code == kNTSC60pInverseTelecine_to_NTSCFilm ||
               fr_code == kNTSC60pInverseTelecine_to_PAL)
            {
                //double frameRate = 23.976;
                if (fr_code == EFrameRateConversionCode::kNTSC60pInverseTelecine_to_NTSCFilm ||
                    fr_code == EFrameRateConversionCode::kNTSC60pInverseTelecine_to_PAL)
                {
                    // TODO
                    //wcscpy_s(script, _countof(script), L"TDecimate(cycleR=3, Cycle=5)");

                    // Insert Decimate
                    /*
                    cur_filter = avfilter_get_by_name("decimate");

                    if (!cur_filter)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Filter decimate not found\n");
                        ret = AVERROR_UNKNOWN;
                        goto end;
                    }

                    ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "decimate",
                                                       NULL, NULL, filter_graph);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot create decimate filter\n");
                        goto end;
                    }

                    ret = avfilter_link(prev_ctx, 0, cur_ctx, 0);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot link decimate filter\n");
                        goto end;
                    }

                    prev_ctx = cur_ctx;
                    */
                }
                else
                {
                    //wcscpy_s(script, _countof(script), L"TFM()TDecimate()");

                    // Insert FieldMatch
                    cur_filter = avfilter_get_by_name("fieldmatch");

                    if (!cur_filter)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Filter fieldmatch not found\n");
                        ret = AVERROR_UNKNOWN;
                        goto end;
                    }

                    ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "fieldmatch",
                                                       NULL, NULL, filter_graph);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot create fieldmatch filter\n");
                        goto end;
                    }

                    ret = avfilter_link(prev_ctx, 0, cur_ctx, 0);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot link fieldmatch filter\n");
                        goto end;
                    }

                    prev_ctx = cur_ctx;

                    // Insert Decimate
                    cur_filter = avfilter_get_by_name("decimate");

                    if (!cur_filter)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Filter decimate not found\n");
                        ret = AVERROR_UNKNOWN;
                        goto end;
                    }

                    ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "decimate",
                                                       NULL, NULL, filter_graph);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot create decimate filter\n");
                        goto end;
                    }

                    ret = avfilter_link(prev_ctx, 0, cur_ctx, 0);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Cannot link decimate filter\n");
                        goto end;
                    }

                    prev_ctx = cur_ctx;
                }

                // Speed up video to 25fps if desired (e.g. NTSC film to PAL conversion)
                if (fr_code == EFrameRateConversionCode::kNTSCInverseTelecine_to_PAL ||
                    fr_code == EFrameRateConversionCode::kNTSC60pInverseTelecine_to_PAL)
                {
                    //wcscat_s(script, _countof(script), L"AssumeFPS(25, 1, sync_audio=false)");
                    //frameRate = 25.0;
                    dst.num = 25;
                    dst.den = 1;
                }
                else
                {
                    dst.num = 24000;
                    dst.den = 1001;
                }

                // Update output frame rate
                // TODO: code review -- move otu to BuildFilterGraph everything that changes the output frame 
                //if (m_pTUM->GetFrameRatePassThru() && m_iTranscodeStage == TranscodeStages::kMainContentStage)
                //    m_pTUM->SetOutputFrameRate(frameRate);
            }
            else if (fr_code == kNTSC60p_to_PAL)
            {
                /*
                // Perform frame rate conversion to 50i
                wcscpy_s(script, _countof(script), L"ChangeFPS(50.00)\n"); // or blend via ConvertFPS

                                                                           // Re-interlace
                bool isTFF = m_pTUM->IsInterlacedTopFieldFirst();
                wcscat_s(script, _countof(script), (isTFF) ? L"AssumeTFF()\n" : L"AssumeBFF()\n");
                wcscat_s(script, _countof(script), L"SeparateFields()\n");
                wcscat_s(script, _countof(script), L"SelectEvery(4,0,3)\n");
                wcscat_s(script, _countof(script), L"Weave()\n");

                wcscat_s(script, _countof(script), L"AssumeFPS(25, 1, sync_audio=false)");
                */
            }

            cur_filter = avfilter_get_by_name("fps");
            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter fps not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            char fps[32];
            char num[10];
            char den[10];
            snprintf(num, sizeof(num), "%d", dst.num);
            snprintf(den, sizeof(den), "%d", dst.den);
            strcpy(fps, num);
            strcat(fps, "/");
            strcat(fps, den);

            snprintf(args, sizeof(args), "fps=%s", fps);

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "fps",
                                               args, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create fps\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link fps\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

#define DO_AVISYNTH 0
#if DO_AVISYNTH

        // Create avisynth?
        ///////////////////
        if (g_options.avisynth)
        {
            cur_filter = avfilter_get_by_name("avisynth");
            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter avisynth not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            snprintf(args, sizeof(args),
                     "script=%s",
                     g_options.avisynth_script);

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "avisynth",
                                               args, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create avisynth\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link avisynth\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

#define DO_SCALING 0
#if DO_SCALING

        // Create Video Scaler?
        ///////////////////////
        if (enc_ctx->width != st->codecpar->width || enc_ctx->height != st->codecpar->height)
        {
            cur_filter = avfilter_get_by_name("scale");
            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter video_scale not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            snprintf(args, sizeof(args),
                     "width=%d:height=%d",
                     enc_ctx->width, enc_ctx->height);

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "scale",
                                               args, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create video scaler\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link video scaler\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

#define DO_VIDEO_TRIM 0
#if DO_VIDEO_TRIM

        // Create Video Trim?
        /////////////////////
        if (g_options.start_time != -1 || g_options.end_time != -1)
        {
            cur_filter = avfilter_get_by_name("trim");
            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter video_trim not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            memset(args, 0, sizeof(args));

            if(g_options.start_time != -1)
            {
                snprintf(args, sizeof(args),
                         "start=%d",
                         g_options.start_time);
            }

            if(g_options.end_time != -1)
            {
                char end[32];
                snprintf(end, sizeof(end),
                         (g_options.start_time != -1) ? ":end=%d" : "end=%d",
                         g_options.end_time);
                strcat(args, end);
            }

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "video_trim",
                                               args, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create video trim\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link video trim\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

        // Create Video Sink
        ////////////////////
        cur_filter = avfilter_get_by_name("buffersink");
        if (!cur_filter)
        {
            av_log(NULL, AV_LOG_ERROR, "Filter video sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "sink",
                                           NULL, NULL, filter_graph);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot create video sink\n");
            goto end;
        }

        ret = av_opt_set_bin(cur_ctx, "pix_fmts",
                             (uint8_t*)&dec_ctx->pix_fmt, sizeof(dec_ctx->pix_fmt),
                             //(uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                             AV_OPT_SEARCH_CHILDREN);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }

        ret = avfilter_link(prev_ctx, 0,
                            cur_ctx, 0);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot link video sink\n");
            goto end;
        }

        fctx->buffersink_ctx = cur_ctx;
    }
    else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        // Create the audio source
        //////////////////////////
        cur_filter = avfilter_get_by_name("abuffer");
        if (!cur_filter)
        {
            av_log(NULL, AV_LOG_ERROR, "Filter audio source not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%llx",
                 st->time_base.num, st->time_base.den, st->codecpar->sample_rate,
                 av_get_sample_fmt_name((AVSampleFormat) st->codecpar->format),
                 st->codecpar->channel_layout);

        ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "src",
                                           args, NULL, filter_graph);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio source\n");
            goto end;
        }

        fctx->buffersrc_ctx = cur_ctx;
        prev_ctx = cur_ctx;

#define DO_AUDIO_TRIM 0
#if DO_AUDIO_TRIM
        // Create audio trim?
        /////////////////////
        if (g_options.start_time != -1 || g_options.end_time != -1)
        {
            cur_filter = avfilter_get_by_name("atrim");
            if (!cur_filter)
            {
                av_log(NULL, AV_LOG_ERROR, "Filter audio_trim not found\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            memset(args, 0, sizeof(args));

            if(g_options.start_time != -1)
            {
                snprintf(args, sizeof(args),
                         "start=%d",
                         g_options.start_time);
            }

            if(g_options.end_time != -1)
            {
                char end[32];
                snprintf(end, sizeof(end),
                         (g_options.start_time != -1) ? ":end=%d" : "end=%d",
                         g_options.end_time);
                strcat(args, end);
            }

            ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "audio_trim",
                                               args, NULL, filter_graph);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot create audio trim\n");
                goto end;
            }

            ret = avfilter_link(prev_ctx, 0,
                                cur_ctx, 0);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Cannot link audio trim\n");
                goto end;
            }

            prev_ctx = cur_ctx;
        }
#endif

        // Create the audio sink
        ////////////////////////
        cur_filter = avfilter_get_by_name("abuffersink");
        if (!cur_filter)
        {
            av_log(NULL, AV_LOG_ERROR, "Filter audio sink not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avfilter_graph_create_filter(&cur_ctx, cur_filter, "sink",
                                           NULL, NULL, filter_graph);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio sink\n");
            goto end;
        }

        ret = avfilter_link(prev_ctx, 0,
                            cur_ctx, 0);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot link audio sink\n");
            goto end;
        }

        fctx->buffersink_ctx = cur_ctx;
    }
    else
    {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    return ret;
}

static int InitFilters(void)
{
    const char *filter_spec;
    unsigned int i;
    int ret;

    g_filter_ctx = (FilteringContext *)av_malloc_array(g_ifmt_ctx->nb_streams, sizeof(*g_filter_ctx));

    if (!g_filter_ctx)
        return AVERROR(ENOMEM);

#ifdef DEBUG
    char p_graph[2048] = {0};
#endif

    for (i = 0; i < g_ifmt_ctx->nb_streams; i++)
    {
        g_filter_ctx[i].buffersrc_ctx = NULL;
        g_filter_ctx[i].buffersink_ctx = NULL;
        g_filter_ctx[i].filter_graph = NULL;

        if (!(g_ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
            || g_ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;

        if (g_ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            filter_spec = "null"; /* passthrough (dummy) filter for video */
        else
            filter_spec = "anull"; /* passthrough (dummy) filter for audio */

        ret = InitFilter(&g_filter_ctx[i],
                          g_ifmt_ctx->streams[i],
                          //g_stream_ctx[i].enc_ctx,
                          g_stream_ctx[i].dec_ctx,
                          filter_spec);

        if (ret)
            return ret;

#ifdef DEBUG
        strcpy(p_graph, avfilter_graph_dump(g_filter_ctx[i].filter_graph, NULL));
#endif
    }

    return 0;
}

// It all starts here
int main(int argc, char* argv[])
{
//    DoMyXMLTest(argv[1]);
//    DoMyXMLTest2();
//    return 0;

/*
    if(1)
    {
        DoFFMpegOpenGLTest("F:\\streams\\mpeg2\\muppets_take_manhattan.mpg");
        return 0;
    }
*/

    if (0 == argc)
    {
        fprintf(stderr, "Usage: %s input.xml\n", argv[0]);
        fprintf(stderr, "  The file input.xml is generated by mp2ts_parser\n");
        return 1;
    }

    printf("%s: Opening and analyzing %s, this can take a while...\n", argv[0], argv[1]);

    // Open the source xml file that describes the MP2TS
    tinyxml2::XMLDocument doc;
	tinyxml2::XMLError xmlError = doc.LoadFile(argv[1]);

    if(tinyxml2::XML_SUCCESS != xmlError)
    {
        fprintf(stderr, "Error: TinyXml2 could not open file: %s\n", argv[1]);
        return 1;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("file");

    if(nullptr == root)
    {
        fprintf(stderr, "Error: %s does not contain a <file> element at the start!\n", argv[1]);
        return 1;
    }

    MpegTS_XML mpts;

    mpts.ParsePMT(root);

    // Get simple info about the file
    mpts.ParseMpegTSDescriptor(root);

    // Build current access units
    mpts.ParsePacketList(root);

    if(0 != OpenInputFile(mpts))
        return 1;

    inputFile = fopen(mpts.m_mpegTSDescriptor.fileName.c_str(), "rb");

    if(0 != InitFilters())
        return 1;

    // Show as GUI
    if(RunGUI(mpts))
    {
        CloseInputFile(mpts);
        fclose(inputFile);
        return 0;
    }

    return 1;
}