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
extern int DoFFMpegOpenGLTest(const std::string &inFileName);

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 900

static GLFWwindow* g_window = NULL;

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
//    AVCodecContext *enc_ctx;
    AVAudioFifo *audio_fifo;
} StreamContext;

// Describes the input file
static AVFormatContext     *g_ifmt_ctx = NULL;
static StreamContext       *g_stream_ctx = NULL;

static char g_error[AV_ERROR_MAX_STRING_SIZE] = {0};
static double g_total_duration = 0;
static double g_total_frames = 0;

static int OpenInputFile(const std::string &inFileName)
{
    int ret;
    unsigned int i;
    g_ifmt_ctx = NULL;

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

    for (i = 0; i < g_ifmt_ctx->nb_streams; i++)
    {
        AVStream *stream = g_ifmt_ctx->streams[i];
        
        if(stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;

        AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx = NULL;

        if (!dec)
            continue;

        codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", i);
            return AVERROR(ENOMEM);
        }

        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                "for stream #%u\n", i);
            return ret;
        }

        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
            || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                codec_ctx->framerate = av_guess_frame_rate(g_ifmt_ctx, stream, NULL);

                //if(g_options.start_time == -1 &&
                //   g_options.end_time == -1)
                    g_total_duration = (double) g_ifmt_ctx->duration / AV_TIME_BASE;
                //else
                //{
                //    double start_time = g_options.start_time == -1 ? 0 : g_options.start_time;
                //    double end_time = g_options.end_time == -1 ? ((double) g_ifmt_ctx->duration / AV_TIME_BASE) : g_options.end_time;
                //    g_total_duration = end_time - start_time;
                //}

                double fps = (double) stream->avg_frame_rate.num / stream->avg_frame_rate.den;
                g_total_frames = g_total_duration / (1.0 / fps);
            }

			// Just for debugging, two fields which state frame rate
            //double frame_rate = stream->r_frame_rate.num / (double)stream->r_frame_rate.den;
            //frame_rate = stream->avg_frame_rate.num / (double)stream->avg_frame_rate.den;

            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }

        g_stream_ctx[i].dec_ctx = codec_ctx;
    }

    av_dump_format(g_ifmt_ctx, 0, inFileName.c_str(), 0);
    return 0;
}

