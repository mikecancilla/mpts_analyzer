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

using namespace tinyxml2;

// Create XML from scratch, not from reading a file.
void DoMyXMLTest2()
{
	{
		// Test: Programmatic DOM nodes insertion return values
		XMLDocument doc;

		XMLNode* first = doc.NewElement( "firstElement" );
		XMLNode* firstAfterInsertion = doc.InsertFirstChild( first );

		XMLNode* last = doc.NewElement( "lastElement" );
		XMLNode* lastAfterInsertion = doc.InsertEndChild( last );

		XMLNode* middle = doc.NewElement( "middleElement" );
		XMLNode* middleAfterInsertion = doc.InsertAfterChild( first, middle );
        doc.Print();
	}
	{
		// Test: Programmatic DOM
		// Build:
		//		<element>
		//			<!--comment-->
		//			<sub attrib="1" />
		//			<sub attrib="2" />
		//			<sub attrib="3" >& Text!</sub>
		//		<element>

		XMLDocument* doc = new XMLDocument();
		XMLNode* element = doc->InsertEndChild( doc->NewElement( "element" ) );

		XMLElement* sub[3] = { doc->NewElement( "sub" ), doc->NewElement( "sub" ), doc->NewElement( "sub" ) };
		for( int i=0; i<3; ++i ) {
			sub[i]->SetAttribute( "attrib", i );
		}
		element->InsertEndChild( sub[2] );

		const int dummyInitialValue = 1000;
		int dummyValue = dummyInitialValue;

		XMLNode* comment = element->InsertFirstChild( doc->NewComment( "comment" ) );
		comment->SetUserData(&dummyValue);
		element->InsertAfterChild( comment, sub[0] );
		element->InsertAfterChild( sub[0], sub[1] );
		sub[2]->InsertFirstChild( doc->NewText( "& Text!" ));
		doc->Print();

		// And now deletion:
		element->DeleteChild( sub[2] );
		doc->DeleteNode( comment );

		element->FirstChildElement()->SetAttribute( "attrib", true );
		element->LastChildElement()->DeleteAttribute( "attrib" );

		const int defaultIntValue = 10;
		const int replacementIntValue = 20;
		int value1 = defaultIntValue;
		int value2 = doc->FirstChildElement()->LastChildElement()->IntAttribute( "attrib", replacementIntValue );
		XMLError result = doc->FirstChildElement()->LastChildElement()->QueryIntAttribute( "attrib", &value1 );

		doc->Print();

		{
			XMLPrinter streamer;
			doc->Print( &streamer );
			printf( "%s", streamer.CStr() );
		}
		{
			XMLPrinter streamer( 0, true );
			doc->Print( &streamer );
		}

//        doc->SaveFile( "./resources/out/pretty.xml" );
//		doc->SaveFile( "./resources/out/compact.xml", true );
		delete doc;
	}
}