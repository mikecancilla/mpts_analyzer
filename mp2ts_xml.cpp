#include "mp2ts_xml.h"

bool MpegTS_XML::ParsePMT(tinyxml2::XMLElement* root)
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
                    pAU->esd.streamType = (eStreamType) strtol(type->GetText(), NULL, 16);

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

bool MpegTS_XML::ParseMpegTSDescriptor(tinyxml2::XMLElement* root)
{
    tinyxml2::XMLElement* element = root->FirstChildElement("name");
    m_mpegTSDescriptor.fileName = element->GetText();

    element = root->FirstChildElement("file_size");
    m_mpegTSDescriptor.fileSize = std::atoi(element->GetText());

    element = root->FirstChildElement("packet_size");
    m_mpegTSDescriptor.packetSize = std::atoi(element->GetText());

    element = root->FirstChildElement("terse");
    m_mpegTSDescriptor.terse = std::atoi(element->GetText()) == 1 ? true : false;

    if("" == m_mpegTSDescriptor.fileName ||
        0 == m_mpegTSDescriptor.packetSize)
        return false;

    return true;
}

static eFrameType ConvertStringToFrameType(std::string input)
{
    if(input == "I")
        return eFrameI;

    if(input == "P")
        return eFrameP;

    if(input == "B")
        return eFrameB;

    return eFrameUnknown;
}

static uint64_t ConvertStringToPTS(std::string input)
{
    uint64_t timeStamp = 0;

    sscanf(input.c_str(), "%llu", &timeStamp);

    return timeStamp;
}

static float ConvertStringToPTSSeconds(std::string input)
{
    float timeStamp = 0;

    sscanf(input.c_str(), "%*s (%f)", &timeStamp);

    return timeStamp;
}

bool MpegTS_XML::ParsePacketListTerse(tinyxml2::XMLElement* root)
{
    if(NULL == root)
        return false;

    tinyxml2::XMLElement* element = nullptr;

    element = root->FirstChildElement("frame");

    unsigned int videoFrameNumber = 0;
    unsigned int audioFrameNumber = 0;

    while(element)
    {
        const char *pid = element->Attribute("pid");
        long thisPID = strtol(pid, NULL, 16);

        AccessUnit *pAU = nullptr;

        if(m_currentVideoAU.esd.pid == thisPID)
            pAU = &m_currentVideoAU;
        else if(m_currentAudioAU.esd.pid == thisPID)
            pAU = &m_currentAudioAU;

        if(pAU)
        {
            tinyxml2::XMLElement* dts = element->FirstChildElement("DTS");
            if(dts)
            {
                pAU->dts = ConvertStringToPTS(dts->GetText());
                pAU->dts_seconds = ConvertStringToPTSSeconds(dts->GetText());
            }

            tinyxml2::XMLElement* pts = element->FirstChildElement("PTS");
            if(pts)
            {
                pAU->pts = ConvertStringToPTS(pts->GetText());
                pAU->pts_seconds = ConvertStringToPTSSeconds(pts->GetText());
            }

            tinyxml2::XMLElement* type = element->FirstChildElement("type");
            if(type)
                //pAU->frameType = ConvertStringToFrameType(type->GetText());
                pAU->frameType = type->GetText();

            tinyxml2::XMLElement* slices = element->FirstChildElement("slices");

            if(slices)
            {
                tinyxml2::XMLElement* slice = slices->FirstChildElement("slice");

                while(slice)
                {
                    AccessUnitElement aue;
                    aue.startByteLocation = slice->Int64Attribute("byte");
                    aue.numPackets = slice->Int64Attribute("packets");

                    pAU->accessUnitElements.push_back(aue);

                    slice = slice->NextSiblingElement("slice");
                }
            }

            if(pAU == &m_currentVideoAU)
            {
                m_currentVideoAU.frameNumber = videoFrameNumber++;
                m_videoAccessUnitsDecode.push_back(m_currentVideoAU);
            }
            else if(pAU == &m_currentAudioAU)
            {
                m_currentVideoAU.frameNumber = audioFrameNumber++;
                m_audioAccessUnits.push_back(m_currentAudioAU);
            }

            pAU->accessUnitElements.clear();
        }

        element = element->NextSiblingElement("frame");
    }

    if(m_currentVideoAU.accessUnitElements.size())
    {
        m_videoAccessUnitsDecode.push_back(m_currentVideoAU);
        m_currentVideoAU.accessUnitElements.clear();
    }
        
    if(m_currentAudioAU.accessUnitElements.size())
    {
        m_audioAccessUnits.push_back(m_currentAudioAU);
        m_currentAudioAU.accessUnitElements.clear();
    }

    return true;
}

bool MpegTS_XML::ParsePacketList(tinyxml2::XMLElement* root)
{
    if(NULL == root)
        return false;

    bool ret = false;

    if(m_mpegTSDescriptor.terse)
        ret = ParsePacketListTerse(root);
    else
    {
        tinyxml2::XMLElement* element = nullptr;

        element = root->FirstChildElement("packet");

        long lastPID = -1;

        unsigned int videoFrameNumber = 0;
        unsigned int audioFrameNumber = 0;

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
                        {
                            m_currentVideoAU.frameNumber = videoFrameNumber++;
                            m_videoAccessUnitsDecode.push_back(m_currentVideoAU);
                        }
                        else if(pAU == &m_currentAudioAU)
                        {
                            m_currentVideoAU.frameNumber = audioFrameNumber++;
                            m_audioAccessUnits.push_back(m_currentAudioAU);
                        }
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
            m_videoAccessUnitsDecode.push_back(m_currentVideoAU);
            m_currentVideoAU.accessUnitElements.clear();
        }
        
        if(m_currentAudioAU.accessUnitElements.size())
        {
            m_audioAccessUnits.push_back(m_currentAudioAU);
            m_currentAudioAU.accessUnitElements.clear();
        }

        ret = true;
    }

    uint32_t frameNumber = 0;

    if(ret && m_videoAccessUnitsDecode.size())
    {
        AccessUnit *referenceFrame = NULL;

        for(std::vector<AccessUnit>::iterator iter = m_videoAccessUnitsDecode.begin(); iter < m_videoAccessUnitsDecode.end(); iter++)
        {
            if(iter->frameType == "I" ||
               iter->frameType == "P")
            {
                if(referenceFrame)
                    AddPresentationUnit(*referenceFrame, frameNumber++);

                referenceFrame = &(*iter);
            }
            else
            {
                AddPresentationUnit(*iter, frameNumber++);
            }
        }

        AddPresentationUnit(*referenceFrame, frameNumber++);
    }

    return ret;
}

inline void MpegTS_XML::AddPresentationUnit(AccessUnit au, uint32_t frameNumber)
{
    au.frameNumber = frameNumber;
    m_videoAccessUnitsPresentation.push_back(au);
}
