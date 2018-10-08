#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "makedir.h"

typedef enum SDTtype_e{
    SDT_TYPE_1 = 0x0000,
    SDT_TYPE_2 = 0x3039
}SDTtype_t;

/*********** SDT archive types ***********
** There are 2 types of SDT archives identified by the SDT_type field in the SDT_header_t structure,
** which differ as follows in their structure:
**

******** SDT_TYPE_1 ********
- SDT_header_t
- array of numFiles offsets to subfiles ("subFilesOffsets" in the code below); each subfile's data block is preceded by
  a SDT_subfileHeader_t header, like this:
    - subfile 1's subfileHeader_t
    - subfile 1's data

    - subfile 2's subfileHeader_t
    - subfile 2's data
    ....
    - subfile N's subfileHeader_t
    - subfile N's data

******** SDT_TYPE_2 ********
- SDT_header_t
- array of numFiles offsets to subfiles' data("subFilesOffsets" in the code below); unlike SDT_TYPE_1, the offsets
  point directly to subfiles' data this time, since the subfileHeader_t headers are organized sequentially
  as an array and placed after the array of offsets.
- array of numFiles subfileHeader_t headers.

********************************************

** In other words, in SDT_TYPE_1 archives each subfileHeader_t header precedes the subfile's data it's associated to,
** and the offsets point to the headers which are then followed by the subfiles' data, while in SDT_TYPE_2 archives the
** archive structure follows this scheme:

    - SDT_header_t

    - subfile 1's data offset
    - subfile 2's data offset
    ...
    - subfile N's data offset

    - subfile 1's subFileHeader_t header
    - subfile 2's subFileHeader_t header
    ...
    - subfile N's subFileHeader_t header

    - subfile 1's data
    - subfile 2's data
    ...
    - subfile N's data

** Confused yet? :)

*****************************************/


/* macros used for the values in the sndFormat field in the structure
** SDT_subfileHeader_t.
** It's obvious that this field is actually a bitfield containing
** several flags indicating much more than the sound format
** (notice how bit 15 is set for VAG ADPCM, and bit 13 is set for MP2),
** but since the other fields aren't important for ripping purposes
** I'll just keep it as it is, especially since I've only encountered the 3 values
** listed below for all the entries I've examined.
** A bitmask would have probably made this look a little more elegant, but whatever.
*/
#define SNDFORMAT_VAG   0x8010
#define SNDFORMAT_MP2   0x2410
#define SNDFORMAT_MP2_2 0x2510


#define SWAP_ENDIAN16(x) (((x) >> 8) | ((x) << 8))
#define SWAP_ENDIAN32(x) (((x)>>24) | (((x)>>8) & 0xFF00) | (((x)<<8) & 0x00FF0000) | ((x)<<24))

typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;

typedef struct SDT_header_s{
    WORD numFiles;
    WORD SDT_type;
}SDT_header_t;

typedef struct SDT_subfileHeader_s{
    DWORD currHeaderSize;   // seems to be always 0x28
    DWORD dataSize;
    char fileName[16];
    WORD sampleRate;
    WORD sndFormat;   // 0x8010 for .VAG ADPCM, 0x2410/0x2510 for .MP2
    DWORD unk1, unk2, unk3;
}SDT_subfileHeader_t;

typedef struct VAGhdr_s{            // All the values in this header must be big endian
        char id[4];                 // VAGp
        DWORD version;              // I guess it doesn't matter, so I'll place a 0 here and call it a day
        DWORD reserved;             // I guess it doesn't matter either
        DWORD dataSize;
        DWORD samplingFrequency;
        char  reserved2[12];
        char  name[16];
}VAGhdr_t;


// global variables(used only inside this module)
static VAGhdr_t VAGhdr = {
    {'V', 'A', 'G', 'p'},
    0,
    0,
    0,              // will be set for each entry later on
    0,              // as above
    "Yagotzirck",   // Couldn't resist :)
    ""              // will be set for each entry later on
};

static char path[FILENAME_MAX];
static char *currDirPtr;

// local functions declarations
static bool is_SDT(FILE *in_fp, SDTtype_t *SDTtype);
static bool extract_SDT1(FILE *in_fp);
static bool extract_SDT2(FILE *in_fp);
static bool save_subFile(BYTE *subFileData, SDT_subfileHeader_t *SDT_subfileHeader);


