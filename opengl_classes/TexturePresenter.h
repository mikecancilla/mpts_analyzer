#pragma once

#include <memory>
#include <string>

#include "VertexArray.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Texture.h"

#include <GLFW/glfw3.h>

class TexturePresenter
{
public:

    TexturePresenter(GLFWwindow *pWindow, const std::string &imageFileName);
    ~TexturePresenter();

    void Render();
    void Render(uint8_t *pFrame, int w, int h);

	std::unique_ptr<VertexArray> m_VAO;
	std::unique_ptr<VertexBuffer> m_VertexBuffer;
	std::unique_ptr<IndexBuffer> m_IndexBuffer;
	std::unique_ptr<Shader> m_Shader;
	std::unique_ptr<Texture> m_Texture;
	glm::mat4 m_Proj;
};

