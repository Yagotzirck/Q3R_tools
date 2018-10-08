#include <stdio.h>
#include <string.h>

#include "tga_utils.h"
#include "types.h"

#define PIXELS_EQUAL(px1,px2,pxSize) (!memcmp((px1), (px2), (pxSize)))

typedef struct _TgaHeader
{
  BYTE IDLength;        /* 00h  Size of Image ID field */
  BYTE ColorMapType;    /* 01h  Color map type */
  BYTE ImageType;       /* 02h  Image type code */
  WORD CMapStart;       /* 03h  Color map origin */
  WORD CMapLength;      /* 05h  Color map length */
  BYTE CMapDepth;       /* 07h  Depth of color map entries */
  WORD XOffset;         /* 08h  X origin of image */
  WORD YOffset;         /* 0Ah  Y origin of image */
  WORD Width;           /* 0Ch  Width of image */
  WORD Height;          /* 0Eh  Height of image */
  BYTE PixelDepth;      /* 10h  Image pixel size */
  BYTE ImageDescriptor; /* 11h  Image descriptor byte */
} TGAHEAD;



/* global variables (file scope only) */
static TGAHEAD tga_header;

static struct tgaPixel24_s tga_palette24[256], tga_shrunk_palette24[256];
static struct tgaPixel32_s tga_palette32[256], tga_shrunk_palette32[256];


/* local functions declarations */
static WORD shrink_palette24(BYTE imgBuf[], DWORD size, BYTE used_indexes[]);
static WORD shrink_palette32(BYTE imgBuf[], DWORD size, BYTE used_indexes[]);


/* functions definitions */
void tga_sshToTgaPal24(const sshPixel32_t *ssh_palette, DWORD numPalEntries){
    unsigned int i;

    for(i = 0; i < numPalEntries; ++i){
        tga_palette24[i].blue = ssh_palette[i].blue;
        tga_palette24[i].green = ssh_palette[i].green;
        tga_palette24[i].red = ssh_palette[i].red;
    }
}

void tga_sshToTgaPal32(const sshPixel32_t *ssh_palette, DWORD numPalEntries){
    unsigned int i;

    for(i = 0; i < numPalEntries; ++i){
        tga_palette32[i].blue = ssh_palette[i].blue;
        tga_palette32[i].green = ssh_palette[i].green;
        tga_palette32[i].red = ssh_palette[i].red;
        tga_palette32[i].alpha = ssh_palette[i].alpha;
    }
}


void tga_initHdr(tgaInitStruct_t *tgaInitStruct){
    tga_header.IDLength =          0;                           /* No image ID field used, size 0 */
    tga_header.ColorMapType =      tgaInitStruct->isCMapped;
    tga_header.ImageType =         tgaInitStruct->imgType;
    tga_header.CMapStart =         0;                           /* Color map origin */
    tga_header.CMapLength =        tgaInitStruct->CMapLen;      /* Number of palette entries */
    tga_header.CMapDepth =         tgaInitStruct->CMapDepth;    /* Depth of color map entries */
    tga_header.XOffset =           0;                           /* X origin of image */
    tga_header.YOffset =           0;                           /* Y origin of image */
    tga_header.Width =             tgaInitStruct->width;        /* Width of image */
    tga_header.Height =            tgaInitStruct->height;       /* Height of image */
    tga_header.PixelDepth =        tgaInitStruct->PixelDepth;   /* Image pixel size */
    tga_header.ImageDescriptor =   tgaInitStruct->ImageDesc;
}

void tga_writeHdr(FILE *stream){
    fwrite(&tga_header.IDLength, sizeof(tga_header.IDLength), 1, stream);
    fwrite(&tga_header.ColorMapType, sizeof(tga_header.ColorMapType), 1, stream);
    fwrite(&tga_header.ImageType, sizeof(tga_header.ImageType), 1, stream);
    fwrite(&tga_header.CMapStart, sizeof(tga_header.CMapStart), 1, stream);
    fwrite(&tga_header.CMapLength, sizeof(tga_header.CMapLength), 1, stream);
    fwrite(&tga_header.CMapDepth, sizeof(tga_header.CMapDepth), 1, stream);
    fwrite(&tga_header.XOffset, sizeof(tga_header.XOffset), 1, stream);
    fwrite(&tga_header.YOffset, sizeof(tga_header.YOffset), 1, stream);
    fwrite(&tga_header.Width , sizeof(tga_header.Width), 1, stream);
    fwrite(&tga_header.Height, sizeof(tga_header.Height), 1, stream);
    fwrite(&tga_header.PixelDepth, sizeof(tga_header.PixelDepth), 1, stream);
    fwrite(&tga_header.ImageDescriptor, sizeof(tga_header.ImageDescriptor), 1, stream);
}