int main(int argc, char **argv){
    FILE *in_fp;

    unsigned i;
    bool success;

    SDTtype_t SDTtype;

    puts("\t\tQuake 3 Revolution SDT extractor by Yagotzirck");

    if(argc == 1){
        fputs("Usage: Q3R_SDT_Extractor.exe <file1.SDT> <file2.SDT> ... <fileN.SDT>", stderr);
        return 1;
    }

    for(i = 1; i < argc; i++){
        if((in_fp = fopen(argv[i], "rb")) == NULL){
            fprintf(stderr, "Couldn't open %s: %s\n", argv[i], strerror(errno));
            continue;
        }

        // check if the opened file is a valid SDT file and get the SDT archive type while we're at it
        if(!is_SDT(in_fp, &SDTtype)){
            fprintf(stderr, "%s doesn't appear to be a valid SDT file\n", argv[i]);
            fclose(in_fp);
            continue;
        }

        /* create a directory in the same path as the SDT file we're going to extract,
        ** with the same name as the SDT file but without the .SDT file extension and
        ** with "_extracted" appended to it
        */
        strcpy(path, argv[i]);
        currDirPtr = path + strlen(path) - 4;
        strcpy(currDirPtr, "_extracted/");
        makeDir(path);
        currDirPtr = currDirPtr + strlen(currDirPtr);
        currDirPtr[sizeof(((SDT_subfileHeader_t*)0)->fileName)] = '\0';

        // handle the SDT subfiles' extraction according to the archive type
        printf("Extracting %s...", argv[i]);

        if(SDTtype == SDT_TYPE_1)
            success = extract_SDT1(in_fp);
        else
            success = extract_SDT2(in_fp);

        fclose(in_fp);
        if(success)
            puts("done");

    }

    return 0;
}

// local functions definitions

static bool is_SDT(FILE *in_fp, SDTtype_t *SDTtype){
    SDT_header_t SDT_header;
    DWORD firstSubfileOffset = 0;
    DWORD firstSubfileHdrSize = 0;

    if(!fread(&SDT_header, sizeof(SDT_header), 1, in_fp))
        return false;

    /* all the subfiles' headers have a field indicating the header's size;
    ** since all of the headers are 40(0x28) bytes, we can exploit this fact
    ** to check if the file is a valid SDT file by comparing the first subfile's
    ** header size field with the expected subfile header's size
    ** (which is 0x28, as already stated.)
    */

    switch(SDT_header.SDT_type){
        case SDT_TYPE_1:
            fread(&firstSubfileOffset, sizeof(firstSubfileOffset), 1, in_fp);
            fseek(in_fp, firstSubfileOffset, SEEK_SET);
            fread(&firstSubfileHdrSize, sizeof(firstSubfileHdrSize), 1, in_fp);

            if(firstSubfileHdrSize != sizeof(SDT_subfileHeader_t))
               return false;

            break;

        case SDT_TYPE_2:
            fseek(in_fp, SDT_header.numFiles * sizeof(firstSubfileOffset), SEEK_CUR);   // skip past the array of offsets
            fread(&firstSubfileHdrSize, sizeof(firstSubfileHdrSize), 1, in_fp);

            if(firstSubfileHdrSize != sizeof(SDT_subfileHeader_t))
                return false;

            break;

        default:
            return false;
    }

    rewind(in_fp);
    *SDTtype = SDT_header.SDT_type;
    return true;
}

static bool extract_SDT1(FILE *in_fp){
    SDT_header_t        SDT_header;
    DWORD*              subFilesOffsets;
    SDT_subfileHeader_t SDT_subfileHeader;
    BYTE*               subfileData;

    unsigned i, numFiles;

    // get the SDT header
    fread(&SDT_header, sizeof(SDT_header), 1, in_fp);

    numFiles = SDT_header.numFiles;

    // allocate and read the array of subfiles' offsets
    if((subFilesOffsets = malloc(numFiles * sizeof(*subFilesOffsets))) == NULL){
        fprintf(stderr, "\n\tCouldn't allocate %u bytes for %s's offsets array\n", numFiles * sizeof(*subFilesOffsets), path);
        return false;
    }
    fread(subFilesOffsets, sizeof(*subFilesOffsets), numFiles, in_fp);

    // save the subfiles
    for(i = 0; i < numFiles; i++){
        fseek(in_fp, subFilesOffsets[i], SEEK_SET);
        fread(&SDT_subfileHeader, sizeof(SDT_subfileHeader), 1, in_fp);

        if((subfileData = malloc(SDT_subfileHeader.dataSize)) == NULL){
            fprintf(stderr, "\n\tCouldn't allocate %u bytes for %.16s's sound data\n", SDT_subfileHeader.dataSize, SDT_subfileHeader.fileName);
            free(subFilesOffsets);
            return false;
        }

        fread(subfileData, 1, SDT_subfileHeader.dataSize, in_fp);

        if(!save_subFile(subfileData, &SDT_subfileHeader)){
            free(subfileData);
            free(subFilesOffsets);
            return false;
        }

        free(subfileData);
    }

    free(subFilesOffsets);
    return true;
}


