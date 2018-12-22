#include "gl_LTexture.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "foundation/krr_math.h"
#include "SDL_log.h"
#include "SDL_image.h"

struct DDS_PixelFormat
{
  int size;
  int flags;
  int fourcc;
  int rgb_bitcount;
  int r_bitmask;
  int g_bitmask;
  int b_bitmask;
  int a_bitmask;
};

struct DDS_Header
{
  int size;
  int flags;
  int height;
  int width;
  int pitch_or_linear_size;
  int depth;
  int mipmap_count;
  int reserved1[11];
  struct DDS_PixelFormat ddspf;
  int caps;
  int caps2;
  int caps3;
  int caps4;
  int reserved2;
};

/// print header struct info
static void _print_dds_header_struct(const struct DDS_Header* header)
{
  SDL_Log("DDS Header Dump");
  SDL_Log("- size: %d", header->size);
  SDL_Log("- flags: 0x%X", header->flags);
  SDL_Log("- height: %d", header->height);
  SDL_Log("- width: %d", header->width);
  SDL_Log("- pitch or linear size: %d", header->pitch_or_linear_size);
  SDL_Log("- depth: %d", header->depth);
  SDL_Log("- mipmap count: %d", header->mipmap_count);
  SDL_Log("- caps: 0x%X", header->caps);
  SDL_Log("- caps2: 0x%X", header->caps2);
  SDL_Log("- ddspf");
  SDL_Log("\t- size: %d", header->ddspf.size);
  SDL_Log("\t- flags: 0x%X", header->ddspf.flags);
  char fourcc_chrs[5];
  memset(fourcc_chrs, 0, sizeof(fourcc_chrs));
  strncpy(fourcc_chrs, (char*)&header->ddspf.fourcc, 4);
  SDL_Log("\t- fourCC: %s [0x%X]", fourcc_chrs, header->ddspf.fourcc);
  SDL_Log("\t- RGB bitcount: %d", header->ddspf.rgb_bitcount);
  SDL_Log("\t - R bitmask: 0x%X", header->ddspf.r_bitmask);
  SDL_Log("\t - G bitmask: 0x%X", header->ddspf.g_bitmask);
  SDL_Log("\t - B bitmask: 0x%X", header->ddspf.b_bitmask);
  SDL_Log("\t - A bitmask: 0x%X", header->ddspf.a_bitmask);
}

static void init_defaults(gl_LTexture* texture)
{
  texture->texture_id = 0;
  texture->width = 0;
  texture->height = 0;
}

static void free_internal_texture(gl_LTexture* texture)
{
  if (texture != NULL & texture->texture_id != 0)
  {
    glDeleteTextures(1, &texture->texture_id);
    texture->texture_id = 0;
  }

  texture->width = 0;
  texture->height = 0;
}

gl_LTexture* gl_LTexture_new()
{
  gl_LTexture* out = malloc(sizeof(gl_LTexture));
  init_defaults(out);
  return out;
}

void gl_LTexture_free(gl_LTexture* texture)
{
  if (texture != NULL)
  {
    // free internal texture
    free_internal_texture(texture);
    // free allocated memory
    free(texture);
    texture = NULL;
  }
}

bool gl_LTexture_load_texture_from_file(gl_LTexture* texture, const char* path)
{
  SDL_Surface* loaded_surface = IMG_Load(path);
  if (loaded_surface == NULL)
  {
    SDL_Log("Unable to load image %s! SDL_Image error: %s", path, IMG_GetError());
    return false;
  }

  // set pixel data to texture
  if (!gl_LTexture_load_texture_from_pixels32(texture, loaded_surface->pixels, loaded_surface->w, loaded_surface->h))
  {
    SDL_Log("Failed to set pixel data to texture");
    return false;
  }

  // free surface
  SDL_FreeSurface(loaded_surface);
  loaded_surface = NULL;

  return true;
}

