# Q3R_tools
A set of tools to extract/convert resources from the PlayStation 2 game Quake 3 Revolution (2001).


## Overview
First of all, these tools extract/convert resources by saving them on the same path as the input file, so you have to move them in a path/folder where you have write permissions before passing them to the tools (e.g. you can't pass the files directly from the game's CD.)</br>

The following tools are included:

#### Q3R_LINKFILE_Extractor
Extracts files and folders contained into the archive file LINKFILE.LNK in a folder named LINKFILE_extracted.</br>
This contains several goodies, but the only files I've properly examined are the .ssh image files.

#### Q3R_SDT_Extractor
Extracts sound files (.mp2 and .vag files) from the .SDT archive files contained in the SOUND, SOUND_FR and SOUND_IT folders located in the game CD's root directory.

#### Q3R_ssh2tga
As the name implies, converts the .ssh image files extracted from the LINKFILE.LNK archive file into .tga images.

## Usage
Invoke each tool from a command line prompt without any arguments to see basic usage instructions; this is advised especially for Q3R_ssh2tga, since you can give it an option to change tga's output format.

On Windows the drag-and-drop method works as well, but in Q3R_ssh2tga's case you won't be able to control the output format for the tga files.

## Credits
- torridgristle: for discovering that the compressed entries inside the LINKFILE.LNK archive file use REFPACK compression format, and explaining me how to fix the swizzled palettes for 8bpp .ssh files;

- Mr. Mouse: for sharing his discoveries about .ssh file format (albeit for a different game and with slight variations), which made me life easier when reversing the format used in Q3R.
