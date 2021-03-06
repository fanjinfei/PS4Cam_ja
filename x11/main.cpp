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

#include "ps4eye.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <string>

int stick_this_thread_to_core(int core_id);
void set_realtime_priority();
int read_JPEG_file (char * filename);
void write_JPEG_file (JSAMPLE * image_buffer, int image_height, int image_width, char * filename, int quality);


// Threads
sem_t draw_sem;
pthread_t th, th_usb, th_draw; // Drawing thread
pthread_t *thFill=0; // Sub drawing threads
pthread_t thFifo; // Pipe control thread
pthread_t thPreload; // Preload image thread

// Drawing 
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER; // Mutex protecting the data array


unsigned char* read_jpeg(const char* sFile, int& iW, int& iH);
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
/*
void convert_opencv_to_RGB(uint8_t *in,uint8_t *out, int size_x,int size_y)
{
    cv::Mat yuv(size_y,size_x,CV_8UC2 ,in);

    cv::Mat rgb(size_y,size_x,CV_8UC3, out);
    //ARRRRRRGGGGGGHHHHHHHH openframework has an veryyyyyyyyy old opencv lib you wiil need opencv 2.4.9 to use 115 ->>> CV_YUV2RGB_YUY2 
    cv::cvtColor(yuv, rgb, 115);
}*/

void time_ms (time_t &s, long &ms)
{
//    long            ms; // Milliseconds
//    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }
    //printf("Current time: %"PRIdMAX".%03ld seconds since the Epoch\n", (intmax_t)s, ms);
}

long ofGetElapsedTimeMillis() {
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    time_ms(s, ms);
    return (s%3600)*1000 + ms;
}

class Data {
  public:
    void setup(int draw);
    void update();
    void record();
    void record2();
    void draw(Display *display, Window window, GC gc, int x, int y,int w, int h);
    void exit();
    ps4eye::PS4EYECam::PS4EYERef eye;
    uint8_t *frame_rgb_left;
    uint8_t *frame_rgb_right;
    unsigned char * videoFrame;

    int camFrameCount;
    int camFpsLastSampleFrame;
    long camFpsLastSampleTime;
    float camFps;
    
    XImage *img_l;
    XImage *img_r;
    int hasNewFrame;

    uint8_t *raw_frames[100]; //3 seconds data
    int raw_index, raw_frame_size, xdraw;

};

struct t_arg {
    Data *pdev;
    Display *disp;
    Window win;
    GC gc;
    int appFps;
    int run;
};

void Data::setup(int _draw) {
    using namespace ps4eye;

    camFrameCount = 0;
    camFpsLastSampleFrame = 0;
    camFpsLastSampleTime = 0;
    camFps = 0;
    hasNewFrame = 0;
    xdraw = _draw;

    // // list out the devices
    //  std::vector<PS3EYECam::PS3EYERef> devices( PS3EYECam::getDevices() );
    std::vector<PS4EYECam::PS4EYERef> devices( PS4EYECam::getDevices() );
    

    if(devices.size())
    {
        eye = devices.at(0);
        std::cerr << "Initializing..." << std::endl;
       bool res = eye->init(1, 30);
        std::cerr << "Starting..." << std::endl;
       eye->start();

        frame_rgb_left = new uint8_t[eye->getWidth()*eye->getHeight()*3];
        frame_rgb_right = new uint8_t[eye->getWidth()*eye->getHeight()*3];
        memset(frame_rgb_left, 0, eye->getWidth()*eye->getHeight()*3);
        memset(frame_rgb_right, 0, eye->getWidth()*eye->getHeight()*3);


       videoFrame 	= new unsigned char[eye->getWidth()*eye->getHeight()*3];

       raw_frame_size = eye->getHeight()*1748*2;
       for (int i=0; i<100; i++){
           raw_frames[i] = new uint8_t[raw_frame_size];
       }
       raw_index = 0;

        std::cerr << "Thread Starting... " << (raw_frame_size/eye->getHeight()) <<" "<< eye->getWidth() <<" " << eye->getHeight()  << std::endl;
    }
    else {
        int iw, ih;
        frame_rgb_left = read_jpeg("/tmp/a.jpg", iw, ih);
        frame_rgb_right = read_jpeg("/tmp/b.jpg", iw, ih);
        std::cerr << "Initializing from file..." << iw <<" " << ih<< std::endl;

    }
}

