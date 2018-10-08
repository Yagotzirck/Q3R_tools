#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include "ssh_utils.h"
#include "tga_utils.h"
#include "types.h"


/************************* local functions' prototypes *************************/
static bool openTgaFile(sshHandle_t *sshHandle);

static void convertAndSave_shrink(sshHandle_t *sshHandle);
static void convertAndSave_asIs(sshHandle_t *sshHandle);
static void convertAndSave_truecolor_upsideDown(sshHandle_t *sshHandle);

static bool isFullOpaque(sshHandle_t *sshHandle);
static void paletteFix(sshHandle_t *sshHandle);

// functions' definitions
bool init_sshHandle(sshHandle_t *sshHandle, const char *sshPath){
    FILE    *in_fp;

    DWORD           imgDataSize;
    sshImgType_t    imgType;

    DWORD           paletteDataSize;
    DWORD           paletteNumEntriesRead;

    long            footerBytesToRead;

    // open the ssh file
    if((in_fp = fopen(sshPath, "rb")) == NULL){
        fprintf(stderr, "Couldn't open %s: %s\n", sshPath, strerror(errno));
        return false;
    }

    // initialize fields

    // read main header
    fread(&(sshHandle->mainHdr), sizeof(sshHandle->mainHdr), 1, in_fp);
    if(sshHandle->mainHdr.magic != SSH_MAGICID){
        fprintf(stderr, "%s isn't a valid SSH file\n", sshPath);
        fclose(in_fp);
        return false;
    }

    if(sshHandle->mainHdr.numResources > 1)
        fprintf(stderr, "Warning: %s contains more than one image (%u images reported in the header)\n", sshPath, sshHandle->mainHdr.numResources);

    /* read the resource entry header, then skip to the resource data header offset specified in this header
    ** (between resEntry and resHdr there's a "Buy ERTS" string without null-termination, sometimes followed by a series of
    ** 0x00 values; nothing to care about)
    */
    fread(&(sshHandle->resEntry), sizeof(sshHandle->resEntry), 1, in_fp);
    fseek(in_fp, sshHandle->resEntry.dataOffset, SEEK_SET);

    // read the resource data header
    fread(&(sshHandle->resHdr), sizeof(sshHandle->resHdr), 1, in_fp);

    imgType     = sshHandle->resHdr.nextHdrOffset_plus_imgType & 0xFF;
    imgDataSize = (sshHandle->resHdr.nextHdrOffset_plus_imgType >> 8);

    /* some image headers report zero in the nextHdrOffset field; this kind of images seem to be truecolor 32bpp
    ** and without the footer header at the end of the file (image data goes all the way to the end of the file).
    ** However, it's better to handle all possible cases, to be completely sure.
    */
    if(imgDataSize == 0){
        imgDataSize = sshHandle->resHdr.width * sshHandle->resHdr.height;

        switch(imgType){
        case SSH_PALETTED_4BPP:
            imgDataSize = (imgDataSize + 1) / 2;
            break;

        case SSH_PALETTED_8BPP:
            break;

        case SSH_TRUECOLOR_24BPP:
            imgDataSize *= sizeof(sshPixel24_t);
            break;

        case SSH_TRUECOLOR_32BPP:
            imgDataSize *= sizeof(sshPixel32_t);
            break;

        default:
            fprintf(stderr,"%s's image type is unknown (%u)\n", sshPath, imgType);
            fclose(in_fp);
            return false;
        }
    }
    else    // if it's nonzero, it includes the size of the header which needs to be removed
        imgDataSize -= sizeof(sshHandle->resHdr);


    // read the image data
    if((sshHandle->imgData = malloc(imgDataSize)) == NULL){
        fprintf(stderr, "Couldn't allocate %u bytes for %s's image data\n", imgDataSize, sshPath);
        fclose(in_fp);
        return false;
    }
    fread(sshHandle->imgData, 1, imgDataSize, in_fp);


    /* read palette header and palette(if the image is paletted, that is);
    ** also, make sure imgType holds a supported value
    */
    switch(imgType){
        case SSH_PALETTED_4BPP:
        case SSH_PALETTED_8BPP:
            fread(&(sshHandle->paletteHdr), sizeof(sshHandle->paletteHdr), 1, in_fp);

            paletteDataSize = (sshHandle->paletteHdr.nextHdrOffset_plus_unk >> 8);

            if(paletteDataSize == 0){
                /* Sometimes there are more palette entries in the file than the reported value
                ** in palNumEntries, and it's better to read them all (even though the image's
                ** pixel indexes never go beyond palNumEntries' reported value)
                */
                paletteDataSize = sshHandle->mainHdr.sshSize - ftell(in_fp);
                // we don't want any overflow
                if(paletteDataSize > sizeof(sshHandle->palette))
                   paletteDataSize = sizeof(sshHandle->palette);
            }
            else    // if it's nonzero, it includes the size of the header which needs to be removed
                paletteDataSize -= sizeof(sshHandle->paletteHdr);


            paletteNumEntriesRead = paletteDataSize / sizeof(sshHandle->palette[0]);
            fread(sshHandle->palette, sizeof(sshHandle->palette[0]), paletteNumEntriesRead, in_fp);

            break;

        case SSH_TRUECOLOR_24BPP:
        case SSH_TRUECOLOR_32BPP:
            paletteNumEntriesRead = 0;
            sshHandle->paletteHdr.palNumEntries = 0;
            break;

        default:
            fprintf(stderr,"%s's image type is unknown (%u)\n", sshPath, imgType);
            fclose(in_fp);
            free(sshHandle->imgData);
            return false;
    }

    /* read the footer header at the end of the file;
    ** initialize header's fields first, in case we don't read anything
    ** due to the header's absence
    */
    sshHandle->footerHdr.unk = 0;
    sshHandle->footerHdr.fileName[0] = '\0';


    // we don't want any buffer overflow
    footerBytesToRead = sshHandle->mainHdr.sshSize - ftell(in_fp);
    if(footerBytesToRead > sizeof(sshHandle->footerHdr))
       footerBytesToRead = sizeof(sshHandle->footerHdr);

    fread(&(sshHandle->footerHdr), 1, footerBytesToRead, in_fp);


    /* initialize the other fields in the handle structure not directly tied
    ** to the ssh file's structures
    */
    sshHandle->imgDataSize =            imgDataSize;
    sshHandle->imgType =                imgType;
    sshHandle->paletteNumEntriesRead =  paletteNumEntriesRead;
    sshHandle->sshPath =                sshPath;

    sshHandle->tgaImgBuf =              NULL; // it will be properly initialized by ssh_convertAndSave()
    sshHandle->tgaExtraBuf =            NULL; // same as above(if used)

    sshHandle->tga_fp =                 NULL; // it will be properly initialized by openTgaFile()

    fclose(in_fp);
    return true;
}


