#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <sched.h>

#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include "jpeglib.h"
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>

#include <X11/Xatom.h>

#include <iostream>
#include <fstream>
#include <chrono>
#include <string>

#include "movie.h"
#include <vector>

using namespace std;

/*
apt install x264 libx264-dev libavutil55 libavutil-dev libavutil-ffmpeg54
libavformat57 libavformat-dev libavformat-ffmpeg56
librsvg2-dev libx264-dev libgtk-3-dev

ffprobe -show_streams -i "file.mp4"
*/
void write_JPEG_file (JSAMPLE * image_buffer, int image_height, int image_width, char * filename, int quality);
void yuyvToRgb(uint8_t *in,uint8_t *out, int size_x,int size_y);

class Data {
public:
    Data() {
        w = 640;
        h = 400;
        linesize = 1748;
        raw_frame_size = h*1748*2;
        frame = new uint8_t[raw_frame_size];
        l = new uint8_t[w*h*3];
        r = new uint8_t[w*h*3];
        rgb_left = new uint8_t[w*h*3];
        rgb_right = new uint8_t[w*h*3];
        movie_l = new Movie("/tmp/1_l.mp4", w, h);
        movie_r = new Movie("/tmp/1_r.mp4", w, h);
    }
    ~Data() { delete movie_l; delete movie_r; }
    void read(char *fn){
        printf("read file %s.\n", fn);
        auto myfile = std::ifstream(fn, std::ios::in | std::ios::binary);
        for (int i=0; i<100; i++) {
            myfile.read((char*)&frame[0], raw_frame_size);
            for (int k=0; k<h; k++) {
                memcpy(l+w*2*k, &frame[k*linesize*2 + 32+64], w*2);
                memcpy(r+w*2*k, &frame[linesize*2*k +32+64+w*2], w*2);
            }
            //save(l, r, i);
            yuyvToRgb(r, rgb_right, w, h);
            yuyvToRgb(l, rgb_left, w, h);
            movie_l->addFrame(rgb_left);
            movie_r->addFrame(rgb_right);
        }
        myfile.close();

    }
    void save(uint8_t *left, uint8_t *right, int seq) {
        char fn[255];
        yuyvToRgb(right, rgb_right, w, h);
        yuyvToRgb(left, rgb_left, w, h);

        snprintf(fn, 255, "/tmp/%d_l.jpg", seq);
        write_JPEG_file (rgb_left, h, w, fn, 100);
        snprintf(fn, 255, "/tmp/%d_r.jpg", seq);
        write_JPEG_file (rgb_right, h, w, fn, 100);
    }
    int w, h, linesize, raw_frame_size;
    uint8_t *frame, *l, *r, *rgb_right, *rgb_left;
    Movie *movie_l, *movie_r;
};

void savemp4();
int main (int argc, char *argv[])
{

    //if (argc >1) draw = std::stoi(argv[1], nullptr, 0);
    Data data = Data();
    data.read(argv[1]);
    //savemp4();
    return(0);
}

void savemp4(){
	const unsigned int width = 1024;
	const unsigned int height = 768;
	const unsigned int nframes = 128;

	Movie movie("random_pixels", width, height);
	
	vector<uint8_t> pixels(4 * width * height);
	for (unsigned int iframe = 0; iframe < nframes; iframe++)
	{
		for (unsigned int j = 0; j < height; j++)
			for (unsigned int i = 0; i < width; i++)
			{
				pixels[4 * width * j + 4 * i + 0] = rand() % 256;
				pixels[4 * width * j + 4 * i + 1] = rand() % 256;
				pixels[4 * width * j + 4 * i + 2] = rand() % 256;
				pixels[4 * width * j + 4 * i + 3] = rand() % 256;
			}	
	
		movie.addFrame(&pixels[0]);
	}
}

