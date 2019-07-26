#pragma once

#include <string>
#include <vector>

// Tinyxml
#include "tinyxml2.h"

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

enum eStreamType
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
};

struct AccessUnitElement
{
    int64_t startByteLocation;
    int64_t numPackets;

    AccessUnitElement()
        : startByteLocation(-1)
        , numPackets(-1)
    {
    }

    AccessUnitElement(int64_t startByteLocation, int64_t numPackets)
        : startByteLocation(startByteLocation)
        , numPackets(numPackets)
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
        : frameNumber(0)
    {
    }

    AccessUnit(std::string name, eStreamType type, long pid)
        : esd(name, type, pid)
        , frameNumber(0)
    {
    }

    unsigned int frameNumber;
};

struct MpegTSDescriptor
{
    std::string fileName;
    int64_t fileSize;
    uint8_t packetSize;
    bool terse;

    MpegTSDescriptor()
        : fileName("")
        , fileSize(0)
        , packetSize(0)
        , terse(true)
    {}
};

class MpegTS_XML
{
public:

    MpegTS_XML()
    : m_bParsedMpegTSDescriptor(false)
    , m_bParsedPMT(false)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    {
    }

    bool ParsePMT(tinyxml2::XMLElement* root);
    bool ParseMpegTSDescriptor(tinyxml2::XMLElement* root);
    bool ParsePacketList(tinyxml2::XMLElement* root);
    bool ParsePacketListTerse(tinyxml2::XMLElement* root);

public:
    MpegTSDescriptor            m_mpegTSDescriptor;
    std::vector<AccessUnit>     m_videoAccessUnits;
    std::vector<AccessUnit>     m_audioAccessUnits;
    int                         m_videoStreamIndex;
    int                         m_audioStreamIndex;

private:
    AccessUnit                  m_currentVideoAU;
    AccessUnit                  m_currentAudioAU;
    bool                        m_bParsedMpegTSDescriptor = false;
    bool                        m_bParsedPMT = false;
    //std::vector<ElementaryStream> gElementaryStreams;
};
