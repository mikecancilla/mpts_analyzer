#pragma once

#include "Renderer.h"

class Texture
{
private:
    std::string m_FilePath;
    unsigned char* m_LocalBuffer;
    int m_Width, m_Height, m_BPP;

public:
    Texture(const std::string& path);
    Texture(unsigned char *pData, int w, int h);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void UnBind() const;

    inline int GetWidth() const { return m_Width; }
    inline int GetHeight() const { return m_Height; }

    unsigned int m_RendererID;
};
