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

#include "tinyxml2.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw_gl3.h"

#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

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

#include "Renderer.h"
#include "Texture.h"
#include "VertexBufferLayout.h"

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

/*
Taken from: http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/M2TS.html

M2TS StreamType Values

Value  StreamType                             Value  StreamType
0x0 =  Reserved                               0x12 = MPEG - 4 generic
0x1 =  MPEG-1 Video	                          0x13 = ISO 14496-1 SL-packetized
0x2 =  MPEG-2 Video	                          0x14 = ISO 13818-6 Synchronized Download Protocol
0x3 =  MPEG-1 Audio	                          0x1b = H.264 Video
0x4 =  MPEG-2 Audio	                          0x80 = DigiCipher II Video
0x5 =  ISO 13818-1 private sections           0x81 = A52 / AC-3 Audio
0x6 =  ISO 13818 1 PES private data	          0x82 = HDMV DTS Audio
0x7 =  ISO 13522 MHEG	                      0x83 = LPCM Audio
0x8 =  ISO 13818-1 DSM - CC                   0x84 = SDDS Audio
0x9 =  ISO 13818-1 auxiliary                  0x85 = ATSC Program ID
0xa =  ISO 13818-6 multi - protocol encap     0x86 = DTS-HD Audio
0xb =  ISO 13818-6 DSM - CC U - N msgs        0x87 = E-AC-3 Audio
0xc =  ISO 13818-6 stream descriptors         0x8a = DTS Audio
0xd =  ISO 13818-6 sections                   0x91 = A52b / AC-3 Audio
0xe =  ISO 13818-1 auxiliary                  0x92 = DVD_SPU vls Subtitle
0xf =  MPEG-2 AAC Audio                       0x94 = SDDS Audio
0x10 = MPEG-4 Video                           0xa0 = MSCODEC Video
0x11 = MPEG-4 LATM AAC Audio                  0xea = Private ES(VC-1)
*/

extern void DoMyXMLTest(char *pXMLFile);
extern int DoFFMpegOpenGLTest(const std::string &inFileName);

typedef enum streamTypes
{
    eReserved                                   = 0x0, 
    eMPEG1_Video                                = 0x1, 
    eMPEG2_Video                                = 0x2, 
    eMPEG1_Audio                                = 0x3, 
    eMPEG2_Audio                                = 0x4, 
    eISO13818_1_private_sections                = 0x5, 
    eISO13818_1_PES_private_data                = 0x6, 
    eISO13522_MHEG                              = 0x7, 
    eISO13818_1_DSM_CC                          = 0x8, 
    eISO13818_1_auxiliary                       = 0x9, 
    eISO13818_6_multi_protocol_encap            = 0xa, 
    eISO13818_6_DSM_CC_UN_msgs                  = 0xb, 
    eISO13818_6_stream_descriptors              = 0xc, 
    eISO13818_6_sections                        = 0xd, 
    eISO13818_1_auxiliary2                      = 0xe, 
    eMPEG2_AAC_Audio                            = 0xf, 
    eMPEG4_Video                                = 0x10,
    eMPEG4_LATM_AAC_Audio                       = 0x11,
    eMPEG4_generic                              = 0x12,
    eISO14496_1_SL_packetized                   = 0x13,
    eISO13818_6_Synchronized_Download_Protocol  = 0x14,
    eH264_Video                                 = 0x1b,
    eDigiCipher_II_Video                        = 0x80,
    eA52_AC3_Audio                              = 0x81,
    eHDMV_DTS_Audio                             = 0x82,
    eLPCM_Audio                                 = 0x83,
    eSDDS_Audio                                 = 0x84,
    eATSC_Program_ID                            = 0x85,
    eDTSHD_Audio                                = 0x86,
    eEAC3_Audio                                 = 0x87,
    eDTS_Audio                                  = 0x8a,
    eA52b_AC3_Audio                             = 0x91,
    eDVD_SPU_vls_Subtitle                       = 0x92,
    eSDDS_Audio2                                = 0x94,
    eMSCODEC_Video                              = 0xa0,
    ePrivate_ES_VC1                             = 0xea
} eStreamType;

