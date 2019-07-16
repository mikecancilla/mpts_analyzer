// A set of tests for me to figure out how to use TinyXML

#include "tinyxml2.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>

void inline printf_xml(unsigned int indent, const char *format, ...)
{
    if(format)
    {
        char output_buffer[512] = "";

        for(unsigned int i = 0; i < indent; i++)
            strcat_s(output_buffer, sizeof(output_buffer), " ");

        va_list arg_list;
        va_start(arg_list, format);
        vsprintf_s(output_buffer + indent, sizeof(output_buffer) - indent, format, arg_list);
        va_end(arg_list);

        printf(output_buffer);
    }
}

void PrintChildren(tinyxml2::XMLElement* root, unsigned int indent = 0)
{
    tinyxml2::XMLElement* element = root->FirstChildElement();

	while(element)
	{
        if(!element->GetText())
        {
            printf_xml(indent, "<%s", element->Value());

            const tinyxml2::XMLAttribute *attribute = element->FirstAttribute();
            while(attribute)
            {
                printf(" %s=\"%s\"", attribute->Name(), attribute->Value());
                attribute = attribute->Next();
            }

            printf(">\n");

            PrintChildren(element, indent + 2);

            printf_xml(indent, "</%s>\n", element->Value());
        }
        else
        {
            printf_xml(indent, "<%s", element->Value());

            const tinyxml2::XMLAttribute *attribute = element->FirstAttribute();
            while(attribute)
            {
                printf(" %s=\"%s\"", attribute->Name(), attribute->Value());
                attribute = attribute->Next();
            }

            printf(">%s</%s>\n", element->GetText(), element->Value());
        }

        element = element->NextSiblingElement();
    }
}

// It all starts here
void DoMyXMLTest(char *pXMLFile)
{
    tinyxml2::XMLDocument doc;
	doc.LoadFile(pXMLFile);

    tinyxml2::XMLElement* root = doc.FirstChildElement();

    printf("<%s>\n", root->Value());

    PrintChildren(root, 2);

    printf("</%s>\n", root->Value());
}