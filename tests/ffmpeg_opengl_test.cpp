#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <string>
#include <iostream>

#include "Renderer.h"
#include "Texture.h"
#include "VertexBufferLayout.h"

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

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
//    AVCodecContext *enc_ctx;
    AVAudioFifo *audio_fifo;
} StreamContext;

// Describes the input file
static AVFormatContext     *g_ifmt_ctx = NULL;
//FilteringContext    *g_filter_ctx = NULL;
static StreamContext       *g_stream_ctx = NULL;

static char g_error[AV_ERROR_MAX_STRING_SIZE] = {0};
static double g_total_duration = 0;
static double g_total_frames = 0;

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 900

static GLFWwindow* g_window = NULL;
static int g_textureID = 0;

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

#pragma warning(disable:4996)

static void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
}

static void SaveFrame(uint8_t *pData, int width, int height, int linesize, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(pData+y*linesize, 1, width*3, pFile);

    // Close file
    fclose(pFile);
}

static void DeStride(uint8_t *pData, int width, int height, int linesize, uint8_t *buffer)
{
    //fwrite(pData+y*linesize, 1, width*3, pFile);

    // Write pixel data
    for(int y=0; y<height; y++)
        memcpy(buffer+(y*width*3), pData+(y*linesize), width*3);
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

    // We need to flip the video image
    //  See: https://lists.ffmpeg.org/pipermail/ffmpeg-user/2011-May/000976.html

    frame->data[0] += frame->linesize[0] * (frame->height - 1);
    frame->linesize[0] = -(frame->linesize[0]);

    frame->data[1] += frame->linesize[1] * ((frame->height/2) - 1);
    frame->linesize[1] = -(frame->linesize[1]);

    frame->data[2] += frame->linesize[2] * ((frame->height/2) - 1);
    frame->linesize[2] = -(frame->linesize[2]);

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

	GLCall(glClearColor(0.f, 0.f, 0.f, 1.f));

    renderer.Clear();
	pTest->Render(dst_data[0], dec_ctx->width, dec_ctx->height);

    // Swap front and back buffers
    GLCall(glfwSwapBuffers(g_window));

    // Poll for and process events
    GLCall(glfwPollEvents());

    av_freep(&dst_data[0]);
    sws_freeContext(sws_ctx);

    return true;
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

int DoFFMpegOpenGLTest(const std::string &inFileName)
{
/*
    if(0 != InitOpenGL())
        return -1;

    TextureTest *test = new TextureTest("1930_ford_model_a.jpg");
    Renderer renderer;

    while (!glfwWindowShouldClose(g_window))
    {
		GLCall(glClearColor(0.f, 0.f, 0.f, 1.f));

        renderer.Clear();
		test->Render();

        // Swap front and back buffers
        GLCall(glfwSwapBuffers(g_window));

        // Poll for and process events
        GLCall(glfwPollEvents());
    }

    delete test;

    CloseOpenGL();

    return 0;
*/
    av_register_all();

    avfilter_register_all();

    if(0 != OpenInputFile(inFileName))
        return -1;

    if(0 != InitOpenGL())
        return -1;

    TextureTest *test = new TextureTest("");

    AVPacket packet;

    int ret = 0;
    int frame_num = 0;

    //While still frames to read
    while(av_read_frame(g_ifmt_ctx, &packet) >= 0)
    {
        int stream_index = packet.stream_index;
        AVFrame *frame = av_frame_alloc();

        if (!frame)
        {
            av_log(NULL, AV_LOG_ERROR, "Decode thread could not allocate frame\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        //If the packet is from the video stream
        if(g_ifmt_ctx->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            // Send a packet to the decoder
            ret = avcodec_send_packet(g_stream_ctx[stream_index].dec_ctx, &packet);

            // Unref the packet
            av_packet_unref(&packet);

            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
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
                WriteFrame(g_stream_ctx[stream_index].dec_ctx, frame, frame_num++, test);
        }
    }

    if(g_stream_ctx)
        av_free(g_stream_ctx);

    if(g_ifmt_ctx)
        avformat_close_input(&g_ifmt_ctx);

    delete test;

    CloseOpenGL();

    return ret;
}