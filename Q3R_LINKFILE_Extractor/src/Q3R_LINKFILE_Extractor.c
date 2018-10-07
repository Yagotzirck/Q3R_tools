#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "makedir.h"

typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;

#define MAGICID 0x4C4E4B46

typedef struct linkFileHdr_s{
	DWORD magic;	//0x4C4E4B46, or FKNL
	DWORD filler;	// 0
}linkFileHdr_t;

typedef struct archiveDescriptor_s{
	DWORD dataBlockOffset;
	DWORD unk;
	DWORD fileNamesBlockOffset;
	DWORD rootDirDescrOffset;
}archiveDescriptor_t;

typedef struct dirDescriptor_s{
	DWORD fileDescrOffset;
	DWORD subDirDescrOffset;
	DWORD fileDescrCount;
	DWORD subDirDescrCount;
}dirDescriptor_t;

typedef struct fileDescriptor_s{
	DWORD fileNameOffset;
	DWORD dataOffset;
	DWORD dataSize;
	DWORD uncomprDataSize;
}fileDescriptor_t;

typedef struct subDirDescriptor_s{
	DWORD subDirNameOffset;
	DWORD subDirDescrOffset;
}subDirDescriptor_t;


/* local functions declarations */

static bool is_linkFile(FILE *in_fp);
static void init_path(const char *Path);
static void extractCurrDir(dirDescriptor_t *dirDescriptor, char *currDirPtr);
static size_t refpack_decompress_unsafe(const BYTE *indata, size_t *bytes_read_out,	BYTE *outdata);

/* global data (accessible only by this module) */
static char path[FILENAME_MAX];
static char *baseDirPtr;
static BYTE *linkfile_data;