struct AccessUnitElement
{
    size_t startByteLocation;
    size_t numPackets;
    uint8_t packetSize;

    AccessUnitElement()
        : startByteLocation(-1)
        , numPackets(-1)
        , packetSize(0)
    {
    }

    AccessUnitElement(size_t startByteLocation, size_t numPackets, uint8_t packetSize)
        : startByteLocation(startByteLocation)
        , numPackets(numPackets)
        , packetSize(packetSize)
    {
    }
};

struct ElementaryStreamDescriptor
{
    std::string name;
    eStreamType type;
    long pid;

    ElementaryStreamDescriptor()
        : name("")
        , type(eReserved)
        , pid(-1)
    {
    }

    ElementaryStreamDescriptor(std::string name, eStreamType type, long int pid)
        : name(name)
        , type(type)
        , pid(pid)
    {
    }
};

struct AccessUnit
{
    ElementaryStreamDescriptor esd;

    std::vector<AccessUnitElement> accessUnitElements;
    
    AccessUnit()
    {
    }

    AccessUnit(std::string name, eStreamType type, long int pid)
        : esd(name, type, pid)
    {
    }
};

struct MpegTSDescriptor
{
    std::string fileName;
    uint8_t packetSize;

    MpegTSDescriptor()
        : fileName("")
        , packetSize(0)
    {}
};

class TextureTest
{
public:

    TextureTest(const std::string &imageFileName)
	{
		// Create the shader
		m_Shader = std::make_unique<Shader>("Basic.shader");

		// Bind the shader
		m_Shader->Bind();

        if("" != imageFileName)
		    m_Texture = std::make_unique<Texture>(imageFileName);
        else
            m_Texture = NULL;

		m_Shader->SetUniform1i("u_Texture", 0);

        int w, h;
        glfwGetWindowSize(g_window, &w, &h);
        m_Proj = glm::ortho(0.f, (float) w, 0.f, (float) h, -1.f, 1.f);

        float positions[] = {
            0, 0, 0.f, 0.f, // Bottom Left, 0
			w, 0, 1.f, 0.f, // Bottom Right, 1
			w, h, 1.f, 1.f, // Top Right, 2
            0, h, 0.f, 1.f  // Top Left, 3
		};

		unsigned short indicies[] = {
			0, 1, 2,
			2, 3, 0
		};

		GLCall(glEnable(GL_BLEND));
		GLCall(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		// Create and Bind the vertex array
		m_VAO = std::make_unique<VertexArray>();

		// Create and Bind the vertex buffer
		m_VertexBuffer = std::make_unique<VertexBuffer>(positions, 4 * 4 * sizeof(float));

		// Define the layout of the vertex buffer memory
		VertexBufferLayout layout;
		layout.Push<float>(2);
		layout.Push<float>(2);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);

		// Create and Bind the index buffer
		m_IndexBuffer = std::make_unique<IndexBuffer>(indicies, 6);
    }

    ~TextureTest()
    {
    }

    void Render()
    {
		Renderer renderer;

		m_Texture->Bind();

		glm::mat4 mvp = m_Proj;

        m_Shader->Bind();
		m_Shader->SetUniformMat4f("u_MVP", mvp);
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

    void Render(uint8_t *pFrame, int w, int h)
    {
		Renderer renderer;

        if(NULL == m_Texture)
		    m_Texture = std::make_unique<Texture>(pFrame, w, h);

		m_Texture->Bind();

		glm::mat4 mvp = m_Proj;

        m_Shader->Bind();
		m_Shader->SetUniformMat4f("u_MVP", mvp);
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);

        m_Texture = NULL;
	}

	std::unique_ptr<VertexArray> m_VAO;
	std::unique_ptr<VertexBuffer> m_VertexBuffer;
	std::unique_ptr<IndexBuffer> m_IndexBuffer;
	std::unique_ptr<Shader> m_Shader;
	std::unique_ptr<Texture> m_Texture;
	glm::mat4 m_Proj;
};

class MpegTS
{
public:

    MpegTS()
    : m_bParsedMpegTSDescriptor(false)
    , m_bParsedPMT(false)
    {
    }