void tga_writePalette24(FILE *stream){
    fwrite(tga_palette24, sizeof(struct tgaPixel24_s), tga_header.CMapLength, stream);
}

void tga_writeShrunkPalette24(FILE *stream){
    fwrite(tga_shrunk_palette24, sizeof(struct tgaPixel24_s), tga_header.CMapLength, stream);
}

void tga_writePalette32(FILE *stream){
    fwrite(tga_palette32, sizeof(struct tgaPixel32_s), tga_header.CMapLength, stream);
}

void tga_writeShrunkPalette32(FILE *stream){
    fwrite(tga_shrunk_palette32, sizeof(struct tgaPixel32_s), tga_header.CMapLength, stream);
}


/* tga_shrink8bpp(): compress both palette (by deleting unused entries) and data(RLE encoding) */
int tga_shrink8bpp(BYTE imgDest[], BYTE imgBuf[], DWORD size, DWORD CMapDepth, WORD *CMapLen){
    BYTE used_indexes[256] = {0};
    unsigned int i = 0, j = 0;

    BYTE pixelCount = 0;    // this is TGA's RLE-packet counter field; the real count is pixelCount + 1

    switch(CMapDepth){
        case 24:
            *CMapLen = shrink_palette24(imgBuf, size, used_indexes);
            break;
        case 32:
            *CMapLen = shrink_palette32(imgBuf, size, used_indexes);
            break;

        default:
            return -1;
    }

    do{
        while(i + 1 < size && imgBuf[i+1] == imgBuf[i]){
            ++pixelCount;
            ++i;

            if(pixelCount == 127)
                break;
        }

        /* we got a gain (or uninflated size if 2 consecutive pixels only:
        ** pixelCount byte + pixel byte VS. pixel byte repeated 2 times)
        */
        if(pixelCount){
            imgDest[j++] =  pixelCount | 0x80;    // this is a RLE packet, so the MSB must be set
            imgDest[j++] =  used_indexes[imgBuf[i++]];
            pixelCount = 0;
        }
        else{ // check how many subsequent pixels are uncompressible by RLE
            unsigned int identicalPixelsCount = 0;

            BYTE next_pixelCount = 0;   // the starting pixelCount value for the next packet

            /* save the index in which we put pixelCount later on, once we know the pixel count value
            ** for the current non-RLE packet
            */
            unsigned int packet_pixelCountIdx = j++;

            // put the 1st uncompressible pixel in the destination buffer
            imgDest[j++] = used_indexes[imgBuf[i++]];


            /* in order to avoid inflating the image data when breaking the
            ** run of unidentical pixels, we need at least 3 consecutive identical pixels;
            ** breaking at 2 would inflate data by 1 byte due to an extra pixelCount byte.
            **
            ** As an example consider the following string, where the letters represent pixel bytes
            ** and numbers represent pixelCount bytes, e.g. "abcddefghhh" (11 bytes):
            **
            ** (For clarity's sake, pixelCount's values in this dummy example indicate the real packet's
            ** length, and not (packet length) - 1 with MSB set for RLE packets, as happens in TGA encoding)
            **
            ** - breaking at the double d's gives "3abc2d3efg3h", resulting in 12 bytes;
            ** - breaking at the triple h's gives "8abcddefg3h" , resulting in 11 bytes.
            **
            ** By breaking at the triple h's, we got the same size as the original data,
            ** thus we avoided inflating it.
            */
            do{
                if(i + 1 >= size){
                    /* if pixelCount is 0, image's last pixel has already been saved
                    ** immediately before entering this loop;
                    ** otherwise, the last pixel belongs to a pixel run and must be saved
                    */
                    if(pixelCount){
                        // put the uncompressible pixel in the destination buffer
                        imgDest[j++] = used_indexes[imgBuf[i++]];
                        ++pixelCount;
                    }
                    break;
                }

                if(imgBuf[i+1] == imgBuf[i])
                    ++identicalPixelsCount;
                else
                    identicalPixelsCount = 0;

                /* 3 subsequent identical pixels counted(possibly more after);
                ** we can break the run of uncompressible pixels without inflating the data
                */
                if(identicalPixelsCount == 2){
                    /* the last pixel which has been put in the current packet must be discarded, since it belongs
                    ** to the next RLE packet
                    */
                    --pixelCount;
                    --j;

                    /* we already counted 3 identical pixels; no need to count them again in the next
                    ** RLE counting loop
                    */
                    next_pixelCount = 2;
                    ++i;
                    break;
                }

                // put the uncompressible pixel in the destination buffer
                imgDest[j++] = used_indexes[imgBuf[i++]];
                ++pixelCount;

            }while(pixelCount < 127);

            // put pixelCount in the destination buffer for the current non-RLE packet
            imgDest[packet_pixelCountIdx] = pixelCount;

            // set the number of identical pixels counted for the next packet
            pixelCount = next_pixelCount;
        }
    }while(i < size);

    /* if the RLE compression resulted in increased size keep the data in its raw form
    ** (update the pixel indexes to the new shrunk palette first) */
    if(j >= size){
        for(i = 0; i < size; ++i)
            imgBuf[i] = used_indexes[imgBuf[i]];

        return -1;
    }

    return j;
}