bool gl_LTexture_load_dds_texture_from_file(gl_LTexture* texture, const char* path)
{
  // pre-check if user's system doesn't have required version of opengl support
  if (GLEW_EXT_texture_compression_s3tc == 0)
  {
    SDL_Log("S3TC texture not support for this system. Quit now");
    return false;
  }

  FILE *fp = NULL;
  fp = fopen(path, "r");
  if (fp == NULL)
  {
    SDL_Log("Unable to open file %s for read", path);
    return false;
  }

  // check whether it's DDS file
  int magic_number = 0;
  int f_obj_number = 0;
  f_obj_number = fread(&magic_number, 4, 1, fp);
  if (f_obj_number != 1)
  {
    SDL_Log("Error reading file %s", path);
    fclose(fp);
    return false;
  }

  if (magic_number != 0x20534444)
  {
    SDL_Log("Input file %s is not DDS texture", path);
    return false;
  }

  // read header
  struct DDS_Header header;
  memset(&header, 0, sizeof(header));
  f_obj_number = fread(&header, sizeof(header), 1, fp);
  if (f_obj_number != 1)
  {
    SDL_Log("Error reading header struct");
    fclose(fp);
    return false;
  }

  // print struct info of header
  _print_dds_header_struct(&header);

  // check whether it's non-power-of-two texture or not
  // if so, quit
  if ((header.width & (header.width - 1)) != 0 ||
      (header.height & (header.height - 1)) != 0)
  {
    SDL_Log("Input texture is not power of two");
    return false;
  }

  // determine the blocksize from format of the texture
  int blocksize = 0;
  GLuint gl_format;
  
  // DXT1
  if (header.ddspf.fourcc == 0x31545844)
  {
    SDL_Log("it's DXT1 format");
    gl_format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    blocksize = 8;
  }
  // DXT2 (ignored)
  else if (header.ddspf.fourcc == 0x32545844)
  {
    SDL_Log("it's DXT2 format");
  }
  // DXT3
  else if (header.ddspf.fourcc == 0x33545844)
  {
    SDL_Log("it's DXT3 format");
    gl_format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    blocksize = 16;
  }
  // DXT4 (ignored)
  else if (header.ddspf.fourcc == 0x34545844)
  {
    SDL_Log("it's DXT4 format");
  }
  // DXT5
  else if (header.ddspf.fourcc == 0x35545844)
  {
    SDL_Log("it's DXT5 format");
    gl_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    blocksize = 16;
  }

  // compute the size of image
  // total image size = base image size + all mipmap images' size
  int total_image_size = ceil(header.width / 4) * ceil(header.height / 4) * blocksize;
  SDL_Log("base image size: %d", total_image_size);

  SDL_Log("level 0, width: %d, height: %d, size: %d", header.width, header.height, total_image_size);

  {
    int width = header.width;
    int height = header.height;
    int prev_width = width;
    int prev_height = height;

    // calculate image size for each level of mipmap image, then add it to total image size
    for (int level=1; level<=header.mipmap_count; level++)
    {
      width = krr_math_max(1, width/2);
      height = krr_math_max(1, height/2);

      // check against previous width and height
      // we not going to proceed if it's the same
      if (width == prev_width && height == prev_height)
      {
        break;
      }
      else
      {
        // update previous width and height
        prev_width = width;
        prev_height = height;
      }

      // calculate the size of each mipmap texture
      int size = ceil(width/4.0) * ceil(height/4.0) * blocksize;

      SDL_Log("level %d, width: %d, height: %d, size: %d", level, width, height, size);

      // add back to total image size
      total_image_size += size;
    }

    SDL_Log("total image size (included mipmap images): %d", total_image_size);
  }

  // use pitch to read base image from dds file
  unsigned char images_buffer[total_image_size];
  memset(images_buffer, 0, total_image_size);

  f_obj_number = fread(images_buffer, total_image_size, 1, fp);
  if (f_obj_number != 1)
  {
    SDL_Log("Error reading image data (included mipmap images)");
    fclose(fp);
    return false;
  }

  // generate texture id
  GLuint texture_id = 0;
  glGenTextures(1, &texture_id);

  // bind texture
  glBindTexture(GL_TEXTURE_2D, texture_id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // (important) set maximum level of mipmap to use for this texture
  // if there's no mipmap data inside the texture, we need to set the maximum number of level
  // to zero; otherwise it won't render anything!
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, header.mipmap_count == 0 ? 0 : header.mipmap_count - 1);

  {
    int width = header.width;
    int height = header.height;
    int offset = 0;

    // generate compressed texture
    for (int level=0; level<header.mipmap_count; level++)
    {
      // calculate the size of each mipmap image
      int size = ceil(width/4.0) * ceil(height/4.0) * blocksize;

      glCompressedTexImage2D(GL_TEXTURE_2D, level, gl_format, width, height, 0, size, images_buffer + offset);

      SDL_Log("level %d, width: %d, height: %d, size: %d", level, width, height, size);

      // proceed forward with next mipmap's dimension
      width = krr_math_max(1, width/2);
      height = krr_math_max(1, height/2);

      // update offset
      offset += size;
    }
  }

  // unbind texture
  glBindTexture(GL_TEXTURE_2D, 0);

  // set information back to LTexture
  texture->texture_id = texture_id;
  texture->width = header.width;
  texture->height = header.height;

  fclose(fp);
  fp = NULL;
  return true;
}

