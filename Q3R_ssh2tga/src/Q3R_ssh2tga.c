#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#include "tga_utils.h"
#include "types.h"
#include "ssh_utils.h"


/* local functions declarations */
static void printUsage(void);
static outFormat_t checkOption(const char *option, int *firstFileIdx);

int main(int argc, char **argv){
    int i, firstFileIdx;
    outFormat_t outFormat;
    bool success;
    sshHandle_t sshHandle;

    puts("\tQuake 3 Revolution SSH to TGA image converter by Yagotzirck\n");

    if(argc == 1){
        printUsage();
        return 1;
    }

    outFormat = checkOption(argv[1], &firstFileIdx);

    if(firstFileIdx == argc){
        fputs("You need to specify at least one file after the option!\n", stderr);
        return 1;
    }

    for(i = firstFileIdx; i < argc; ++i){
        if(!init_sshHandle(&sshHandle, argv[i]))
            continue;

        printf("Converting %s...", argv[i]);
        success = ssh_convertAndSave(&sshHandle, outFormat);
        if(success)
            puts("done");

        free_sshHandleBuffers(&sshHandle);
    }

    puts("\nConversion complete!");
    return 0;
}

// local functions definitions

static void printUsage(void){
    fputs(
        "Usage: Q3R_ssh2tga.exe <option> <file1> <file2> ... <fileN>\n"
        "where <option> is one of the following:\n\n"

        "-out_shrink\n\t"
            "Remove unused palette entries from paletted images, remove alpha\n\t"
            "channel from palette entries/truecolor pixels if the alpha value is\n\t"
            "fully opaque for all palette/pixel entries, and apply RLE encoding.\n\n"

        "-out_asIs\n\t"
            "Save paletted images as paletted and truecolor images\n\t"
            "as truecolor, without removing/altering anything.\n\n"

        "-out_truecolor_upsideDown\n\t"
            "Convert paletted images to truecolor, then switch the\n\t"
            "pixel data's row order from top-bottom to bottom-top for all\n\t"
            "images; this option is both for maximum compatibility reasons\n\t"
            "and/or in case you wish to use the converted images to mod\n\t"
            "Quake 3 Arena, since it only accepts bottom-top TGA images\n\t"
            "(good job, John Carmack.)\n\n"

        "If no option is specified, -out_shrink will be used by default.\n",

      stderr
    );
}

static outFormat_t checkOption(const char *option, int *firstFileIdx){
    char option_lowercase[FILENAME_MAX];

    const char *optionsStrList[] = {
        "-out_shrink",
        "-out_asis",
        "-out_truecolor_upsidedown"
    };

    int i;
    const int numOptions = sizeof(optionsStrList) / sizeof(optionsStrList[0]);


    /* if the 1st character isn't a hyphen then we assume that no argument
    ** has been specified and that argv[1] is a file passed as a parameter,
    ** so we'll use the default option OUT_SHRINK as specified in the
    ** usage message.
    */
    if(option[0] != '-'){
        *firstFileIdx = 1;
        return OUT_SHRINK;
    }

    /* if the 1st character is a hyphen, then argv[1] is expected to be the option
    ** and the 1st file passed as a parameter is expected to be at argv[2]
    */
    *firstFileIdx = 2;

    // get rid of case sensitivity
    for(i = 0; option[i] != '\0'; i++)
        option_lowercase[i] = tolower(option[i]);
    option_lowercase[i] = '\0';

    // find which option has been chosen
    for(i = 0; i < numOptions; i++)
        if(strcmp(optionsStrList[i], option_lowercase) == 0)
            return i;

    // no supported option has been found; abort the program
    fputs(  "The option you specified is unsupported.\n"
            "Invoke this exe without any parameters to see a list of available options.\n", stderr);

    exit(EXIT_FAILURE);
}
