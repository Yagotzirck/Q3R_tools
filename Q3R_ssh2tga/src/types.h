#ifndef TYPES_H
#define TYPES_H

#define SSH_MAGICID     0x53504853

typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;

typedef enum outFormat_e{
    OUT_SHRINK,
    OUT_AS_IS,
    OUT_TRUECOLOR_UPSIDEDOWN
}outFormat_t;


// ssh types/structures
typedef enum sshImgType_e{
    SSH_PALETTED_4BPP   = 1,
    SSH_PALETTED_8BPP   = 2,
    SSH_TRUECOLOR_24BPP = 4,
    SSH_TRUECOLOR_32BPP = 5
}sshImgType_t;

typedef struct sshPixel24_s{
    BYTE red, green, blue;
}sshPixel24_t;

typedef struct sshPixel32_s{
    BYTE red, green, blue, alpha;
}sshPixel32_t;


/*************************************************************************************
In order to reverse .ssh format I used Mr. Mouse's info I got from this post as a starting point,
then expanded/modified some bits from there:
http://forum.xentax.com/viewtopic.php?p=33277#p33277

Here's a copy of the info for quick reference:
(Doesn't entirely reflect Q3R's ssh files; for that, refer to my code/structures.)

// Header

[4]      Magic word "SHPS"
uint32      Archive size (total size of .ssh file)
uint32      Number of resources
[4]      Filename (first part)

// Resource entry (repeated 'number of resources' times)

[4]      Filename (second part)
utin32      Offset of resource data

// Resource data (each resource actually begins with 'Buy ERTS' followed by 16 zero bytes, but the offset pointers above skip this part)

[1]      Unknown, always 2
uint24      Size of the graphics bitmap part (24-bit value!) INCLUDING the 16-byte header
uint16      Width
uint16      Height
[n]      BITMAP (8-bit bitmap, of 256 colours)

[1]      Unknown, always 0x21
uint32      Size of colour table INCLUDING the 16-byte header
[11]      Unknown, always the same, possibly one value is also the number of colours (256)
[n]      Palette (RGBA format, where A is often 0x80)
[16]      Another row of values
[240]      "EAGL240 metal bin attachment for runtime texture management" followed by zero bytes
[32]      tail values

*************************************************************************************/


typedef struct sshMainHdr_s{
    DWORD magic;            // "SHPS" (no null-termination character), or 0x53504853 (little endian)
    DWORD sshSize;          // total size
    DWORD numResources;     // seems to be always 1, so we can assume that each Q3R .ssh file contains just one image
    char fileName1[4];      // always "GIMX" (no null-termination character), apparently
}sshMainHdr_t;

typedef struct sshResEntry_s{
    char fileName2[4];      // the first 4 characters of the same .ssh filename
    DWORD dataOffset;
}sshResEntry_t;


/* I know, I know: using bitfields when dealing with files' structures isn't the best
** of practices; as a matter of fact, I'm not using bitfields at all in the code,
** it's there just to better document the file format. To be even more pedantic:
** - imgType occupies the least significant bits (0-7);
** - nextHdrOffset occupies the most significant bits (8-31).
**
** The fields themselves are extracted with a bitmask/bitshift combo on the other field
** inside the union, which should be a safer and more portable approach than bitfields.
**
*/
typedef struct sshResHdr_s{
    union{
        struct{
            DWORD imgType:      8;      // paletted 4bpp = 1, paletted 8bpp = 2, truecolor 24bpp = 4, truecolor 32bpp = 5
            DWORD nextHdrOffset: 24;    // relative to this header; if it's 0, then the file ends with the image data
        };
        // we'll use this field to extract the data represented in the bitfield struct above
        DWORD nextHdrOffset_plus_imgType;
    };

    WORD width;
    WORD height;
    DWORD unk1;     // seems to be always zero
    WORD  unk2;     // as above
    BYTE  unk3;     // as above

    /* I'm putting this field here for documentation purposes only; I'm not interested in extracting
    ** mipmap subimages, since it doesn't make any sense in my opinion (they're just lower resolution
    ** versions of the main image.)
    ** For those interested in extracting them for whatever reason, it works like this:
    **
    ** The mipmap images' data is located immediately after the main image data; each mipmap's data
    ** is placed one after the other, with width and height being half the size of the
    ** previous image.
    **
    ** As an example, let's assume that the main image is 64x256 and the numMipMaps field is 4; then
    ** - The 1st mipmap is 32x128;
    ** - The 2st mipmap is 16x64;
    ** - The 3rd mipmap is 8x32;
    ** - the 4th mipmap is 4x16.
    **
    ** After the end of the last mipmap's data there's usually the palette header, since I haven't seen any
    ** truecolor .ssh files including mipmaps.
    **
    ** Of course, if the numMipMaps field is 0, then no mipmap images are present.
    */
    union{
        struct{
            BYTE lastHdrByte_unk:   4;  // low nibble, seems to be always zero
            BYTE numMipMaps:        4;  // number of mipmaps after the main image, high nibble
        };
        /* we'll use this field to extract the data represented in the bitfield struct above
        ** (actually I'm completely ignoring this, as already stated.)
        */
        BYTE numMipMaps_plus_unk;
    };
}sshResHdr_t;