int main(int argc, char **argv){
    size_t fileSize;
    FILE *in_fp;

    linkFileHdr_t           *linkFileHdr;
    archiveDescriptor_t     *archiveDescriptor;
    dirDescriptor_t         *rootDirDescriptor;


    puts("\t\tQuake 3 Revolution LINKFILE extractor by Yagotzirck");

    if(argc != 2){
        fputs("Usage: Q3R_LINKFILE_Extractor.exe <LINKFILE.LNK>\n", stderr);
        return 1;
    }

    // open the data file
    if((in_fp = fopen(argv[1], "rb")) == NULL){
        fprintf(stderr, "Couldn't open %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    // check if the file passed as an argument is LINKFILE.LNK
    if(!is_linkFile(in_fp)){
        fprintf(stderr, "%s doesn't appear to be Q3R's LINKFILE archive.\n", argv[1]);
        return 1;
    }

    // get the file size
    fseek(in_fp, 0, SEEK_END);
    fileSize = ftell(in_fp);
    rewind(in_fp);

    // load the data file into memory and close it
    if((linkfile_data = malloc(fileSize)) == NULL){
        fprintf(stderr, "Couldn't allocate %u bytes for loading the file into memory\n", fileSize);
        return 1;
    }

    fread(linkfile_data, 1, fileSize, in_fp);
    fclose(in_fp);


    // create main directory
    init_path(argv[1]);

    /* acquire linkFile's header:
    ** this step is superflous since the magic ID signature check has already been handled by
    ** calling is_linkFile() earlier, but I'm leaving it anyway since I think it makes the
    ** program flow clearer
    */
    linkFileHdr = linkfile_data;

    /* get the root directory descriptor's offset and pass it to extractCurrDir();
    ** everything else will be handled in there, since it's a recursive function
    */
    archiveDescriptor  = linkfile_data + sizeof(linkFileHdr_t);
    rootDirDescriptor  = linkfile_data + archiveDescriptor->rootDirDescrOffset;

    puts("Extracting the archive...");
    extractCurrDir(rootDirDescriptor, baseDirPtr);

    free(linkfile_data);

    puts("The archive has been successfully extracted.");

    return 0;
}

/* local functions definitions */
static bool is_linkFile(FILE *in_fp){
    linkFileHdr_t linkFileHdr;
    size_t elementsRead;

    elementsRead = fread(&linkFileHdr, sizeof(linkFileHdr), 1, in_fp);
    rewind(in_fp);

    return  elementsRead == 1 &&
            linkFileHdr.magic == MAGICID &&
            linkFileHdr.filler == 0;
}

static void init_path(const char *linkfilePath){
    strcpy(path, linkfilePath);

    /* Create a directory named "LINKFILE_extracted" on same directory as the data file */

    baseDirPtr = path + strlen(path);

    while(*baseDirPtr != '\\' && *baseDirPtr != '/' && baseDirPtr-- != path)
        ;
    ++baseDirPtr;

    strcpy(baseDirPtr, "LINKFILE_extracted/");
    makeDir(path);

    /* make baseDirPtr point to the end of "LINKFILE_extracted/" string */
    baseDirPtr += strlen(baseDirPtr);
}


static void extractCurrDir(dirDescriptor_t *dirDescriptor, char *currDirPtr){
    fileDescriptor_t *fileDescriptor  = linkfile_data + dirDescriptor->fileDescrOffset;
    subDirDescriptor_t *subDirDescriptor = linkfile_data + dirDescriptor->subDirDescrOffset;

    unsigned i;
    FILE *out_fp;

    // extract files
    for(i = 0; i < dirDescriptor->fileDescrCount; ++i){
        strcpy(currDirPtr, linkfile_data + fileDescriptor[i].fileNameOffset);

        if((out_fp = fopen(path, "wb")) == NULL){
            fprintf(stderr, "Couldn't create %s: %s\n", path, strerror(errno));
            exit(EXIT_FAILURE);
        }

        // entry is uncompressed
        if(fileDescriptor[i].uncomprDataSize == fileDescriptor[i].dataSize)
            fwrite(linkfile_data + fileDescriptor[i].dataOffset, 1, fileDescriptor[i].dataSize, out_fp);
        // entry is compressed with RefPack
        else{
            BYTE *outData;
            size_t uncompr_size;
            size_t bytes_read_out;

            if((outData = malloc(fileDescriptor[i].uncomprDataSize)) == NULL){
                fprintf(stderr, "Couldn't allocate %u bytes to decompress entry %s\n", fileDescriptor[i].uncomprDataSize, path);
                exit(EXIT_FAILURE);
            }

            uncompr_size = refpack_decompress_unsafe(linkfile_data + fileDescriptor[i].dataOffset, &bytes_read_out, outData);

            if(bytes_read_out != fileDescriptor[i].dataSize)
                fprintf(
                    stderr,
                    "\nWARNING: # of processed bytes mismatch for %s\n"
                        "\tCompressed size reported in header:\t\t"             "0x%08X\n"
                        "\tActual # of compressed bytes processed:\t\t"         "0x%08X\n"
                        "\tUncompressed size reported in header:\t\t"           "0x%08X\n"
                        "\tUncompressed size reported in RefPack's header:\t"   "0x%08X\n"
                    "Saving it anyway (using size reported in RefPack's header)...\n\n",
                    baseDirPtr, fileDescriptor[i].dataSize, bytes_read_out, fileDescriptor[i].uncomprDataSize, uncompr_size
                );

            fwrite(outData, 1, uncompr_size, out_fp);
            free(outData);
        }

        fclose(out_fp);
    }

    // recursively explore subdirectories
    for(i = 0; i < dirDescriptor->subDirDescrCount; ++i){
        int subDirNameLen = sprintf(currDirPtr, "%s/", linkfile_data + subDirDescriptor[i].subDirNameOffset);
        makeDir(path);
        extractCurrDir(linkfile_data + subDirDescriptor[i].subDirDescrOffset, currDirPtr + subDirNameLen);
    }
}

/* RefPack decompress function; I take no credit for it, since I copy-pasted it from here:
** http://wiki.niotso.org/RefPack
*/

/**
 * @brief Decompress a RefPack bitstream
 * @param indata - (optional) Pointer to the input RefPack bitstream; may be
 *	NULL
 * @param bytes_read_out - (optional) Pointer to a size_t which will be filled
 *	with the total number of bytes read from the RefPack bitstream; may be
 *	NULL
 * @param outdata - Pointer to the output buffer which will be filled with the
 *	decompressed data; outdata may be NULL only if indata is also NULL
 * @return The value of the "decompressed size" field in the RefPack bitstream,
 *	or 0 if indata is NULL
 *
 * This function is a verbatim translation from x86 assembly into C (with
 * new names and comments supplied) of the RefPack decompression function
 * located at TSOServiceClientD_base+0x724fd in The Sims Online New & Improved
 * Trial.
 *
 * This function ***does not*** perform any bounds-checking on reading or
 * writing. It is inappropriate to use this function on untrusted data obtained
 * from the internet (even though that is exactly what The Sims Online does...).
 * Here are the potential problems:
 * - This function will read past the end of indata if the last command in
 *   indata tells it to.
 * - This function will write past the end of outdata if indata tells it to.
 * - This function will read before the beginning of outdata if indata tells
 *   it to.
 */
size_t refpack_decompress_unsafe(const BYTE *indata, size_t *bytes_read_out,
	BYTE *outdata)
{
	const BYTE *in_ptr;
	BYTE *out_ptr;
	WORD signature;
	DWORD decompressed_size = 0;
	BYTE byte_0, byte_1, byte_2, byte_3;
	DWORD proc_len, ref_len;
	BYTE *ref_ptr;
	DWORD i;

	in_ptr = indata, out_ptr = outdata;
	if (!in_ptr)
		goto done;

	signature = ((in_ptr[0] << 8) | in_ptr[1]), in_ptr += 2;
	if (signature & 0x0100)
		in_ptr += 3; /* skip over the compressed size field */

	decompressed_size = ((in_ptr[0] << 16) | (in_ptr[1] << 8) | in_ptr[2]);
	in_ptr += 3;

	while (1) {
		byte_0 = *in_ptr++;
		if (!(byte_0 & 0x80)) {
			/* 2-byte command: 0DDRRRPP DDDDDDDD */
			byte_1 = *in_ptr++;

			proc_len = byte_0 & 0x03;
			for (i = 0; i < proc_len; i++)
				*out_ptr++ = *in_ptr++;

			ref_ptr = out_ptr - ((byte_0 & 0x60) << 3) - byte_1 - 1;
			ref_len = ((byte_0 >> 2) & 0x07) + 3;
			for (i = 0; i < ref_len; i++)
				*out_ptr++ = *ref_ptr++;
		} else if(!(byte_0 & 0x40)) {
			/* 3-byte command: 10RRRRRR PPDDDDDD DDDDDDDD */
			byte_1 = *in_ptr++;
			byte_2 = *in_ptr++;

			proc_len = byte_1 >> 6;
			for (i = 0; i < proc_len; i++)
				*out_ptr++ = *in_ptr++;

			ref_ptr = out_ptr - ((byte_1 & 0x3f) << 8) - byte_2 - 1;
			ref_len = (byte_0 & 0x3f) + 4;
			for (i = 0; i < ref_len; i++)
				*out_ptr++ = *ref_ptr++;
		} else if(!(byte_0 & 0x20)) {
			/* 4-byte command: 110DRRPP DDDDDDDD DDDDDDDD RRRRRRRR*/
			byte_1 = *in_ptr++;
			byte_2 = *in_ptr++;
			byte_3 = *in_ptr++;

			proc_len = byte_0 & 0x03;
			for (i = 0; i < proc_len; i++)
				*out_ptr++ = *in_ptr++;

			ref_ptr = out_ptr - ((byte_0 & 0x10) << 12)
				- (byte_1 << 8) - byte_2 - 1;
			ref_len = ((byte_0 & 0x0c) << 6) + byte_3 + 5;
			for (i = 0; i < ref_len; i++)
				*out_ptr++ = *ref_ptr++;
		} else {
			/* 1-byte command: 111PPPPP */
			proc_len = (byte_0 & 0x1f) * 4 + 4;
			if (proc_len <= 0x70) {
				/* no stop flag */
				for (i = 0; i < proc_len; i++)
					*out_ptr++ = *in_ptr++;
			} else {
				/* stop flag */
				proc_len = byte_0 & 0x3;
				for (i = 0; i < proc_len; i++)
					*out_ptr++ = *in_ptr++;

				break;
			}
		}
	}

done:
	if (bytes_read_out)
		*bytes_read_out = in_ptr - indata;
	return decompressed_size;
}
