#include "VertexArray.h"
#include "VertexBufferLayout.h"
#include "Renderer.h"
#include <GL/glew.h>

VertexArray::VertexArray()
{
    GLCall(glGenVertexArrays(1, &m_RendererID));
}

VertexArray::~VertexArray()
{
    GLCall(glDeleteVertexArrays(1, &m_RendererID));
}

void VertexArray::AddBuffer(const VertexBuffer & vb, const VertexBufferLayout & layout)
{
    // Bind the vertex array
    Bind();

    // Bind the vertex buffer
    vb.Bind();

    const auto & elements = layout.GetElements();
    unsigned int offset = 0;

    // Set up all the layout
    for(unsigned int i = 0; i < elements.size(); i++)
    {
        const auto& element = elements[i];

        GLCall(glEnableVertexAttribArray(i));

        // Define the layout of the data in the vertex buffer
        GLCall(glVertexAttribPointer(i,                     // index
                                     element.count,         // number of components per generic vertex attribute.  Must be 1, 2, 3, 4
                                     element.type,          // type
                                     element.normalized,    // normalized
                                     layout.GetStride(),    // stride, byte offset between consecutive generic vertex attributes
                                     (const void*) offset));// the offset of the first component of the first generic vertex attribute
                                                            // in the array in the data store of the buffer currently bound to the GL_ARRAY_BUFFER target
        offset += element.count * VertexBufferElement::GetSizeOfType(element.type);
    }
}

void VertexArray::Bind() const
{
    GLCall(glBindVertexArray(m_RendererID));
}

void VertexArray::UnBind() const
{
    GLCall(glBindVertexArray(0));
}