void write_JPEG_file (JSAMPLE * image_buffer, int image_height, int image_width, char * filename, int quality){
  /* This struct contains the JPEG compression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   * It is possible to have several such structures, representing multiple
   * compression/decompression processes, in existence at once.  We refer
   * to any one struct (and its associated working data) as a "JPEG object".
   */
  struct jpeg_compress_struct cinfo;
  /* This struct represents a JPEG error handler.  It is declared separately
   * because applications often want to supply a specialized error handler
   * (see the second half of this file for an example).  But here we just
   * take the easy way out and use the standard error handler, which will
   * print a message on stderr and call exit() if compression fails.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  struct jpeg_error_mgr jerr;
  /* More stuff */
  FILE * outfile;		/* target file */
  JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
  int row_stride;		/* physical row width in image buffer */

  /* Step 1: allocate and initialize JPEG compression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error(&jerr);
  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress(&cinfo);

  /* Step 2: specify data destination (eg, a file) */
  /* Note: steps 2 and 3 can be done in either order. */

  /* Here we use the library-supplied code to send compressed data to a
   * stdio stream.  You can also write your own code to do something else.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to write binary files.
   */
  if ((outfile = fopen(filename, "wb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    exit(1);
  }
  jpeg_stdio_dest(&cinfo, outfile);

  /* Step 3: set parameters for compression */

  /* First we supply a description of the input image.
   * Four fields of the cinfo struct must be filled in:
   */
  cinfo.image_width = image_width; 	/* image width and height, in pixels */
  cinfo.image_height = image_height;
  cinfo.input_components = 3;		/* # of color components per pixel */
  cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults(&cinfo);
  /* Now you can set any non-default parameters you wish to.
   * Here we just illustrate the use of quality (quantization table) scaling:
   */
  jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress(&cinfo, TRUE);

  /* Step 5: while (scan lines remain to be written) */
  /*           jpeg_write_scanlines(...); */

  /* Here we use the library's state variable cinfo.next_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   * To keep things simple, we pass one scanline per call; you can pass
   * more if you wish, though.
   */
  row_stride = image_width * 3;	/* JSAMPLEs per row in image_buffer */

  while (cinfo.next_scanline < cinfo.image_height) {
    /* jpeg_write_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could pass
     * more than one scanline at a time if that's more convenient.
     */
    row_pointer[0] = & image_buffer[cinfo.next_scanline * row_stride];
    (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  /* Step 6: Finish compression */

  jpeg_finish_compress(&cinfo);
  /* After finish_compress, we can close the output file. */
  fclose(outfile);

  /* Step 7: release JPEG compression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress(&cinfo);

  /* And we're done! */
}

//from https://social.msdn.microsoft.com/forums/windowsdesktop/en-us/1071301e-74a2-4de4-be72-81c34604cde9/program-to-translate-yuyv-to-rgbrgb modified yuyv order
/*--------------------------------------------------------*\
 |    yuv2rgb                                               |
 \*--------------------------------------------------------*/
void inline yuv2rgb(int y, int u, int v, char *r, char *g, char *b)
{
    int r1, g1, b1;
    int c = y-16, d = u - 128, e = v - 128;

    r1 = (298 * c           + 409 * e + 128) >> 8;
    g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
    b1 = (298 * c + 516 * d           + 128) >> 8;

    // Even with proper conversion, some values still need clipping.

    if (r1 > 255) r1 = 255;
    if (g1 > 255) g1 = 255;
    if (b1 > 255) b1 = 255;
    if (r1 < 0) r1 = 0;
    if (g1 < 0) g1 = 0;
    if (b1 < 0) b1 = 0;

    *r = r1 ;
    *g = g1 ;
    *b = b1 ;
}

/*--------------------------------------------------------*\
 |    yuyvToRgb                                             |
 \*--------------------------------------------------------*/
void yuyvToRgb(uint8_t *in,uint8_t *out, int size_x,int size_y)
{
    int i;
    unsigned int *pixel_16=(unsigned int*)in;;     // for YUYV
    unsigned char *pixel_24=out;    // for RGB
    int y, u, v, y2;
    char r, g, b;


    for (i=0; i< (size_x*size_y/2) ; i++)
    {
        // read YuYv from newBuffer (2 pixels) and build RGBRGB in pBuffer

     //   v  = ((*pixel_16 & 0x000000ff));
       // y  = ((*pixel_16 & 0x0000ff00)>>8);
       // u  = ((*pixel_16 & 0x00ff0000)>>16);
       // y2 = ((*pixel_16 & 0xff000000)>>24);

        y2  = ((*pixel_16 & 0x000000ff));
        u  = ((*pixel_16 & 0x0000ff00)>>8);
        y  = ((*pixel_16 & 0x00ff0000)>>16);
        v = ((*pixel_16 & 0xff000000)>>24);

     yuv2rgb(y, u, v, &r, &g, &b);            // 1st pixel


        *pixel_24++ = r;
        *pixel_24++ = g;
        *pixel_24++ = b;



    yuv2rgb(y2, u, v, &r, &g, &b);            // 2nd pixel

        *pixel_24++ = r;
        *pixel_24++ = g;
        *pixel_24++ = b;



        pixel_16++;
    }
}