    bool ParsePMT(tinyxml2::XMLElement* root)
    {
        bool bFoundPMT = false;

        tinyxml2::XMLElement* element = root->FirstChildElement("packet");

        while(!bFoundPMT && element)
        {
            tinyxml2::XMLElement* pmt = element->FirstChildElement("program_map_table");

            if(pmt)
            {
                tinyxml2::XMLElement* stream = pmt->FirstChildElement("stream");
                while(stream)
                {
                    tinyxml2::XMLElement* type_number = stream->FirstChildElement("type_number");
                    int stream_type = strtol(type_number->GetText(), NULL, 16);

                    AccessUnit *pAU = nullptr;

                    switch(stream_type)
                    {
                        case eMPEG1_Video:
                        case eMPEG2_Video:
                        case eMPEG4_Video:
                        case eH264_Video:
                        case eDigiCipher_II_Video:
                        case eMSCODEC_Video:
                            pAU = &m_currentVideoAU;
                        break;

                        case eMPEG1_Audio:
                        case eMPEG2_Audio:
                        case eMPEG2_AAC_Audio:
                        case eMPEG4_LATM_AAC_Audio:
                        case eA52_AC3_Audio:
                        case eHDMV_DTS_Audio:
                        case eA52b_AC3_Audio:
                        case eSDDS_Audio:
                            pAU = &m_currentAudioAU;
                        break;
                    }

                    if(pAU)
                    {
                        tinyxml2::XMLElement* pid = stream->FirstChildElement("pid");
                        pAU->esd.pid = strtol(pid->GetText(), NULL, 16);

                        tinyxml2::XMLElement* type = stream->FirstChildElement("type_number");
                        pAU->esd.type = (eStreamType) strtol(type->GetText(), NULL, 16);

                        tinyxml2::XMLElement* typeName = stream->FirstChildElement("type_name");
                        pAU->esd.name = typeName->GetText();
                    }

                    stream = stream->NextSiblingElement("stream");
                }
                
                bFoundPMT = true;
            }

            element = element->NextSiblingElement("packet");
        }

        return bFoundPMT;
    }

    bool ParseMpegTSDescriptor(tinyxml2::XMLElement* root)
    {
        tinyxml2::XMLElement* element = root->FirstChildElement("name");
        m_mpegTSDescriptor.fileName = element->GetText();

        element = root->FirstChildElement("packet_size");
        m_mpegTSDescriptor.packetSize = std::atoi(element->GetText());

        if("" == m_mpegTSDescriptor.fileName ||
           0 == m_mpegTSDescriptor.packetSize)
            return false;

        return true;
    }

    bool ParsePacketList(tinyxml2::XMLElement* root)
    {
        if(NULL == root)
            return false;

        tinyxml2::XMLElement* element = nullptr;

        element = root->FirstChildElement("packet");

        long lastPID = -1;

        while(element)
        {
            tinyxml2::XMLElement* pid = element->FirstChildElement("pid");
            long thisPID = strtol(pid->GetText(), NULL, 16);

            AccessUnit *pAU = nullptr;

            if(m_currentVideoAU.esd.pid == thisPID)
                pAU = &m_currentVideoAU;
            else if(m_currentAudioAU.esd.pid == thisPID)
                pAU = &m_currentAudioAU;

            if(pAU)
            {
                tinyxml2::XMLElement* pusi = element->FirstChildElement("payload_unit_start_indicator");

                bool bNewAUSet = false;

                if(1 == strtol(pusi->GetText(), NULL, 16))
                {
                    if(pAU->accessUnitElements.size())
                    {
                        if(pAU == &m_currentVideoAU)
                            m_videoAccessUnits.push_back(m_currentVideoAU);
                        else if(pAU == &m_currentAudioAU)
                            m_audioAccessUnits.push_back(m_currentAudioAU);
                    }

                    pAU->accessUnitElements.clear();
                    bNewAUSet = true;
                }

                if(-1 != lastPID && thisPID != lastPID)
                    bNewAUSet = true;

                if(bNewAUSet)
                {
                    const tinyxml2::XMLAttribute *attribute = element->FirstAttribute();

                    AccessUnitElement aue;
                    aue.startByteLocation = attribute->IntValue();
                    aue.numPackets = 1;
                    aue.packetSize = m_mpegTSDescriptor.packetSize;

                    pAU->accessUnitElements.push_back(aue);
                }
                else
                {
                    AccessUnitElement &aue = pAU->accessUnitElements.back();
                    aue.numPackets++;
                }

                lastPID = thisPID;
            }

            element = element->NextSiblingElement("packet");
        }

        if(m_currentVideoAU.accessUnitElements.size())
        {
            m_videoAccessUnits.push_back(m_currentVideoAU);
            m_currentVideoAU.accessUnitElements.clear();
        }
        
        if(m_currentAudioAU.accessUnitElements.size())
        {
            m_audioAccessUnits.push_back(m_currentAudioAU);
            m_currentAudioAU.accessUnitElements.clear();
        }

        return true;
    }

public:
    MpegTSDescriptor                m_mpegTSDescriptor;
    std::vector<AccessUnit>         m_videoAccessUnits;
    std::vector<AccessUnit>         m_audioAccessUnits;

private:
    AccessUnit                      m_currentVideoAU;
    AccessUnit                      m_currentAudioAU;
    bool                            m_bParsedMpegTSDescriptor = false;
    bool                            m_bParsedPMT = false;
    //std::vector<ElementaryStream> gElementaryStreams;
};

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