int tga_shrink24bpp(BYTE imgDest[], const tgaPixel24_t imgBuf[], DWORD size){
    unsigned int i = 0, j = 0;

    BYTE pixelCount = 0;    // this is TGA's RLE-packet counter field; the real count is pixelCount + 1

    DWORD numPixels = size / sizeof(imgBuf[0]);

    do{
        while(i + 1 < numPixels && PIXELS_EQUAL(&imgBuf[i+1], &imgBuf[i], sizeof(imgBuf[0])) ){
            ++pixelCount;
            ++i;

            if(pixelCount == 127)
                break;
        }

        // we got a gain
        if(pixelCount){
            imgDest[j++] =  pixelCount | 0x80;    // this is a RLE packet, so the MSB must be set
            *(tgaPixel24_t *)(imgDest + j) = imgBuf[i++];
            j += sizeof(imgBuf[0]);

            pixelCount = 0;
        }
        else{ // check how many subsequent pixels are uncompressible by RLE
            BYTE next_pixelCount = 0;   // the starting pixelCount value for the next packet


            /* save the index in which we put pixelCount later on, once we know the pixel count value
            ** for the current non-RLE packet
            */
            unsigned int packet_pixelCountIdx = j++;

            // put the 1st uncompressible pixel in the destination buffer
            *(tgaPixel24_t *)(imgDest + j) = imgBuf[i++];
            j += sizeof(imgBuf[0]);


            /* unlike 8bpp images, each pixel occupies 3 bytes this time instead of 1, so we can break the run of
            ** unidentical pixels even when we encounter 2 identical pixels without inflating the image data.
            */
            do{
                if(i + 1 >= numPixels){
                    /* if pixelCount is 0, the last pixel has already been saved
                    ** immediately before entering this loop;
                    ** otherwise, the last pixel belongs to a pixel run and must be saved
                    */
                    if(pixelCount){
                        // put the uncompressible pixel in the destination buffer
                        *(tgaPixel24_t *)(imgDest + j) = imgBuf[i++];
                        j += sizeof(imgBuf[0]);
                        ++pixelCount;
                    }
                    break;
                }

                /* 2 subsequent identical pixels counted(possibly more after);
                ** we can break the run of uncompressible pixels
                */
                if(PIXELS_EQUAL(&imgBuf[i+1], &imgBuf[i], sizeof(imgBuf[0])) ){
                    /* we already counted 2 identical pixels; no need to count them again in the next
                    ** RLE counting loop
                    */
                    next_pixelCount = 1;
                    ++i;
                    break;
                }

                // put the uncompressible pixel in the destination buffer
                *(tgaPixel24_t *)(imgDest + j) = imgBuf[i++];
                j += sizeof(imgBuf[0]);
                ++pixelCount;

            }while(pixelCount < 127);

            // put pixelCount in the destination buffer for the current non-RLE packet
            imgDest[packet_pixelCountIdx] = pixelCount;

            // set the number of identical pixels counted for the next packet
            pixelCount = next_pixelCount;
        }


    }while(i < numPixels);

    /* if the RLE compression resulted in increased size keep the data in its raw form */
    if(j >= size)
        return -1;

    return j;
}

