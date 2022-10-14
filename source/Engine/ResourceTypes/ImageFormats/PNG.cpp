#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/ResourceTypes/ImageFormats/ImageFormat.h>
#include <Engine/IO/Stream.h>

class PNG : public ImageFormat {
public:
    Uint8 NumPaletteColors;
};
#endif

#include <Engine/ResourceTypes/ImageFormats/PNG.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Clock.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/IO/FileStream.h>
#include <Engine/IO/MemoryStream.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Rendering/Software/SoftwareRenderer.h>

#ifdef USING_LIBPNG
#include "png.h"
#ifndef PNG_READ_SUPPORTED
#undef USING_LIBPNG
#endif
#endif

#ifdef USING_LIBPNG
void png_read_fn(png_structp ctx, png_bytep area, png_size_t size) {
    Stream* stream = (Stream*)png_get_io_ptr(ctx);
    stream->ReadBytes(area, size);
}
#else
#define STB_IMAGE_IMPLEMENTATION
#include <Libraries/stb_image.h>
#endif

PUBLIC STATIC  PNG*   PNG::Load(const char* filename) {
#ifdef USING_LIBPNG
    PNG* png = new PNG;
    Stream* stream = NULL;

    // PNG variables
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    Uint32 width, height;
    int bit_depth, color_type, palette_size;
    bool usePalette = false;
    png_colorp palette;
    Uint32* pixelData = NULL;
    png_bytep* row_pointers = NULL;
    Uint32 row_bytes;

    if (strncmp(filename, "file://", 7) == 0)
        stream = FileStream::New(filename + 7, FileStream::READ_ACCESS);
    else
        stream = ResourceStream::New(filename);
    if (!stream) {
        Log::Print(Log::LOG_ERROR, "Could not open file '%s'!", filename);
        goto PNG_Load_FAIL;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL){
        Log::Print(Log::LOG_ERROR, "Couldn't create PNG read struct!");
        goto PNG_Load_FAIL;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        Log::Print(Log::LOG_ERROR, "Couldn't create PNG information struct!");
        goto PNG_Load_FAIL;
    }

    if (setjmp(*png_set_longjmp_fn(png_ptr, longjmp, sizeof(jmp_buf)))) {
        Log::Print(Log::LOG_ERROR, "Couldn't read PNG file!");
        goto PNG_Load_FAIL;
    }

    png_set_read_fn(png_ptr, stream, png_read_fn);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    else if (color_type == PNG_COLOR_TYPE_PALETTE) {
        if (png_get_PLTE(png_ptr, info_ptr, &palette, &palette_size))
            usePalette = true;

        if (!usePalette)
            png_set_palette_to_rgb(png_ptr);
    }

    if (!usePalette && png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    else if (color_type != PNG_COLOR_TYPE_RGB_ALPHA && color_type != PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    png->Width = width;
    png->Height = height;
    png->Data = (Uint32*)Memory::TrackedMalloc("PNG::Data", width * height * sizeof(Uint32));

    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * png->Height);
    if (!row_pointers)
        goto PNG_Load_FAIL;

    pixelData = (Uint32*)malloc(width * height * sizeof(Uint32));
    row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    for (Uint32 row = 0, pitch = row_bytes; row < png->Height; row++)
        row_pointers[row] = (png_bytep)((Uint8*)pixelData + row * pitch);

    png_read_image(png_ptr, row_pointers);
    free(row_pointers);

    if (usePalette) {
        Uint8* data = (Uint8*)pixelData;
        if (bit_depth != 8)
            png->ReadPixelBitstream(data, bit_depth);
        else {
            for (size_t i = 0; i < width * height; data++, i++)
                png->Data[i] = *data;
        }

        png->Colors = (Uint32*)Memory::TrackedMalloc("PNG::Colors", palette_size * sizeof(Uint32));
        png->Paletted = true;
        png->NumPaletteColors = palette_size;

        for (size_t i = 0; i < png->NumPaletteColors; i++, palette++) {
            png->Colors[i]  = 0xFF000000U;
            png->Colors[i] |= palette->red << 16;
            png->Colors[i] |= palette->green << 8;
            png->Colors[i] |= palette->blue;
        }
    }
    else {
        int num_channels = PNG_COLOR_TYPE_RGB_ALPHA ? 4 : 3;
        png->ReadPixelDataARGB(pixelData, num_channels);
        png->Paletted = false;
        png->NumPaletteColors = 0;
    }

    free(pixelData);

    goto PNG_Load_Success;

    PNG_Load_FAIL:
        delete png;
        png = NULL;

    PNG_Load_Success:
    if (png_ptr)
        png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : (png_infopp)NULL, (png_infopp)NULL);
    if (stream)
        stream->Close();
    return png;
#else
    PNG* png = new PNG;
    Stream* stream = NULL;
    Uint32* pixelData = NULL;
    int num_channels;
    int width, height;
    Uint8* buffer = NULL;
    size_t buffer_len = 0;

    if (strncmp(filename, "file://", 7) == 0)
        stream = FileStream::New(filename + 7, FileStream::READ_ACCESS);
    else
        stream = ResourceStream::New(filename);
    if (!stream) {
        Log::Print(Log::LOG_ERROR, "Could not open file '%s'!", filename);
        goto PNG_Load_FAIL;
    }

    buffer_len = stream->Length();
    buffer = (Uint8*)malloc(buffer_len);
    stream->ReadBytes(buffer, buffer_len);
    stream->Close();

    pixelData = (Uint32*)stbi_load_from_memory(buffer, buffer_len, &width, &height, &num_channels, STBI_rgb_alpha);
    if (!pixelData) {
        Log::Print(Log::LOG_ERROR, "stbi_load failed: %s", stbi_failure_reason());
        goto PNG_Load_FAIL;
    }

    png->Width = width;
    png->Height = height;
    png->Data = (Uint32*)Memory::TrackedMalloc("PNG::Data", png->Width * png->Height * sizeof(Uint32));

    png->ReadPixelDataARGB(pixelData, num_channels);

    png->Colors = nullptr;
    png->Paletted = false;
    png->NumPaletteColors = 0;

    goto PNG_Load_Success;

PNG_Load_FAIL:
    delete png;
    png = NULL;

PNG_Load_Success:
    if (buffer)
        free(buffer);
    if (pixelData)
        stbi_image_free(pixelData);
    return png;
#endif
	return NULL;
}
PUBLIC         void   PNG::ReadPixelDataARGB(Uint32* pixelData, int num_channels) {
    Uint32 Rmask, Gmask, Bmask, Amask;
    bool doConvert = false;

    if (Graphics::PreferredPixelFormat == SDL_PIXELFORMAT_ABGR8888) {
        Amask = (num_channels == 4) ? 0xFF000000 : 0;
        Bmask = 0x00FF0000;
        Gmask = 0x0000FF00;
        Rmask = 0x000000FF;
        if (num_channels != 4)
            doConvert = true;
    }
    else if (Graphics::PreferredPixelFormat == SDL_PIXELFORMAT_ARGB8888) {
        Amask = (num_channels == 4) ? 0xFF000000 : 0;
        Rmask = 0x00FF0000;
        Gmask = 0x0000FF00;
        Bmask = 0x000000FF;
        doConvert = true;
    }

    if (doConvert) {
        Uint8* px;
        Uint32 a, b, g, r;
        int pc = 0, size = Width * Height;
        if (Amask) {
            for (Uint32* i = pixelData; pc < size; i++, pc++) {
                px = (Uint8*)i;
                r = px[0] | px[0] << 8 | px[0] << 16 | px[0] << 24;
                g = px[1] | px[1] << 8 | px[1] << 16 | px[1] << 24;
                b = px[2] | px[2] << 8 | px[2] << 16 | px[2] << 24;
                a = px[3] | px[3] << 8 | px[3] << 16 | px[3] << 24;
                Data[pc] = (r & Rmask) | (g & Gmask) | (b & Bmask) | (a & Amask);
            }
        }
        else {
            for (Uint8* i = (Uint8*)pixelData; pc < size; i += 3, pc++) {
                px = (Uint8*)i;
                r = px[0] | px[0] << 8 | px[0] << 16 | px[0] << 24;
                g = px[1] | px[1] << 8 | px[1] << 16 | px[1] << 24;
                b = px[2] | px[2] << 8 | px[2] << 16 | px[2] << 24;
                Data[pc] = (r & Rmask) | (g & Gmask) | (b & Bmask) | 0xFF000000;
            }
        }
    }
    else {
        memcpy(Data, pixelData, Width * Height * sizeof(Uint32));
    }
}
PUBLIC         void   PNG::ReadPixelBitstream(Uint8* pixelData, size_t bit_depth) {
    size_t scanline_width = (((bit_depth * Width) + 15) / 8) - 1;
    Uint8 mask = (1 << bit_depth) - 1;

    for (size_t y = 0; y < Height; y++) {
        Uint8* src = pixelData + (y * scanline_width);
        Uint32* out = Data + (y * Width);

        memset(out, 0, Width * sizeof(Uint32));

        for (size_t w = 0, r = 0; r < scanline_width;) {
            Uint8 pixels = src[r++];
            Uint8 buf[8];
            size_t i = 0;

            for (size_t b = 0; b < 8; b += bit_depth) {
                buf[i++] = pixels & mask;
                pixels >>= bit_depth;
            }
            --i;

            for (size_t b = 0; b < 8; b += bit_depth)
                out[w++] = buf[i--];
        }
    }
}

PUBLIC STATIC  bool   PNG::Save(PNG* png, const char* filename) {
    return png->Save(filename);
}

PUBLIC        bool    PNG::Save(const char* filename) {
    Stream* stream = FileStream::New(filename, FileStream::WRITE_ACCESS);
    if (!stream)
        return false;

    stream->Close();
    return true;
}

PUBLIC                PNG::~PNG() {

}