bool ssh_convertAndSave(sshHandle_t *sshHandle, outFormat_t outFormat){
    DWORD tgaBufSize = sshHandle->imgDataSize;

    // allocate/initialize tga's pixel buffer
    switch(sshHandle->imgType){
        case SSH_PALETTED_4BPP:
            /* Unfortunately, TGA doesn't support 4bpp format, so if
            ** SSH data is 4bpp we must convert it to 8 bpp
            */
            tgaBufSize *= 2;
            if( (sshHandle->tgaImgBuf = malloc(tgaBufSize)) == NULL){
                fprintf(stderr, "\n\tCouldn't allocate %u bytes for tga's pixel buffer\n", tgaBufSize);
                return false;
            }
        {
            BYTE *tgaData = sshHandle->tgaImgBuf;
            BYTE *sshData = sshHandle->imgData;

            DWORD sshDataSize = sshHandle->imgDataSize;

            DWORD i = 0;
            DWORD j = 0;

            while(i < sshDataSize){
                tgaData[j++] = sshData[i] & 0xF; // take the low nibble
                tgaData[j++] = sshData[i] >>  4; // take the high nibble
                ++i;
            }
        }
            break;

        /* if it's 8bpp there's no need to allocate anything;
        ** we can use ssh's pixel buffer directly, since it consists of palette indexes.
        */
        case SSH_PALETTED_8BPP:
            sshHandle->tgaImgBuf = sshHandle->imgData;
            // palette needs to be fixed for 8bpp entries
            paletteFix(sshHandle);
            break;

        case SSH_TRUECOLOR_24BPP:
        case SSH_TRUECOLOR_32BPP:
            if( (sshHandle->tgaImgBuf = malloc(tgaBufSize)) == NULL){
                fprintf(stderr, "\n\tCouldn't allocate %u bytes for tga's pixel buffer\n", tgaBufSize);
                return false;
            }
            break;
    }

    // create tga file
    if(!openTgaFile(sshHandle))
        return false;

    switch(outFormat){
        case OUT_SHRINK:
            /* RLE encoding can result in bigger data size than the unencoded input data, so it's a good idea
            ** to make the buffer for the encoded data twice as big as the input data size and check the
            ** result afterwards
            */
            if( (sshHandle->tgaExtraBuf = malloc(tgaBufSize * 2)) == NULL){
                fprintf(stderr, "\n\tCouldn't allocate %u bytes for tga's shrunk pixel buffer\n", tgaBufSize * 2);
                return false;
            }
            convertAndSave_shrink(sshHandle);
            break;

        case OUT_AS_IS:
            convertAndSave_asIs(sshHandle);
            break;

        case OUT_TRUECOLOR_UPSIDEDOWN:
            /* if the image is paletted, tgaExtraBuf must be 4 times larger than the
            ** original image data size, since it will be converted to truecolor 32bpp
            */
            if(sshHandle->paletteNumEntriesRead){
                DWORD tgaExtraBufSize = tgaBufSize * sizeof(tgaPixel32_t);

                if( (sshHandle->tgaExtraBuf = malloc(tgaExtraBufSize)) == NULL){
                    fprintf(stderr, "\n\tCouldn't allocate %u bytes for tga's truecolor upside-down pixel buffer\n", tgaExtraBufSize);
                    return false;
                }
            }
            convertAndSave_truecolor_upsideDown(sshHandle);
            break;
    }

    return true;
}

