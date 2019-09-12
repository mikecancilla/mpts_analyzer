#include "mp2ts_xml.h"

#define GOP_LENGTH 30

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
                        pAU = &m_videoAU;
                    break;

                    case eMPEG1_Audio:
                    case eMPEG2_Audio:
                    case eMPEG2_AAC_Audio:
                    case eMPEG4_LATM_AAC_Audio:
                    case eA52_AC3_Audio:
                    case eHDMV_DTS_Audio:
                    case eA52b_AC3_Audio:
                    case eSDDS_Audio:
                        pAU = &m_audioAU;
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

        if(m_videoAU.esd.pid == thisPID)
            pAU = &m_videoAU;
        else if(m_audioAU.esd.pid == thisPID)
            pAU = &m_audioAU;

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

            if(pAU->frameType == "I")
            {
                tinyxml2::XMLElement* closed_gop = element->FirstChildElement("closed_gop");
                if(closed_gop)
                    pAU->closed_gop = std::atoi(closed_gop->GetText());
            }

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

            if(pAU == &m_videoAU)
            {
                m_videoAU.frameNumber = videoFrameNumber++;
                m_videoAccessUnitsDecode.push_back(m_videoAU);
            }
            else if(pAU == &m_audioAU)
            {
                m_videoAU.frameNumber = audioFrameNumber++;
                m_audioAccessUnits.push_back(m_audioAU);
            }

            pAU->accessUnitElements.clear();
        }

        element = element->NextSiblingElement("frame");
    }

    if(m_videoAU.accessUnitElements.size())
    {
        m_videoAccessUnitsDecode.push_back(m_videoAU);
        m_videoAU.accessUnitElements.clear();
    }
        
    if(m_audioAU.accessUnitElements.size())
    {
        m_audioAccessUnits.push_back(m_audioAU);
        m_audioAU.accessUnitElements.clear();
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

            if(m_videoAU.esd.pid == thisPID)
                pAU = &m_videoAU;
            else if(m_audioAU.esd.pid == thisPID)
                pAU = &m_audioAU;

            if(pAU)
            {
                tinyxml2::XMLElement* pusi = element->FirstChildElement("payload_unit_start_indicator");

                bool bNewAUSet = false;

                if(1 == strtol(pusi->GetText(), NULL, 16))
                {
                    if(pAU->accessUnitElements.size())
                    {
                        if(pAU == &m_videoAU)
                        {
                            m_videoAU.frameNumber = videoFrameNumber++;
                            m_videoAccessUnitsDecode.push_back(m_videoAU);
                        }
                        else if(pAU == &m_audioAU)
                        {
                            m_videoAU.frameNumber = audioFrameNumber++;
                            m_audioAccessUnits.push_back(m_audioAU);
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

        if(m_videoAU.accessUnitElements.size())
        {
            m_videoAccessUnitsDecode.push_back(m_videoAU);
            m_videoAU.accessUnitElements.clear();
        }
        
        if(m_audioAU.accessUnitElements.size())
        {
            m_audioAccessUnits.push_back(m_audioAU);
            m_audioAU.accessUnitElements.clear();
        }

        ret = true;
    }

    BuildPresentationUnits(0);

/*
    uint32_t frameNumber = 0;

    // Frame reordering: decode order to presentation order.
    // Build a presentation order list too.
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
*/

    return ret;
}

inline void MpegTS_XML::AddPresentationUnit(AccessUnit au, uint32_t frameNumber)
{
    au.frameNumber = frameNumber;
    m_videoAccessUnitsPresentation.push_back(au);
}

// Frame reordering: decode order to presentation order.
// Call this after a seek
unsigned int MpegTS_XML::BuildPresentationUnits(unsigned int startFrameNumber)
{
    unsigned int retCount = startFrameNumber;
    bool bRetCountSet = false;

    if(m_videoAccessUnitsDecode.size())
    {
        m_previousReferenceFrame = nullptr;

        m_videoAccessUnitsPresentation.clear();

        bool closedGOP = m_videoAccessUnitsDecode[startFrameNumber].closed_gop;

        unsigned int addFrameCount = startFrameNumber;
        unsigned int i = startFrameNumber;
        unsigned int endFrameNumber = startFrameNumber+GOP_LENGTH;
        for(; i<endFrameNumber && i < m_videoAccessUnitsDecode.size(); i++)
        {
            if(m_videoAccessUnitsDecode[i].frameType == "I" ||
               m_videoAccessUnitsDecode[i].frameType == "P")
            {
                if(m_previousReferenceFrame)
                {
                    if(!bRetCountSet)
                    {
                        retCount = addFrameCount;
                        bRetCountSet = true;
                    }

                    AddPresentationUnit(*m_previousReferenceFrame, addFrameCount++);
                    
                    // Always set closedGOP to true once second reference frame is found
                    closedGOP = true;
                }

                m_previousReferenceFrame = &(m_videoAccessUnitsDecode[i]);
            }
            else
            {
                if(closedGOP)
                    AddPresentationUnit(m_videoAccessUnitsDecode[i], addFrameCount++);
                else
                {
                    // We are skipping B frames since Open GOP.
                    // Increment frame count
                    // Increment endFrameNumber so we get a full GOP length of frames added
                    addFrameCount++;
                    endFrameNumber++;
                }
            }
        }

        if(m_previousReferenceFrame)
            AddPresentationUnit(*m_previousReferenceFrame, addFrameCount++);

        m_startFrameNumber = startFrameNumber;
    }

    return retCount;
}

bool MpegTS_XML::UpdatePresentationUnits(unsigned int frameDisplaying)
{
    bool bRet = false;

    // Frame reordering: decode order to presentation order.
    // Build a presentation order list too.

    unsigned int index = frameDisplaying + m_videoAccessUnitsPresentation.size()-1;

    if(index < m_videoAccessUnitsDecode.size())
    {
        bRet = true;
        int i = 0;
        for(; i<m_videoAccessUnitsPresentation.size()-1; i++)
            m_videoAccessUnitsPresentation[i] = m_videoAccessUnitsPresentation[i+1];

        m_videoAccessUnitsPresentation[i] = m_videoAccessUnitsDecode[index];
    }
    else
    {
        m_videoAccessUnitsPresentation.pop_front();
    }

    return bRet;
}