/* the following header follows the image's pixel data indexes and precedes the palette;
** it's present only for paletted images, obviously.
** As for the bitfield struct below, what has already been said about sshResHdr_s's bitfield
** still applies. Also:
** - unk occupies the least significant bits (0-7);
** - nextHdrOffset occupies the most significant bits (8-31).
**
*/
typedef struct sshPaletteHdr_s{
    union{
        struct{
            DWORD unk:              8;      // seems to be always 0x21, maybe a marker/magic ID for this header?
            DWORD nextHdrOffset:    24;     // relative to this header; if it's 0, then the file ends with the palette data
        };
        // we'll use this field to extract the data represented in the bitfield struct above
        DWORD nextHdrOffset_plus_unk;
    };

    /* It looks like this header treats the palette as a 32bpp standalone image,
    ** with the following 2 fields indicating its width(which seems to always be the same value
    ** as the palNumEntries field) and height(which seems to be always 1).
    ** As for palNumEntries, sometimes there are more entries included in the palette than the
    ** value reported here, but the image's pixel indexes never seem to go beyond this value.
    */
    WORD palWidth;
    WORD palHeight;
    WORD palNumEntries;
    WORD    unk2;       // seems to be always zero
    DWORD   unk3;       // seems to be almost always 0x2000
}sshPaletteHdr_t;

/* The following header is located (as the name implies) at the end of the file,
** immediately after the palette(for paletted images) or pixel entries(for truecolor images).
** If nextHdrOffset field from the last header before this position (sshPaletteHdr_t if paletted,
** sshResHdr_t if truecolor) is 0, then this header isn't present at all.
*/
typedef struct sshFooterHdr_s{
    DWORD   unk;    // seems to be always 0x70, maybe a marker/magic ID for this header?
    /* fileName is kinda weird: it contains the name which is the same as the .ssh filename
    ** without the .ssh extension, then it's always followed by 12 whitespace (0x20) characters,
    ** then it contains a series of null(0x00) characters used to round the .ssh file size up to the next
    ** multiple of 16 boundary.
    */
    char    fileName[60];   // 60 ought to be enough to acquire any possible variable lengths of what I've described above
}sshFooterHdr_t;


/* structure representing the .ssh file;
** each field/structure is placed in the same order as it appears
** inside the file.
*/
typedef struct sshHandle_s{
    sshMainHdr_t    mainHdr;
    sshResEntry_t   resEntry;
    sshResHdr_t     resHdr;
    BYTE*           imgData;            // this needs to be malloc'd
    sshPaletteHdr_t paletteHdr;         // obviously this is ignored/absent for truecolor images
    sshPixel32_t    palette[256];       // as above, also not necessarily 256 entries big
    sshFooterHdr_t  footerHdr;

    /* some fields that are not present in the .ssh file, but which will make data computation
    ** between functions easier nonetheless
    */
    DWORD           imgDataSize;
    sshImgType_t    imgType;

    DWORD           paletteNumEntriesRead;  // doesn't always match the number of entries reported in the palette header
    const char *    sshPath;

    // tga-related buffers
    FILE *          tga_fp;

    BYTE *          tgaImgBuf;
    BYTE *          tgaExtraBuf; // used for either RLE-encoded data, or paletted images converted to truecolor

}sshHandle_t;

#endif // TYPES_H
