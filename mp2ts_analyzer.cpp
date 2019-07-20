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

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 900

static GLFWwindow* g_window = NULL;

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

bool RunGUI(MpegTS &mpts)
{
    unsigned int err = GLFW_NO_ERROR;

    /* Initialize the library */
    if(!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    /* Create a windowed mode window and its OpenGL context */
    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Mpeg2-TS Parser GUI", NULL, NULL);
    if (!g_window)
    {
        fprintf(stderr, "Could not create GL window! Continuing without a GUI!\n");
        glfwTerminate();
        return false;
    }

    /* Make the window's context current */
    glfwMakeContextCurrent(g_window);

    glfwSwapInterval(1);

    if(glewInit() != GLEW_OK)
    {
        fprintf(stderr, "Glew Initialization Error! Continuing without a GUI!\n");
        glfwTerminate();
        return false;
    }

	ImGui::CreateContext();
	ImGui_ImplGlfwGL3_Init(g_window, true);
	ImGui::StyleColorsDark();

    while(!glfwWindowShouldClose(g_window))
    {
		glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

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

  	ImGui_ImplGlfwGL3_Shutdown();
	ImGui::DestroyContext();
    glfwTerminate();

    return true;
}

// It all starts here
int main(int argc, char* argv[])
{
//    DoMyXMLTest(argv[1]);

    if(1)
    {
        DoFFMpegOpenGLTest("F:\\streams\\mpeg2\\muppets_take_manhattan.mpg");
        return 0;
    }

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