void Data::update() {
    eyeframe *frame;
    if(eye)
    {
        bool isNewFrame = eye->isNewFrame();
        if(isNewFrame)
        {
            eye->check_ff71();
          if (xdraw) {
            frame=eye->getLastVideoFramePointer();

            yuyvToRgb(frame->videoRightFrame, frame_rgb_right, eye->getWidth(), eye->getHeight());
            //videoTextureRight.loadData(frame_rgb_right, eye->getWidth(),eye->getHeight(), GL_RGB);

            yuyvToRgb(frame->videoLeftFrame, frame_rgb_left, eye->getWidth(), eye->getHeight());
            //videoTextureLeft.loadData(frame_rgb_left, eye->getWidth(),eye->getHeight(), GL_RGB);
         } else if (camFrameCount >100 and raw_index<100) {
           eye->getLastRawFrame(raw_frames[raw_index], raw_frame_size);
           raw_index++;
         }else {
           eye->getLastRawFrame(NULL, 0);
         }
            hasNewFrame = 1;
        }

        camFrameCount += isNewFrame ? 1: 0;
        long timeNow = ofGetElapsedTimeMillis();
        if( timeNow > camFpsLastSampleTime + 1000 ) {
            uint32_t framesPassed = camFrameCount - camFpsLastSampleFrame;
            camFps = (float)(framesPassed / ((timeNow - camFpsLastSampleTime)*0.001f));

            camFpsLastSampleTime = timeNow;
            camFpsLastSampleFrame = camFrameCount;
        }
    }
}
//--------------------------------------------------------------
void Data::record(){
    write_JPEG_file (frame_rgb_left, eye->getHeight(), eye->getWidth(), "/tmp/1_l.jpg", 100);
    write_JPEG_file (frame_rgb_right, eye->getHeight(), eye->getWidth(), "/tmp/1_r.jpg", 100);
}
void Data::record2(){
    if (raw_index <100) return;
    auto startTime = std::chrono::high_resolution_clock::now();
    auto myfile = std::fstream("/tmp/rawfames.dat", std::ios::out | std::ios::binary);
    for (int i=0; i<100; i++)
        myfile.write((char*)&raw_frames[i][0], raw_frame_size);
    myfile.close();
    auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "record time "<< std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() << std::endl;
   
}
//--------------------------------------------------------------
void Data::draw(Display *display, Window window, GC gc, int x, int y, int w, int h){
    /*
    ofSetHexColor(0xffffff);
    //uglyyyyy harcoded to mode 1 640x400
    //left
    videoTextureLeft.draw(0,0,eye->getWidth(),eye->getHeight());
    //right
    videoTextureRight.draw(640,0,eye->getWidth(),eye->getHeight());
    string str = "app fps: ";
    str += ofToString(ofGetFrameRate(), 2);
    str += "\neye fps: " + ofToString(camFps, 2);
    ofDrawBitmapString(str, 10, 15);
    */
    Visual *visual; 
    int screen;
    int depth; 
    int iw = w;
    int ih = h;
    unsigned char *idata= NULL;
    unsigned char *idata2= NULL;
    if (img_l==NULL) {
        screen = DefaultScreen(display);
        visual = DefaultVisual(display,screen); 
        depth  = DefaultDepth(display,screen);
        idata = (unsigned char*)malloc(iw*ih*4);
        img_l = XCreateImage (display,visual,depth,ZPixmap,0,(char*)idata,iw,ih,32,0);

        idata2 = (unsigned char*)malloc(iw*ih*4);
        img_r = XCreateImage (display,visual,depth,ZPixmap,0,(char*)idata2,iw,ih,32,0);
    } 
    
    idata = (unsigned char *)img_l->data;
    idata2 = (unsigned char *)img_r->data;
    for (int i=0; i<iw; i++)
        for (int j =0; j<ih; j++) {
           int idx = (i+j*iw)*4, s_idx=(i+j*iw)*3;
           /*for (int k=0; k<3; k++) {
               idata[idx+k] = frame_rgb_left[s_idx + k];
               idata2[idx+k] = frame_rgb_right[s_idx + k];
           }
           char r= idata[idx], b= idata[idx+2]; //RGB ->BGR
           idata[idx] = b; idata[idx+2] = r;
           r= idata2[idx]; b= idata2[idx+2];
           idata2[idx] = b; idata2[idx+2] = r;*/
           idata[idx] = frame_rgb_left[s_idx+2]; idata[idx+2] = frame_rgb_left[s_idx]; idata[idx+1] = frame_rgb_left[s_idx+1];
           idata2[idx] = frame_rgb_right[s_idx+2]; idata2[idx+2] = frame_rgb_right[s_idx]; idata2[idx+1] = frame_rgb_right[s_idx+1];
        }

    XPutImage(display,window,gc,img_l,0,0,x,y,w,h);
    XPutImage(display,window,gc,img_r,0,0,x+640,y,w,h);
    /*std::string str = "app fps: 60";
    XDrawString(display, window, gc,
                            100, h+50, str.c_str(), str.size());*/
}