void free_sshHandleBuffers(sshHandle_t *sshHandle){
    /* if ssh's image type is paletted 8bpp, tgaImgBuf points to the same buffer
    ** imgData points to, so we change it to NULL to avoid doing a double-free
    */
    if(sshHandle->imgType == SSH_PALETTED_8BPP)
        sshHandle->tgaImgBuf = NULL;

    free(sshHandle->imgData);
    free(sshHandle->tgaImgBuf);
    free(sshHandle->tgaExtraBuf);

    if(sshHandle->tga_fp != NULL)
        fclose(sshHandle->tga_fp);
}



/************************* local functions' definitions *************************/
static bool openTgaFile(sshHandle_t *sshHandle){
    char    outFilename[FILENAME_MAX];
    char *  filenameEndPtr;
    char *  extPtr; // pointer to file extension that will be replaced to .tga

    strcpy(outFilename, sshHandle->sshPath);
    filenameEndPtr = outFilename + strlen(outFilename);
    extPtr = filenameEndPtr;

    do
        --extPtr;
    while(  *extPtr != '.'  &&
            *extPtr != '\\' &&
            *extPtr != '/'  &&
             extPtr != outFilename
    );

    // if the ssh file has no extension, append the .tga extension at the end of the filename
    if(*extPtr != '.')
        extPtr = filenameEndPtr;

    strcpy(extPtr, ".tga");

    if( (sshHandle->tga_fp = fopen(outFilename, "wb") ) == NULL){
        fprintf(stderr, "\n\tCouldn't create file %s: %s\n", outFilename, strerror(errno));
        return false;
    }

    return true;
}



