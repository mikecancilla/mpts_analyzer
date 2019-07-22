#include "VertexBufferLayout.h"
#include "gtc/matrix_transform.hpp"

#include "TexturePresenter.h"

TexturePresenter::TexturePresenter(GLFWwindow *pWindow, const std::string &imageFileName)
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
    glfwGetWindowSize(pWindow, &w, &h);
    m_Proj = glm::ortho(0.f, (float) w, 0.f, (float) h, -1.f, 1.f);

    float positions[] = {
              0.f,       0.f, 0.f, 0.f, // Bottom Left, 0
		(float) w,       0.f, 1.f, 0.f, // Bottom Right, 1
		(float) w, (float) h, 1.f, 1.f, // Top Right, 2
              0.f, (float) h, 0.f, 1.f  // Top Left, 3
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

TexturePresenter::~TexturePresenter()
{
}

void TexturePresenter::Render()
{
	Renderer renderer;

	m_Texture->Bind();

	glm::mat4 mvp = m_Proj;

    m_Shader->Bind();
	m_Shader->SetUniformMat4f("u_MVP", mvp);
	renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
}

void TexturePresenter::Render(uint8_t *pFrame, int w, int h)
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