void Data::exit() {
    // You should stop before exiting
    // otherwise the app will keep working
    if(eye)
    {
    std::cout << "Shutdown begin wait..." << std::endl;
    eye->shutdown();
    //
    delete[] frame_rgb_left;
    delete[] frame_rgb_right;

    }
    if (img_r)
        XDestroyImage(img_l);
    if (img_r)
        XDestroyImage(img_r);
    std::cout << "eye exited" << std::endl;
}

void *worker_usb(void* arg) { 
    struct t_arg *ta = (struct t_arg *)arg;
    stick_this_thread_to_core(6);
    set_realtime_priority();
    while (ta->run) { //refresh rate:30
//pthread_mutex_lock(&mutexData);
        bool res = ps4eye::PS4EYECam::updateDevices();
//pthread_mutex_unlock(&mutexData);
        if(!res) {
                break;
        }
    }
    std::cout << "polling exited" << std::endl;
    return NULL;
}

void *worker_draw(void* arg) { //background camera+draw thread
    struct t_arg *ta = (struct t_arg *)arg;
    stick_this_thread_to_core(5);

    Data *pdev = ta->pdev;
    long i = 0, j=0;
    long last = ofGetElapsedTimeMillis();
    while (ta->run) { //refresh rate:30
        sem_wait(&draw_sem);
        pdev->draw(ta->disp, ta->win, ta->gc, 0,0, 640,400);
        if (0) {   
            std::string str = "cam fps: " +std::to_string(int(pdev->camFps));
            XClearArea(ta->disp, ta->win,100,430, 250, 20, false); //no background, do nothing
            //XClearWindow(ta->disp, ta->win);
            XDrawRectangle(ta->disp, ta->win, ta->gc,
            //XFillRectangle(ta->disp, ta->win, ta->gc,
                            90,430, 150, 20);
                            
            XDrawString(ta->disp, ta->win, ta->gc,
                            100, 450, str.c_str(), str.size());
        //XFlush(ta->disp);
         }
    }
    return NULL;
}

void *worker(void* arg) { //background camera+draw thread
    struct t_arg *ta = (struct t_arg *)arg;
    stick_this_thread_to_core(7);
    set_realtime_priority();

    Data *pdev = ta->pdev;
    long i = 0, j=0;
    long last = ofGetElapsedTimeMillis();
    while (ta->run) { //refresh rate:30
        usleep(3*1000);
        long now = ofGetElapsedTimeMillis();
        //if( now - last<33 ) continue;
        
        pdev->update();
        if (pdev->hasNewFrame && pdev->xdraw) {
            // send signal to drawer
            sem_post(&draw_sem);
            pdev->hasNewFrame = 0;
        }
        i++;
        if( now > last + 1000 ) {
            uint32_t framesPassed = i - j;
            float appFps = (float)(framesPassed / ((now - last)*0.001f));

            last = now;
            j = i;
        
            ta->appFps = (int) appFps;
            std::string str = "app fps: " +std::to_string(ta->appFps) + "   ";
            std::string cstr = "cam fps: " +std::to_string(int(pdev->camFps)) + "   ";
            str = str+cstr+ " " + std::to_string(int(now/1000.0f)) ;
            std::cout << str << std::endl;
        }
    }
    std::cout << "worker exited" << std::endl;
    return NULL;
}