static void convertAndSave_shrink(sshHandle_t *sshHandle){
    unsigned i;

    /* some aliases to avoid bloating the code too much with long variable names
    ** which include the structure(s) they belong to
    */
    BYTE        *sshData = sshHandle->imgData;
    BYTE        *tgaData = sshHandle->tgaImgBuf;
    BYTE        *tgaShrunkData = sshHandle->tgaExtraBuf;

    BYTE        *bufToWrite;
    DWORD       bufToWrite_size;

    DWORD       sshDataSize = sshHandle->imgDataSize;
    DWORD       tgaDataSize;
    int         tgaDataShrunkSize;


    DWORD width  = sshHandle->resHdr.width;
    DWORD height = sshHandle->resHdr.height;
    WORD numPalEntries = sshHandle->paletteHdr.palNumEntries;
    FILE *tga_fp = sshHandle->tga_fp;   // file pointer (previously opened)

    // fields used for truecolor images only
    DWORD numPixels;
    sshPixel24_t *ssh24Data;
    tgaPixel24_t *tga24Data;
    sshPixel32_t *ssh32Data;
    tgaPixel32_t *tga32Data;

    // tga structure to be passed to tga_initHdr()
    tgaInitStruct_t tgaInitStruct;
    tgaInitStruct.width = width;
    tgaInitStruct.height = height;


    switch(sshHandle->imgType){
        case SSH_PALETTED_4BPP:
        case SSH_PALETTED_8BPP:
            tgaDataSize = width * height;
            tgaInitStruct.PixelDepth = 8;
            tgaInitStruct.isCMapped = PALETTED;

            if(isFullOpaque(sshHandle)){
                tgaInitStruct.CMapDepth = 24;
                tgaInitStruct.ImageDesc = ATTRIB_BITS_0 | TOP_LEFT;
                tga_sshToTgaPal24(sshHandle->palette, numPalEntries);
            }
            else{
                tgaInitStruct.CMapDepth = 32;
                tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | TOP_LEFT;
                tga_sshToTgaPal32(sshHandle->palette, numPalEntries);
            }

            // compress image data
            tgaDataShrunkSize = tga_shrink8bpp(tgaShrunkData, tgaData, tgaDataSize, tgaInitStruct.CMapDepth, &numPalEntries);
            tgaInitStruct.CMapLen = numPalEntries;


            // if RLE encoding resulted in increased size, save uncompressed data
            if(tgaDataShrunkSize == -1){
                bufToWrite = tgaData;
                bufToWrite_size = tgaDataSize;
                tgaInitStruct.imgType = IMGTYPE_COLORMAPPED;
            }
            // otherwise, save compressed data
            else{
                bufToWrite = tgaShrunkData;
                bufToWrite_size = tgaDataShrunkSize;
                tgaInitStruct.imgType = IMGTYPE_COLORMAPPED_RLE;
            }

            break;

        case SSH_TRUECOLOR_24BPP:
            tgaDataSize = sshDataSize;
            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.PixelDepth = 24;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_0 | TOP_LEFT;

            // convert image from ssh to tga pixel format
            numPixels = sshDataSize / sizeof(sshPixel24_t);
            ssh24Data = sshData;
            tga24Data = tgaData;

            for(i = 0; i < numPixels; ++i){
                tga24Data[i].red   =    ssh24Data[i].red;
                tga24Data[i].green =    ssh24Data[i].green;
                tga24Data[i].blue  =    ssh24Data[i].blue;
            }

            // compress image data
            tgaDataShrunkSize = tga_shrink24bpp(tgaShrunkData, tga24Data, tgaDataSize);

            // if RLE encoding resulted in increased size, save uncompressed data
            if(tgaDataShrunkSize == -1){
                bufToWrite = tgaData;
                bufToWrite_size = tgaDataSize;
                tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            }
            // otherwise, save compressed data
            else{
                bufToWrite = tgaShrunkData;
                bufToWrite_size = tgaDataShrunkSize;
                tgaInitStruct.imgType = IMGTYPE_TRUECOLOR_RLE;
            }

            break;

        case SSH_TRUECOLOR_32BPP:
            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;

            // convert image from ssh to tga pixel format
            if(isFullOpaque(sshHandle)){
                tgaDataSize = (sshDataSize * sizeof(tgaPixel24_t) ) / sizeof(tgaPixel32_t);
                tgaInitStruct.PixelDepth = 24;
                tgaInitStruct.ImageDesc = ATTRIB_BITS_0 | TOP_LEFT;

                numPixels = sshDataSize / sizeof(sshPixel32_t);
                ssh32Data = sshData;
                tga24Data = tgaData;

                for(i = 0; i < numPixels; ++i){
                    tga24Data[i].red   =    ssh32Data[i].red;
                    tga24Data[i].green =    ssh32Data[i].green;
                    tga24Data[i].blue  =    ssh32Data[i].blue;
                }

                // compress image data
                tgaDataShrunkSize = tga_shrink24bpp(tgaShrunkData, tga24Data, tgaDataSize);
            }
            else{
                tgaDataSize = sshDataSize;
                tgaInitStruct.PixelDepth = 32;
                tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | TOP_LEFT;
                numPixels = sshDataSize / sizeof(sshPixel32_t);
                ssh32Data = sshData;
                tga32Data = tgaData;

                for(i = 0; i < numPixels; ++i){
                    tga32Data[i].red   =    ssh32Data[i].red;
                    tga32Data[i].green =    ssh32Data[i].green;
                    tga32Data[i].blue  =    ssh32Data[i].blue;
                    tga32Data[i].alpha =    ssh32Data[i].alpha;
                }

                // compress image data
                tgaDataShrunkSize = tga_shrink32bpp(tgaShrunkData, tga32Data, tgaDataSize);
            }

            // if RLE encoding resulted in increased size, save uncompressed data
            if(tgaDataShrunkSize == -1){
                bufToWrite = tgaData;
                bufToWrite_size = tgaDataSize;
                tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            }
            // otherwise, save compressed data
            else{
                bufToWrite = tgaShrunkData;
                bufToWrite_size = tgaDataShrunkSize;
                tgaInitStruct.imgType = IMGTYPE_TRUECOLOR_RLE;
            }

            break;
    }

    // save the tga file
    tga_initHdr(&tgaInitStruct);
    tga_writeHdr(tga_fp);

    if(numPalEntries){
        if(tgaInitStruct.CMapDepth == 24)
            tga_writeShrunkPalette24(tga_fp);
        else
            tga_writeShrunkPalette32(tga_fp);
    }

    fwrite(bufToWrite, 1, bufToWrite_size, tga_fp);
}

