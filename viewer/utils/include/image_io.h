
#ifndef VIVE_IMAGE_IO_H
#define VIVE_IMAGE_IO_H

#include "vive_utils.h"

namespace vive {

//
// read/write image u8, channel= 1(Grayscale), 3(RGB), or 4(RGBA)
// be sure to free after used or LEAK!
uint8_t* read_image(char const* filename, int& width, int& height, int& channels);
bool write_image(char const* filename, void const* pixels, int width, int height, int channels);

}

#endif