int main (int argc, char *argv[])
{
    Display                 *display;
    Visual                  *visual;
    int                     depth;
    int                     text_x;
    int                     text_y;
    XSetWindowAttributes    frame_attributes;
    Window                  frame_window;
    XFontStruct             *fontinfo;
    XGCValues               gr_values;
    GC                      graphical_context;
    XKeyEvent               event;
    char                    hello_string[] = "Hello World";
    int                     hello_string_length = strlen(hello_string);
    int draw = 0;

    if (argc >1) draw = std::stoi(argv[1], nullptr, 0);
    Data dev = Data();
    dev.setup(draw);
    sem_init(&draw_sem, 0, 0);

    display = XOpenDisplay(NULL);
    visual = DefaultVisual(display, 0);
    depth  = DefaultDepth(display, 0);
    
    frame_attributes.background_pixel = XWhitePixel(display, 0);
    /* create the application window */
    frame_window = XCreateWindow(display, XRootWindow(display, 0),
                                 0, 0, 1280, 500, 5, depth,
                                 InputOutput, visual, CWBackPixel,
                                 &frame_attributes);
    XSetWindowBackground(display, frame_window, 0xBEBEBE);
    XStoreName(display, frame_window, "Hello World Example");

//    XSelectInput(display, frame_window, ExposureMask | StructureNotifyMask|ButtonPressMask|KeyPressMask);
    XSelectInput(display, frame_window, KeyPressMask);

    fontinfo = XLoadQueryFont(display, "10x20");
    gr_values.font = fontinfo->fid;
    gr_values.foreground = XBlackPixel(display, 0);
    graphical_context = XCreateGC(display, frame_window, 
                                  GCFont+GCForeground, &gr_values);
    XMapWindow(display, frame_window);

    struct t_arg w_arg;
    w_arg.run = 1;
    w_arg.pdev = &dev;
    w_arg.disp = display;
    w_arg.win = frame_window;
    w_arg.gc = graphical_context;
    pthread_create(&th,NULL,worker, &w_arg);
    pthread_create(&th_usb,NULL,worker_usb, &w_arg);
    pthread_create(&th_draw,NULL,worker_draw, &w_arg);
    while ( 1 ) {
        XNextEvent(display, (XEvent *)&event);
        char k=0;
        switch ( event.type ) {
            case Expose:
            {
                XWindowAttributes window_attributes;
                int font_direction, font_ascent, font_descent;
                XCharStruct text_structure;
                XTextExtents(fontinfo, hello_string, hello_string_length, 
                             &font_direction, &font_ascent, &font_descent, 
                             &text_structure);
                XGetWindowAttributes(display, frame_window, &window_attributes);
                text_x = (window_attributes.width - text_structure.width)/2;
                text_y = (window_attributes.height - 
                          (text_structure.ascent+text_structure.descent))/2;
                XDrawString(display, frame_window, graphical_context,
                            text_x, text_y, hello_string, hello_string_length);
                break;
            }
            case KeyPress:
                //dev.draw(display, frame_window, graphical_context, 0,0, 640,400);
                //dev.draw(display, frame_window, graphical_context, 640, 0, 640,400);
                
                k = XLookupKeysym(&event, 0);
                if (k == XK_space)  {
                    fprintf (stdout, "The space bar was pressed.\n");
                }else{ fprintf (stdout, "The %c was pressed.\n", k); }
                if (k=='r'){
                    dev.record();
                }
                break;
            default:
                break;
        }
        if (k=='q') break;
    }
    w_arg.run = 0;
    void *thr;
    pthread_join(th, &thr); //wait for background thread to end
    //pthread_join(th_usb, &thr); //wait for background thread to end
    //pthread_join(th_draw, &thr); //wait for background thread to end
    dev.exit(); //close device

    pthread_join(th_usb, &thr); //wait for background thread to end
    //pthread_join(th_draw, &thr); //wait for background thread to end

    return(0);
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


struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

void my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}
int read_JPEG_file (char * filename)
{
  /* This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
  struct jpeg_decompress_struct cinfo;
  /* We use our private extension JPEG error handler.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  struct my_error_mgr jerr;
  /* More stuff */
  FILE * infile;		/* source file */
  JSAMPARRAY buffer;		/* Output row buffer */
  int row_stride;		/* physical row width in output buffer */

  /* In this example we want to open the input file before doing anything else,
   * so that the setjmp() error recovery below can assume the file is open.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to read binary files.
   */

  if ((infile = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    return 0;
  }

  /* Step 1: allocate and initialize JPEG decompression object */

  /* We set up the normal JPEG error routines, then override error_exit. */
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;
  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp(jerr.setjmp_buffer)) {
    /* If we get here, the JPEG code has signaled an error.
     * We need to clean up the JPEG object, close the input file, and return.
     */
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    return 0;
  }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, infile);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.txt for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   * In this example, we need to make an output work buffer of the right size.
   */ 
  /* JSAMPLEs per row in output buffer */
  row_stride = cinfo.output_width * cinfo.output_components;
  /* Make a one-row-high sample array that will go away when done with image */
  buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  /* Step 6: while (scan lines remain to be read) */
  /*           jpeg_read_scanlines(...); */

  /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
  while (cinfo.output_scanline < cinfo.output_height) {
    /* jpeg_read_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could ask for
     * more than one scanline at a time if that's more convenient.
     */
    (void) jpeg_read_scanlines(&cinfo, buffer, 1);
    /* Assume put_scanline_someplace wants a pointer and sample count. */
    // WHY?? put_scanline_someplace(buffer[0], row_stride);
  }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  fclose(infile);

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
  return 1;
}