static void convertAndSave_asIs(sshHandle_t *sshHandle){
    unsigned i;
    /* some aliases to avoid bloating the code too much with long variable names
    ** which include the structure(s) they belong to
    */
    BYTE        *sshData = sshHandle->imgData;
    BYTE        *tgaData = sshHandle->tgaImgBuf;

    DWORD       sshDataSize = sshHandle->imgDataSize;
    DWORD       tgaDataSize;

    DWORD width  = sshHandle->resHdr.width;
    DWORD height = sshHandle->resHdr.height;
    DWORD numPalEntries = sshHandle->paletteHdr.palNumEntries;
    FILE *tga_fp = sshHandle->tga_fp;   // file pointer (previously opened)

    // fields used for truecolor images only
    DWORD numPixels;
    sshPixel24_t *ssh24Data;
    tgaPixel24_t *tga24Data;
    sshPixel32_t *ssh32Data;
    tgaPixel32_t *tga32Data;

    // tga structure to be passed to tga_initHdr()
    tgaInitStruct_t tgaInitStruct;
    tgaInitStruct.width = width;
    tgaInitStruct.height = height;


    switch(sshHandle->imgType){
        case SSH_PALETTED_4BPP:
        case SSH_PALETTED_8BPP:
            tgaDataSize = width * height;

            tgaInitStruct.PixelDepth = 8;
            tgaInitStruct.isCMapped = PALETTED;
            tgaInitStruct.imgType = IMGTYPE_COLORMAPPED;
            tgaInitStruct.CMapDepth = 32;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | TOP_LEFT;
            tgaInitStruct.CMapLen = numPalEntries;

            tga_sshToTgaPal32(sshHandle->palette, numPalEntries);

            break;

        case SSH_TRUECOLOR_24BPP:
            tgaDataSize = sshDataSize;

            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            tgaInitStruct.PixelDepth = 24;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_0 | TOP_LEFT;

            // convert image from ssh to tga pixel format
            numPixels = sshDataSize / sizeof(sshPixel24_t);
            ssh24Data = sshData;
            tga24Data = tgaData;

            for(i = 0; i < numPixels; ++i){
                tga24Data[i].red   =    ssh24Data[i].red;
                tga24Data[i].green =    ssh24Data[i].green;
                tga24Data[i].blue  =    ssh24Data[i].blue;
            }

            break;

        case SSH_TRUECOLOR_32BPP:
            tgaDataSize = sshDataSize;

            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;
            tgaInitStruct.PixelDepth = 32;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | TOP_LEFT;

            // convert image from ssh to tga pixel format
            numPixels = sshDataSize / sizeof(sshPixel32_t);
            ssh32Data = sshData;
            tga32Data = tgaData;

            for(i = 0; i < numPixels; ++i){
                tga32Data[i].red   =    ssh32Data[i].red;
                tga32Data[i].green =    ssh32Data[i].green;
                tga32Data[i].blue  =    ssh32Data[i].blue;
                tga32Data[i].alpha =    ssh32Data[i].alpha;
            }

            break;
    }

    // save the tga file
    tga_initHdr(&tgaInitStruct);
    tga_writeHdr(tga_fp);

    if(numPalEntries)
        tga_writePalette32(tga_fp);


    fwrite(tgaData, 1, tgaDataSize, tga_fp);
}

