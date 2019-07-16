#include "tinyxml2.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>

extern void DoMyXMLTest(char *pXMLFile);


// It all starts here
int main(int argc, char* argv[])
{
    DoMyXMLTest(argv[1]);

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
}