static bool extract_SDT2(FILE *in_fp){
    SDT_header_t            SDT_header;
    DWORD*                  subFilesOffsets;
    SDT_subfileHeader_t*    SDT_subfileHeaderArr;
    BYTE*                   subfileData;

    unsigned i, numFiles;

    // get the SDT header
    fread(&SDT_header, sizeof(SDT_header), 1, in_fp);

    numFiles = SDT_header.numFiles;

    // allocate and read the array of subfiles' offsets
    if((subFilesOffsets = malloc(numFiles * sizeof(*subFilesOffsets))) == NULL){
        fprintf(stderr, "\n\tCouldn't allocate %u bytes for %s's offsets array\n", numFiles * sizeof(*subFilesOffsets), path);
        return false;
    }
    fread(subFilesOffsets, sizeof(*subFilesOffsets), numFiles, in_fp);

    // allocate and read the array of subfiles' headers
    if((SDT_subfileHeaderArr = malloc(numFiles * sizeof(*SDT_subfileHeaderArr))) == NULL){
        fprintf(stderr, "\n\tCouldn't allocate %u bytes for %s's subfiles headers' array\n", numFiles * sizeof(*SDT_subfileHeaderArr), path);
        free(subFilesOffsets);
        return false;
    }
    fread(SDT_subfileHeaderArr, sizeof(*SDT_subfileHeaderArr), numFiles, in_fp);


    // save the subfiles
    for(i = 0; i < numFiles; i++){

        if((subfileData = malloc(SDT_subfileHeaderArr[i].dataSize)) == NULL){
            fprintf(stderr, "\n\tCouldn't allocate %u bytes for %.16s's sound data\n", SDT_subfileHeaderArr[i].dataSize, SDT_subfileHeaderArr[i].fileName);
            free(subFilesOffsets);
            free(SDT_subfileHeaderArr);
            return false;
        }
        fseek(in_fp, subFilesOffsets[i], SEEK_SET);
        fread(subfileData, 1, SDT_subfileHeaderArr[i].dataSize, in_fp);

        if(!save_subFile(subfileData, &SDT_subfileHeaderArr[i])){
            free(subfileData);
            free(subFilesOffsets);
            free(SDT_subfileHeaderArr);
            return false;
        }

        free(subfileData);
    }

    free(subFilesOffsets);
    free(SDT_subfileHeaderArr);
    return true;
}


static bool save_subFile(BYTE *subfileData, SDT_subfileHeader_t *SDT_subfileHeader){
    FILE *out_fp;

    enum fileExtension_e{
        EXT_VAG,
        EXT_MP2
    }fileExtension;

    const char *strFileExtension[2] = {".vag", ".mp2"};

    // not every filename terminates with ".mp2", ".vag", or a null-character, due to the 16 characters limit
    char *fileNameFixExt;

    switch(SDT_subfileHeader->sndFormat){
        case SNDFORMAT_VAG:
            fileExtension = EXT_VAG;
            break;

        case SNDFORMAT_MP2:
        case SNDFORMAT_MP2_2:
            fileExtension = EXT_MP2;
            break;

        default:
            fprintf(stderr, "\n\tUnknown sound format for entry %.16s (field value: 0x%04X)\n", SDT_subfileHeader->fileName, SDT_subfileHeader->sndFormat);
            return false;
    }


    // put the correct file extension at the end of the filename
    memcpy(currDirPtr, SDT_subfileHeader->fileName, sizeof(SDT_subfileHeader->fileName));

    fileNameFixExt = currDirPtr;
    while(*fileNameFixExt != '.' && *fileNameFixExt != '\0')
        ++fileNameFixExt;

    strcpy(fileNameFixExt, strFileExtension[fileExtension]);

    if((out_fp = fopen(path, "wb")) == NULL){
        fprintf(stderr, "\n\tCouldn't create file %s: %s\n", path, strerror(errno));
        return false;
    }

    // if the sound data is ADPCM we need to put a VAG header at the beginning of the file
    if(fileExtension == EXT_VAG){
        VAGhdr.dataSize = SWAP_ENDIAN32(SDT_subfileHeader->dataSize);
        VAGhdr.samplingFrequency = ((WORD)SWAP_ENDIAN16(SDT_subfileHeader->sampleRate)) << 16;
        strncpy(VAGhdr.name, SDT_subfileHeader->fileName, sizeof(VAGhdr.name));

        fwrite(&VAGhdr, sizeof(VAGhdr), 1, out_fp);
    }

    fwrite(subfileData, 1, SDT_subfileHeader->dataSize, out_fp);
    fclose(out_fp);
    return true;
}