int tga_shrink32bpp(BYTE imgDest[], const tgaPixel32_t imgBuf[], DWORD size){
    unsigned int i = 0, j = 0;

    BYTE pixelCount = 0;    // this is TGA's RLE-packet counter field; the real count is pixelCount + 1

    DWORD numPixels = size / sizeof(imgBuf[0]);

    do{
        while(i + 1 < numPixels && PIXELS_EQUAL(&imgBuf[i+1], &imgBuf[i], sizeof(imgBuf[0])) ){
            ++pixelCount;
            ++i;

            if(pixelCount == 127)
                break;
        }

        // we got a gain
        if(pixelCount){
            imgDest[j++] =  pixelCount | 0x80;    // this is a RLE packet, so the MSB must be set
            *(tgaPixel32_t *)(imgDest + j) = imgBuf[i++];
            j += sizeof(imgBuf[0]);

            pixelCount = 0;
        }
        else{ // check how many subsequent pixels are uncompressible by RLE
            BYTE next_pixelCount = 0;   // the starting pixelCount value for the next packet


            /* save the index in which we put pixelCount later on, once we know the pixel count value
            ** for the current non-RLE packet
            */
            unsigned int packet_pixelCountIdx = j++;

            // put the 1st uncompressible pixel in the destination buffer
            *(tgaPixel32_t *)(imgDest + j) = imgBuf[i++];
            j += sizeof(imgBuf[0]);


            /* unlike 8bpp images, each pixel occupies 4 bytes this time instead of 1, so we can break the run of
            ** unidentical pixels even when we encounter 2 identical pixels without inflating the image data
            ** (in this case we actually get a gain, even if it's just one byte.)
            */
            do{
                if(i + 1 >= numPixels){
                    /* if pixelCount is 0, the last pixel has already been saved
                    ** immediately before entering this loop;
                    ** otherwise, the last pixel belongs to a pixel run and must be saved
                    */
                    if(pixelCount){
                        // put the uncompressible pixel in the destination buffer
                        *(tgaPixel32_t *)(imgDest + j) = imgBuf[i++];
                        j += sizeof(imgBuf[0]);
                        ++pixelCount;
                    }
                    break;
                }

                /* 2 subsequent identical pixels counted(possibly more after);
                ** we can break the run of uncompressible pixels
                */
                if(PIXELS_EQUAL(&imgBuf[i+1], &imgBuf[i], sizeof(imgBuf[0])) ){
                    /* we already counted 2 identical pixels; no need to count them again in the next
                    ** RLE counting loop
                    */
                    next_pixelCount = 1;
                    ++i;
                    break;
                }

                // put the uncompressible pixel in the destination buffer
                *(tgaPixel32_t *)(imgDest + j) = imgBuf[i++];
                j += sizeof(imgBuf[0]);
                ++pixelCount;

            }while(pixelCount < 127);

            // put pixelCount in the destination buffer for the current non-RLE packet
            imgDest[packet_pixelCountIdx] = pixelCount;

            // set the number of identical pixels counted for the next packet
            pixelCount = next_pixelCount;
        }


    }while(i < numPixels);

    /* if the RLE compression resulted in increased size keep the data in its raw form */
    if(j >= size)
        return -1;

    return j;
}


/* local functions definitions */
static WORD shrink_palette24(BYTE imgBuf[], DWORD size, BYTE used_indexes[]){
    unsigned int i, j;

    /* scan the whole image to track the palette colors actually used */
    for(i = 0; i < size; ++i)
        used_indexes[imgBuf[i]] = 1;

    /* remap the palette with the used palette colors placed sequentially */
    for(i = 0, j = 0; i < 256; ++i)
        if(used_indexes[i]){
            tga_shrunk_palette24[j] = tga_palette24[i];
            used_indexes[i] = j++;
        }

    return j;
}

static WORD shrink_palette32(BYTE imgBuf[], DWORD size, BYTE used_indexes[]){
    unsigned int i, j;

    /* scan the whole image to track the palette colors actually used */
    for(i = 0; i < size; ++i)
        used_indexes[imgBuf[i]] = 1;

    /* remap the palette with the used palette colors placed sequentially */
    for(i = 0, j = 0; i < 256; ++i)
        if(used_indexes[i]){
            tga_shrunk_palette32[j] = tga_palette32[i];
            used_indexes[i] = j++;
        }

    return j;
}
