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

#include "tinyxml2.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>

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
    long type;
    long pid;

    ElementaryStreamDescriptor()
        : name("")
        , type(-1)
        , pid(-1)
    {
    }

    ElementaryStreamDescriptor(std::string name, long int type, long int pid)
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

    AccessUnit(std::string name, long int type, long int pid)
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
                        case 0x2: // Mpeg2-Video
                            pAU = &m_currentVideoAU;
                        break;
                        case 0x3:
                            pAU = &m_currentAudioAU;
                        break;
                    }

                    if(pAU)
                    {
                        tinyxml2::XMLElement* pid = stream->FirstChildElement("pid");
                        pAU->esd.pid = strtol(pid->GetText(), NULL, 16);

                        tinyxml2::XMLElement* type = stream->FirstChildElement("type_number");
                        pAU->esd.type = strtol(type->GetText(), NULL, 16);

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

private:
    MpegTSDescriptor                m_mpegTSDescriptor;
    std::vector<AccessUnit>         m_videoAccessUnits;
    std::vector<AccessUnit>         m_audioAccessUnits;
    AccessUnit                      m_currentVideoAU;
    AccessUnit                      m_currentAudioAU;
    bool                            m_bParsedMpegTSDescriptor = false;
    bool                            m_bParsedPMT = false;
    //std::vector<ElementaryStream> gElementaryStreams;
};

// It all starts here
int main(int argc, char* argv[])
{
//    DoMyXMLTest(argv[1]);

/*
    if (0 == argc)
    {
        fprintf(stderr, "Usage: %s [-g] [-p] [-x] mp2ts_file\n", argv[0]);
        fprintf(stderr, "-g: Generate an OpenGL GUI representing the MP2TS\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        fprintf(stderr, "-x: Output extensive xml representation of MP2TS file to stdout\n");
        return 0;
    }

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
}