static void convertAndSave_truecolor_upsideDown(sshHandle_t *sshHandle){
    unsigned i;

    /* some aliases to avoid bloating the code too much with long variable names
    ** which include the structure(s) they belong to
    */
    BYTE    *sshData = sshHandle->imgData;
    BYTE    *tgaData = sshHandle->tgaImgBuf;

    BYTE    *bufToWrite;
    DWORD   bufToWrite_size;

    DWORD   sshDataSize = sshHandle->imgDataSize;

    DWORD width  = sshHandle->resHdr.width;
    DWORD height = sshHandle->resHdr.height;
    DWORD numPalEntries = sshHandle->paletteHdr.palNumEntries;
    FILE *tga_fp = sshHandle->tga_fp;   // file pointer (previously opened)

    // fields used for truecolor images only
    sshPixel24_t *ssh24Data;
    tgaPixel24_t *tga24Data;
    sshPixel32_t *ssh32Data;
    tgaPixel32_t *tga32Data;

    tgaPixel24_t *tgaCurrRow24;
    tgaPixel32_t *tgaCurrRow32;

    // tga structure to be passed to tga_initHdr()
    tgaInitStruct_t tgaInitStruct;
    tgaInitStruct.width = width;
    tgaInitStruct.height = height;

    /* buffer for palette converted to tga's pixel format
    ** (used for converting paletted images to truecolor)
    */
    tgaPixel32_t tgaPal[256];


    switch(sshHandle->imgType){
        case SSH_PALETTED_4BPP:
        case SSH_PALETTED_8BPP:
            bufToWrite = sshHandle->tgaExtraBuf;
            bufToWrite_size = width * height * sizeof(tgaPixel32_t);

            tgaInitStruct.PixelDepth = 32;
            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | BOTTOM_LEFT;
            tgaInitStruct.CMapLen = 0;

            // convert palette to tga's pixel format
            for(i = 0; i < numPalEntries; ++i){
                tgaPal[i].blue = sshHandle->palette[i].blue;
                tgaPal[i].green = sshHandle->palette[i].green;
                tgaPal[i].red = sshHandle->palette[i].red;
                tgaPal[i].alpha = sshHandle->palette[i].alpha;
            }

            /* convert indexes to truecolor pixels;
            ** also, put them in upside-down row order
            */
            tga32Data = bufToWrite;

            // let tgaCurrRow32 point to the last row of the dest. buffer
            tgaCurrRow32 = &tga32Data[width * (height - 1)];

            do{
                for(int y = 0; y < width; ++y)
                    tgaCurrRow32[y] = tgaPal[*tgaData++];
                tgaCurrRow32 -= width;
            }while(tgaCurrRow32 >= tga32Data);

            break;


        case SSH_TRUECOLOR_24BPP:
            bufToWrite = tgaData;
            bufToWrite_size = sshDataSize;

            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            tgaInitStruct.PixelDepth = 24;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_0 | BOTTOM_LEFT;

            /* convert image from ssh to tga pixel format;
            ** also, put rows in upside-down order
            */
            ssh24Data = sshData;
            tga24Data = tgaData;


            // let tgaCurrRow24 point to the last row of the dest. buffer
            tgaCurrRow24 = &tga24Data[width * (height - 1)];

            do{
                for(int y = 0; y < width; ++y){
                    tgaCurrRow24[y].red = ssh24Data->red;
                    tgaCurrRow24[y].green = ssh24Data->green;
                    tgaCurrRow24[y].blue = ssh24Data->blue;

                    ++ssh24Data;
                }
                tgaCurrRow24 -= width;
            }while(tgaCurrRow24 >= tga24Data);

            break;


        case SSH_TRUECOLOR_32BPP:
            bufToWrite = tgaData;
            bufToWrite_size = sshDataSize;

            tgaInitStruct.isCMapped = NO_PALETTE;
            tgaInitStruct.imgType = IMGTYPE_TRUECOLOR;
            tgaInitStruct.CMapDepth = 0;
            tgaInitStruct.CMapLen = 0;


            tgaInitStruct.PixelDepth = 32;
            tgaInitStruct.ImageDesc = ATTRIB_BITS_8 | BOTTOM_LEFT;

           /* convert image from ssh to tga pixel format;
            ** also, put rows in upside-down order
            */
            ssh32Data = sshData;
            tga32Data = tgaData;

            // let tgaCurrRow32 point to the last row of the dest. buffer
            tgaCurrRow32 = &tga32Data[width * (height - 1)];

            do{
                for(int y = 0; y < width; ++y){
                    tgaCurrRow32[y].red = ssh32Data->red;
                    tgaCurrRow32[y].green = ssh32Data->green;
                    tgaCurrRow32[y].blue = ssh32Data->blue;
                    tgaCurrRow32[y].alpha = ssh32Data->alpha;

                    ++ssh32Data;
                }
                tgaCurrRow32 -= width;
            }while(tgaCurrRow32 >= tga32Data);

            break;
    }

    // save the tga file
    tga_initHdr(&tgaInitStruct);
    tga_writeHdr(tga_fp);
    fwrite(bufToWrite, 1, bufToWrite_size, tga_fp);
}




