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

    AccessUnitElement(size_t startByteLocation, size_t numPackets, uint8_t packetSize)
        : startByteLocation(startByteLocation)
        , numPackets(numPackets)
        , packetSize(packetSize)
    {
    }
};

struct AccessUnit
{
    long int type;
    std::string name;

    std::vector<AccessUnitElement> accessUnitElements;

    AccessUnit(long int type, std::string name)
        : type(type)
        , name(name)
    {
    }
};

struct ElementaryStream
{
    std::string name;
    long int type;
    long int pid;
};

AccessUnit                    gVideoAccessUnit(0, nullptr);
std::vector<ElementaryStream> gElementaryStreams;
bool                          gbParsedPMT = false;

bool BuildPacketList(const std::string &pXMLFile)
{
    tinyxml2::XMLDocument doc;
	doc.LoadFile(pXMLFile.c_str());

    tinyxml2::XMLElement* root = doc.FirstChildElement("file");

    if(NULL == root)
        return false;

    tinyxml2::XMLElement* element = root->FirstChildElement("packet");

    while(element)
    {
        if(!gbParsedPMT)
        {
            tinyxml2::XMLElement* pmt = element->FirstChildElement("program_map_table");
            if(pmt)
            {
                tinyxml2::XMLElement* stream = pmt->FirstChildElement("stream");
                while(stream)
                {
                    ElementaryStream es;

                    tinyxml2::XMLElement* pid = stream->FirstChildElement("pid");
                    es.pid = strtol(pid->GetText(), NULL, 16);

                    tinyxml2::XMLElement* type = stream->FirstChildElement("type_number");
                    es.type = strtol(type->GetText(), NULL, 16);

                    tinyxml2::XMLElement* typeName = stream->FirstChildElement("type_name");
                    es.name = typeName->GetText();

                    gElementaryStreams.push_back(es);

                    stream = stream->NextSiblingElement("stream");;
                }

                gbParsedPMT = true;
            }
        }
        else
        {
            tinyxml2::XMLElement* pusi = element->FirstChildElement("payload_unit_start_indicator");

            if(1 == strtol(pusi->GetText(), NULL, 16))
            {
                tinyxml2::XMLElement* pid = element->FirstChildElement("pid");

                if(0x31 == strtol(pid->GetText(), NULL, 16))
                {
/*
AccessUnit au;
struct AccessUnit
{
    long int type;
    std::string name;

    std::vector<AccessUnitElement> accessUnitElements;

    AccessUnit(long int type, std::string name)
        : type(type)
        , name(name)
    {
    }
};
*/
                    if(0 == gElementaryStreams.size())
                    {
                    }
                }
            }
        }

        element = element->NextSiblingElement("packet");
    }

    return true;
}

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

    // Build current access units
    BuildPacketList(argv[1]);

    // Show as GUI
}