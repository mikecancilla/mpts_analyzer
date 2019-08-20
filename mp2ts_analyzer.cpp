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

// Describes the input file
static AVFormatContext  *g_ifmt_ctx = NULL;
static StreamContext    *g_stream_ctx = NULL;
static AVFrame          *g_pFrame = NULL;
static GLFWwindow       *g_window = NULL;
static TexturePresenter *g_pTexturePresenter = NULL;

static char             g_error[AV_ERROR_MAX_STRING_SIZE] = {0};

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

    av_log_set_level(AV_LOG_QUIET);

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

static int FrameNumberFromBytePos(int64_t bytePos, std::vector<AccessUnit> &accessUnitList)
{
    if(0 == bytePos)
        return 0;

    for(std::vector<AccessUnit>::iterator i = accessUnitList.begin(); i < accessUnitList.end(); i++)
    {
        if(i->accessUnitElements[0].startByteLocation >= bytePos)
            return i->frameNumber;
    }

    AccessUnit lastAccessUnit = accessUnitList.back();
    return lastAccessUnit.frameNumber;
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

    int frameDisplaying = 0;
    size_t framesToDecode = 0;
    size_t framesDecoded = 0;

    float red = 114.f;
    float redInc = -3.f;
    float green = 144.f;
    float greenInc = 5.f;
    float blue = 154.f;
    float blueInc = 7.f;

    size_t numVideoFrames = mpts.m_videoAccessUnitsPresentation.size();
    size_t bytePosOfLastAU = BytePosOfLastAU(mpts.m_videoAccessUnitsPresentation);

    Renderer renderer;

    avformat_seek_file(g_ifmt_ctx, mpts.m_videoStreamIndex, 0, 0, 0, AVSEEK_FLAG_BYTE);

    g_pFrame = GetNextVideoFrame();

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
        if (ImGui::IsKeyPressed(262))
        {
            g_playState = eStopped;
            bNeedFrame = true;
            framesDecoded = 0;
            framesToDecode = 1;
        }

        bool bForceSeek = false;

        // Left Arrow Key
        if (ImGui::IsKeyPressed(263))
        {
            seekValue--;
            seekValue = MAX(0, seekValue);

            if(seekValueLast == seekValue &&
               frameDisplaying != 0)
                bForceSeek = true;
        }

        size_t fileBytePos = (size_t) ((float) bytePosOfLastAU * ((float) seekValue / 100.f));

        static size_t lastFileBytePos = 0;

        if(seekValueLast != seekValue ||
           bForceSeek)
        {
            g_playState = eStopped;
            seekValueLast = seekValue;

            frameDisplaying =  FrameNumberFromBytePos(fileBytePos, mpts.m_videoAccessUnitsPresentation);
            lastFileBytePos = fileBytePos;

            avformat_flush(g_ifmt_ctx);
            avio_flush(g_ifmt_ctx->pb);
            avformat_seek_file(g_ifmt_ctx, mpts.m_videoStreamIndex, fileBytePos, fileBytePos, fileBytePos, AVSEEK_FLAG_BYTE);

            /* Timestamp based testing with cars_2_toy_story
            size_t lastTimestamp = 33495336;
            size_t timeStampPos = (size_t) ((float) lastTimestamp * ((float) seekValue / 100.f));
            av_seek_frame(g_ifmt_ctx, mpts.m_videoStreamIndex, timeStampPos, 0);
            */

            if(g_pFrame)
                av_frame_free(&g_pFrame);

            g_pFrame = GetNextVideoFrame();

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
                g_pFrame = GetNextVideoFrame();
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

                g_pFrame = GetNextVideoFrame();

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

        int frame = frameDisplaying;

        size_t high = frame + 30; // TODO: Make this GOP size

        high = MIN(high, numVideoFrames);

        if(mpts.m_videoAccessUnitsPresentation.size())
        {
            if (ImGui::CollapsingHeader(mpts.m_videoAccessUnitsPresentation[0].esd.name.c_str()))
            {
                for(int i = frame; i < high; i++)
                {
                    AccessUnit &au = mpts.m_videoAccessUnitsPresentation[i];

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

    // Show as GUI
    if(RunGUI(mpts))
    {
        CloseInputFile(mpts);
        return 0;
    }

    return 1;
}