static bool WriteFrame(AVCodecContext *dec_ctx, AVFrame *frame, int frame_num,
                TextureTest *pTest)
{
    uint8_t *dst_data[4];
    int dst_linesize[4];
    enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGBA;
    struct SwsContext *sws_ctx = NULL;
    Renderer renderer;

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

	pTest->Render(dst_data[0], dec_ctx->width, dec_ctx->height);

    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);

    return true;
}

#pragma warning(disable:4996)

bool RunGUI(MpegTS &mpts)
{
    av_register_all();

    avfilter_register_all();

    if(0 != OpenInputFile(mpts.m_mpegTSDescriptor.fileName))
        return false;

    if(0 != InitOpenGL())
        return false;

    unsigned int err = GLFW_NO_ERROR;

	ImGui::CreateContext();
	ImGui_ImplGlfwGL3_Init(g_window, true);
	ImGui::StyleColorsDark();

    TextureTest *test = new TextureTest("");

    AVPacket packet;

    int ret = 0;
    int frame_num = 0;
    bool bNeedFrame = true;

    AVFrame *showFrame = NULL;
    int video_stream_index = 0;

    Renderer renderer;

    while(!glfwWindowShouldClose(g_window))
    {
		glClearColor(0.f, 0.f, 0.f, 1.f);
        renderer.Clear();

        // If still frames to read
        if(bNeedFrame && av_read_frame(g_ifmt_ctx, &packet) >= 0)
        {
            int stream_index = packet.stream_index;

            //If the packet is from the video stream
            if(g_ifmt_ctx->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                video_stream_index = stream_index;

                // Send a packet to the decoder
                ret = avcodec_send_packet(g_stream_ctx[stream_index].dec_ctx, &packet);

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
                ret = avcodec_receive_frame(g_stream_ctx[stream_index].dec_ctx, frame);
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

                    //bNeedFrame = false;
                }
            }
        }

        if(showFrame)
            WriteFrame(g_stream_ctx[video_stream_index].dec_ctx, showFrame, 0, test);

        ImGui_ImplGlfwGL3_NewFrame();

        unsigned int frame = 1;

        if (ImGui::CollapsingHeader("Video"))
        {
            for(std::vector<AccessUnit>::iterator i = mpts.m_videoAccessUnits.begin(); i < mpts.m_videoAccessUnits.end(); i++)
            {
                unsigned int numPackets = 0;
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

        frame = 1;
        if (ImGui::CollapsingHeader("Audio"))
        {
            for(std::vector<AccessUnit>::iterator i = mpts.m_audioAccessUnits.begin(); i < mpts.m_audioAccessUnits.end(); i++)
            {
                unsigned int numPackets = 0;
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

    tinyxml2::XMLDocument doc;
	doc.LoadFile(argv[1]);

    tinyxml2::XMLElement* root = doc.FirstChildElement("file");

    MpegTS mpts;

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