//yet another reader
jmp_buf env;
void error_handler (j_common_ptr cinfo)
{
  // Does nothing, it's just to prevent libjpeg from exiting
  longjmp(env, 1);
}

// Read a jpeg image, returns 0 if not recognized.
// Returns width, height
unsigned char* read_jpeg(const char* sFile, int& iW, int& iH)
{
  FILE *f = fopen(sFile,"rb");
  if(f==NULL)
    return 0;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = error_handler;

  unsigned char* buf =0;
  
  /* Establish the setjmp return context to prevent jpeglib from exiting. */
  if (setjmp(env)) {
    /* If we get here, the JPEG code has signaled an error.
     * We need to clean up the JPEG object, close the input file, and return.
     */
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    if(buf) free(buf);

    return 0;
  }
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, f);
  if(jpeg_read_header(&cinfo, TRUE)!=JPEG_HEADER_OK)
    return 0;

  jpeg_calc_output_dimensions(&cinfo);
  iW = cinfo.output_width;
  iH = cinfo.output_height;
  // Tell libjpeg to convert to RGB
  cinfo.out_color_space = JCS_RGB;

  buf = (unsigned char*)malloc(iW*iH*3);
  if(buf==NULL) return 0;

  jpeg_start_decompress(&cinfo);

  while(cinfo.output_scanline<cinfo.output_height)
    {
      unsigned char* pImage = buf+cinfo.output_scanline*3*iW;
      jpeg_read_scanlines(&cinfo,&pImage,1);
    }

  jpeg_finish_decompress(&cinfo);

  jpeg_destroy_decompress(&cinfo);

  fclose(f);

  return buf;
}
void set_realtime_priority() {
     int ret;

     // We'll operate on the currently running thread.
     pthread_t this_thread = pthread_self();
     // struct sched_param is used to store the scheduling priority
     struct sched_param params;

     // We'll set the priority to the maximum.
     params.sched_priority = sched_get_priority_max(SCHED_FIFO);
     std::cout << "Trying to set thread realtime prio = " << params.sched_priority << std::endl;

     // Attempt to set thread real-time priority to the SCHED_FIFO policy
     ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
     if (ret != 0) {
         // Print the error
         std::cout << "Unsuccessful in setting thread realtime prio" << std::endl;
         return;     
     }
     // Now verify the change in thread priority
     int policy = 0;
     ret = pthread_getschedparam(this_thread, &policy, &params);
     if (ret != 0) {
         std::cout << "Couldn't retrieve real-time scheduling paramers" << std::endl;
         return;
     }

     // Check the correct policy was applied
     if(policy != SCHED_FIFO) {
         std::cout << "Scheduling is NOT SCHED_FIFO!" << std::endl;
     } else {
         std::cout << "SCHED_FIFO OK" << std::endl;
     }

     // Print thread scheduling priority
     std::cout << "Thread priority is " << params.sched_priority << std::endl; 
}

int stick_this_thread_to_core(int core_id) {
   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   if (core_id < 0 || core_id >= num_cores)
      return EINVAL;

   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);

   pthread_t current_thread = pthread_self();    
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