static bool isFullOpaque(sshHandle_t *sshHandle){
    sshPixel32_t *pixelData;
    DWORD i, numEntries;


    // if the image is paletted, check the palette
    if(sshHandle->paletteNumEntriesRead){
        pixelData = sshHandle->palette;
        numEntries = sshHandle->paletteNumEntriesRead;
    }

    // if it's truecolor, check pixel data
    else{
        pixelData = sshHandle->imgData;
        numEntries = sshHandle->imgDataSize;
    }

    for(i = 0; i < numEntries; ++i)
        if(pixelData[i].alpha != 0xFF)
           return false;

    return true;
}


/* The palette for 8bpp entries must be adjusted as shown below;
** If I were to take a guess about the reason why it's organized like that,
** I'd say it's probably something related to this:
** https://en.wikipedia.org/wiki/Z-order_curve
*/
static void paletteFix(sshHandle_t *sshHandle){
    sshPixel32_t    *sshPalette = sshHandle->palette;
    DWORD           numEntries  = sshHandle->paletteNumEntriesRead;

    sshPixel32_t paletteSwapBuf[8];

    unsigned palIndex = 8;

    do{
        memcpy(&paletteSwapBuf, &sshPalette[palIndex], sizeof(sshPalette[0]) * 8);
        memcpy(&sshPalette[palIndex], &sshPalette[palIndex + 8], sizeof(sshPalette[0]) * 8);
        memcpy(&sshPalette[palIndex + 8], &paletteSwapBuf, sizeof(sshPalette[0]) * 8);

        palIndex += 32;
    } while(palIndex < numEntries);
}
