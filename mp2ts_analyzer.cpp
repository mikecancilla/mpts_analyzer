#include "tinyxml2.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>

extern void DoMyXMLTest(char *pXMLFile);

struct AccessUnit
{
    std::string auName;
    size_t startByteLocation;
    size_t numPackets;
    uint8_t packetSize;

    AccessUnit(std::string auName, size_t startByteLocation, size_t numPackets, size_t packetSize)
        : auName(auName)
        , startByteLocation(startByteLocation)
        , numPackets(numPackets)
        , packetSize(packetSize)
    {
    }
};

struct ElementaryStream
{
    std::string name;
    long int pid;
};

std::vector<AccessUnit> gVideoAccessUnit;
std::vector<ElementaryStream> gElementaryStreams;
bool gbParsedPMT = false;

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

                    tinyxml2::XMLElement* typeName = stream->FirstChildElement("type_name");
                    es.name = typeName->GetText();

                    gElementaryStreams.push_back(es);

                    stream = stream->NextSiblingElement("stream");;
                }

                gbParsedPMT = true;
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