static int InitOpenGL()
{
    /* Initialize the library */
    if(!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    /* Create a windowed mode window and its OpenGL context */
    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Mpeg2-TS Parser GUI", NULL, NULL);
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

static bool WriteFrame(AVCodecContext *dec_ctx,
                       AVFrame *frame,
                       int frame_num,
                       TexturePresenter *pTexturePresenter)
{
    uint8_t *dst_data[4];
    int dst_linesize[4];
    enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGBA;
    struct SwsContext *sws_ctx = NULL;

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

	pTexturePresenter->Render(dst_data[0], dec_ctx->width, dec_ctx->height);

    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);

    return true;
}

#pragma warning(disable:4996)

bool RunGUI(MpegTS_XML &mpts)
{
    av_register_all();

    avfilter_register_all();

    av_log_set_level(AV_LOG_QUIET);

    if(0 != OpenInputFile(mpts.m_mpegTSDescriptor.fileName))
        return false;

    if(0 != InitOpenGL())
        return false;

    unsigned int err = GLFW_NO_ERROR;

	ImGui::CreateContext();
	ImGui_ImplGlfwGL3_Init(g_window, true);
	ImGui::StyleColorsDark();

    TexturePresenter *pTexturePresenter = new TexturePresenter(g_window, "");

    AVPacket packet;

    int ret = 0;
    int frame_num = 0;
    bool bNeedFrame = true;

    AVFrame *showFrame = NULL;
    int videoStreamIndex = 0;
    int frameNumberToDisplay = 1;
    int framesDecoded = 0;

    Renderer renderer;

    while(!glfwWindowShouldClose(g_window))
    {
		glClearColor(0.f, 0.f, 0.f, 1.f);
        renderer.Clear();

        // If still frames to read
        if(bNeedFrame)
        {
            if(av_read_frame(g_ifmt_ctx, &packet) >= 0)
            {
                int streamIndex = packet.stream_index;

                //If the packet is from the video stream
                if(g_ifmt_ctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    videoStreamIndex = streamIndex;

                    // Send a packet to the decoder
                    ret = avcodec_send_packet(g_stream_ctx[streamIndex].dec_ctx, &packet);

                    // Unref the packet
                    av_packet_unref(&packet);

                    if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                        break;
                    }

                    AVFrame *frame = av_frame_alloc();

                    if (!frame)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Decode thread could not allocate frame\n");
                        ret = AVERROR(ENOMEM);
                        break;
                    }

                    // Get a frame from the decoder
                    ret = avcodec_receive_frame(g_stream_ctx[streamIndex].dec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        av_frame_free(&frame);
                        continue;
                    }
                    else if (ret < 0)
                    {
                        av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                        av_frame_free(&frame);
                        break;
                    }

                    if (ret >= 0)
                    {
                        framesDecoded++;

                        if(framesDecoded == frameNumberToDisplay)
                        {
                            if(showFrame)
                                av_frame_free(&showFrame);

                            showFrame = av_frame_clone(frame);

                            // We need to flip the video image
                            //  See: https://lists.ffmpeg.org/pipermail/ffmpeg-user/2011-May/000976.html

                            showFrame->data[0] += showFrame->linesize[0] * (showFrame->height - 1);
                            showFrame->linesize[0] = -(showFrame->linesize[0]);

                            showFrame->data[1] += showFrame->linesize[1] * ((showFrame->height/2) - 1);
                            showFrame->linesize[1] = -(showFrame->linesize[1]);

                            showFrame->data[2] += showFrame->linesize[2] * ((showFrame->height/2) - 1);
                            showFrame->linesize[2] = -(showFrame->linesize[2]);

                            bNeedFrame = false;
                        }
                    }
                }
            }
        }

        if(showFrame)
            WriteFrame(g_stream_ctx[videoStreamIndex].dec_ctx, showFrame, 0, pTexturePresenter);

        ImGui_ImplGlfwGL3_NewFrame();

        unsigned int frame = 1;

        int node_clicked = -1;                // Temporary storage of what node we have clicked to process selection at the end of the loop. May be a pointer to your own node type, etc.

        if (ImGui::CollapsingHeader("Video"))
        {
            for(std::vector<AccessUnit>::iterator i = mpts.m_videoAccessUnits.begin(); i < mpts.m_videoAccessUnits.end(); i++)
            {
                size_t numPackets = 0;
                for (std::vector<AccessUnitElement>::iterator j = i->accessUnitElements.begin(); j < i->accessUnitElements.end(); j++)
                    numPackets += j->numPackets;

                if(ImGui::TreeNode((void*) frame, "Frame:%d, Name:%s, Packets:%d, PID:%ld", frame, i->esd.name.c_str(), numPackets, i->esd.pid))
                {
                    if (ImGui::SmallButton("View"))
                    {
                        if(frame != frameNumberToDisplay)
                        {
                            //av_seek_frame(g_ifmt_ctx, videoStreamIndex, 0, 0);
                            avformat_flush(g_ifmt_ctx);
                            avformat_seek_file(g_ifmt_ctx, videoStreamIndex, 0, 0, 0, AVSEEK_FLAG_BYTE);
                            //avformat_seek_file(g_ifmt_ctx, videoStreamIndex, frame, frame, frame, AVSEEK_FLAG_FRAME);
                            frameNumberToDisplay = frame;
                            framesDecoded = 0;
                            bNeedFrame = true;
                        }
                    };

                    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                    //if (ImGui::IsItemFocused())
                    //{
                    //    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    //    printf("Video frame:%d selected, Name:%s, Packets:%d, PID:%ld\n", frame, i->esd.name.c_str(), numPackets, i->esd.pid);
                    //}

                    for (std::vector<AccessUnitElement>::iterator j = i->accessUnitElements.begin(); j < i->accessUnitElements.end(); j++)
                    {
                        //if (ImGui::TreeNode((void*)(intptr_t)frame, "Byte Location:%d, Num Packets:%d, Packet Size:%d", j->startByteLocation, j->numPackets, j->packetSize))
                        //    ImGui::TreePop();
                        ImGui::TreeNodeEx((void*)(intptr_t)frame, nodeFlags, "Byte Location:%d, Num Packets:%d, Packet Size:%d", j->startByteLocation, j->numPackets, j->packetSize);
                    }

                    ImGui::TreePop();
                }

                frame++;
            }
        }

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
                        if (ImGui::TreeNode((void*)(intptr_t)frame, "Byte Location:%d, Num Packets:%d, Packet Size:%d", j->startByteLocation, j->numPackets, j->packetSize))
                            ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }

                frame++;
            }
        }

		ImGui::Render();
		ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        /* Swap front and back buffers */
        glfwSwapBuffers(g_window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    av_frame_free(&showFrame);

  	ImGui_ImplGlfwGL3_Shutdown();
	ImGui::DestroyContext();

    CloseOpenGL();

    return true;
}

// It all starts here
int main(int argc, char* argv[])
{
//    DoMyXMLTest(argv[1]);

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
        return 0;
    }

/*
    for (int i = 0; i < argc - 1; i++)
    {
        if (0 == strcmp("-d", argv[i]))
            g_b_debug = true;

        if (0 == strcmp("-g", argv[i]))
            g_b_gui = true;

        if (0 == strcmp("-p", argv[i]))
            g_b_progress = true;

        if(0 == strcmp("-x", argv[i]))
            g_b_xml = true;
    }
*/

    printf("%s: Opening and analyzing %s, this can take a while...\n", argv[0], argv[1]);

    tinyxml2::XMLDocument doc;
	tinyxml2::XMLError xmlError = doc.LoadFile(argv[1]);

    if(tinyxml2::XML_SUCCESS != xmlError)
    {
        fprintf(stderr, "Error: TinyXml2 could not open file: %s\n", argv[1]);
        return 0;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("file");

    MpegTS_XML mpts;

    mpts.ParsePMT(root);

    // Get simple info about the file
    mpts.ParseMpegTSDescriptor(root);

    // Build current access units
    mpts.ParsePacketList(root);

    // Show as GUI
    if(RunGUI(mpts))
        return 0;

    return 1;
}