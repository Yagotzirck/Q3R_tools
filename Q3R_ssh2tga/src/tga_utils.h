#ifndef TGA_UTILS_H
#define TGA_UTILS_H

#include <stdio.h>

#include "types.h"

enum tgaImageDescriptor{
    ATTRIB_BITS_0 =    0,
    ATTRIB_BITS_8 =    8,
    BOTTOM_LEFT =   0x00,
    TOP_LEFT    =   0x20
};

enum tgaImageType{
    IMGTYPE_COLORMAPPED =       1,
    IMGTYPE_TRUECOLOR =         2,
    IMGTYPE_COLORMAPPED_RLE =   9,
    IMGTYPE_TRUECOLOR_RLE =    10
};

enum tgaColorMap{
    NO_PALETTE =    0,
    PALETTED =      1
};

typedef struct tgaPixel24_s{
    BYTE blue, green, red;
}tgaPixel24_t;

typedef struct tgaPixel32_s{
    BYTE blue, green, red, alpha;
}tgaPixel32_t;

typedef struct tgaInitStruct_s{
    enum tgaColorMap isCMapped;
    enum tgaImageType imgType;
    WORD CMapLen;
    BYTE CMapDepth;
    WORD width;
    WORD height;
    BYTE PixelDepth;
    enum tgaImageDescriptor ImageDesc;
}tgaInitStruct_t;



/* function prototypes */
void tga_sshToTgaPal24(const sshPixel32_t *ssh_palette, DWORD numPalEntries);
void tga_sshToTgaPal32(const sshPixel32_t *ssh_palette, DWORD numPalEntries);
void tga_initHdr(tgaInitStruct_t *tgaInitStruct);
void tga_writeHdr(FILE *stream);
void tga_writePalette24(FILE *stream);
void tga_writeShrunkPalette24(FILE *stream);
void tga_writePalette32(FILE *stream);
void tga_writeShrunkPalette32(FILE *stream);
int tga_shrink8bpp(BYTE imgDest[], BYTE imgBuf[], DWORD size, DWORD CMapDepth, WORD *CMapLen);
int tga_shrink24bpp(BYTE imgDest[], const tgaPixel24_t imgBuf[], DWORD size);
int tga_shrink32bpp(BYTE imgDest[], const tgaPixel32_t imgBuf[], DWORD size);


#endif /* TGA_UTILS_H */