bool gl_LTexture_load_texture_from_pixels32(gl_LTexture* texture, GLuint* pixels, GLuint width, GLuint height)
{
  // free existing texture first if it exists
  // because user can load texture from pixels data multiple times
  free_internal_texture(texture);

  // get texture dimensions
  texture->width = width;
  texture->height = height;

  // generate texture id
  glGenTextures(1, &texture->texture_id);

  // bind texture id
  glBindTexture(GL_TEXTURE_2D, texture->texture_id);

  // generate texture
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

  // set texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  // unbind texture
  glBindTexture(GL_TEXTURE_2D, 0);

  // check for errors
  GLenum error = glGetError();
  if (error != GL_NO_ERROR)
  {
    SDL_Log("Error loading texture from %p pixels! %s", pixels, gluErrorString(error));
    return false;
  }

  return true;
}

void gl_LTexture_render(gl_LTexture* texture, GLfloat x, GLfloat y, LFRect* clip)
{
  // texture coordinates
  GLfloat tex_top = 0.f;
  GLfloat tex_bottom = 1.f;
  GLfloat tex_left = 0.f;
  GLfloat tex_right = 1.f;

  // vertex coordinates
  GLfloat quad_width = texture->width;
  GLfloat quad_height = texture->height;

  // handle clipping
  if (clip != NULL)
  {
    // modify texture coordinates
    tex_left = clip->x / texture->width;
    tex_right = (clip->x + clip->w) / texture->width;
    tex_top = clip->y / texture->height;
    tex_bottom = (clip->y + clip->h) / texture->height;

    // modify vertex coordinates
    quad_width = clip->w;
    quad_height = clip->h;
  }

  // not to mess with matrix from outside
  // but use its matrix to operate on this further
  glPushMatrix();

  // move to rendering position
  glTranslatef(x, y, 0.f);

  // set texture id
  glBindTexture(GL_TEXTURE_2D, texture->texture_id);

  // render texture quad
  glBegin(GL_QUADS);
    glTexCoord2f(tex_left, tex_top); glVertex2f(0.f, 0.f);
    glTexCoord2f(tex_right, tex_top); glVertex2f(quad_width, 0.f);
    glTexCoord2f(tex_right, tex_bottom); glVertex2f(quad_width, quad_height);
    glTexCoord2f(tex_left, tex_bottom); glVertex2f(0.f, quad_height);
  glEnd();

  // back to original matrix
  glPopMatrix();
}
