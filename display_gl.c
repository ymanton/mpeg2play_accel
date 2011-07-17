/* display.c, X11 interface                                                 */

/* Copyright (C) 1994, MPEG Software Simulation Group. All Rights Reserved. */

/*
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
 * any and all warranties, whether express, implied, or statuary, including any
 * implied warranties or merchantability or of fitness for a particular
 * purpose.  In no event shall the copyright-holder be liable for any
 * incidental, punitive, or consequential damages of any kind whatsoever
 * arising from the use of these programs.
 *
 * This disclaimer of warranty extends to the user of these programs and user's
 * customers, employees, agents, transferees, successors, and assigns.
 *
 * The MPEG Software Simulation Group does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any third-party
 * patents.
 *
 * Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
 * are subject to royalty fees to patent holders.  Many of these patents are
 * general enough such that they are unavoidable regardless of implementation
 * design.
 *
 */

/* XvMC modifications added by Mark Vojkovich <markv@XFree86.org> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include "README.XVMC"

#include <X11/Xlib.h>
#include <X11/extensions/XvMClib.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include "config.h"
#include "global.h"

#define MAX_SURFACES 5

typedef struct {
  int reference;
  unsigned long sequence_number;
} surface_rec;

static unsigned long sequence_number = 1;

/* X11 related variables */
static Display *display;
static Window window;

static XvMCSurface *target = NULL;
static XvMCSurface *past   = NULL;
static XvMCSurface *future = NULL;
static XvMCSurface *next_displayed = NULL;

static int numsurfaces = MAX_SURFACES;

static XvMCSurface surfaces[MAX_SURFACES];
static surface_rec surface_info[MAX_SURFACES];

static int width;
static int height;
static int num_blocks;

static short *_block;
static XvMCMacroBlock *_mb;
static int _next_index;

static XvMCContext context;
static XvMCBlockArray blocks;
static XvMCMacroBlockArray macro_blocks;

static int unsignedIntra = 0;
static int useIDCT = 0;
static XvPortID portNum;

static unsigned char uiclip[1024];
static unsigned char *uiclp;
static char iclip[1024];
static char *iclp;
static short niclip[1024];
static short *niclp;

GLXPbuffer gl_pbuffer;
GLXContext gl_context;
GLXWindow gl_window;
GLXFBConfig gl_fbconfig;

#ifdef USE_NV_FENCE
GLuint nvFence;
#endif

static Bool 
GetPortId(Display *dpy, XvPortID *portID, int *surface_type)
{
   int i, j, k, numAdapt, numTypes, eventBase, errorBase;
   XvMCSurfaceInfo *surfaceInfo;
   XvAdaptorInfo *info;
   Bool result = 0;

   if(!XvMCQueryExtension(dpy, &eventBase, &errorBase))
	return 0;

   if(Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&numAdapt,&info))
        return 0;

   for(i = 0; i < numAdapt; i++) {
      if(info[i].type & XvImageMask) {  
         surfaceInfo = XvMCListSurfaceTypes(display, info[i].base_id,
                                               &numTypes);

         if(surfaceInfo) {
            for(j = 0; j < numTypes; j++) {
               if((surfaceInfo[j].chroma_format == XVMC_CHROMA_FORMAT_420) &&
                  (surfaceInfo[j].max_width >= coded_picture_width) &&
                  (surfaceInfo[j].max_height >= coded_picture_height) &&
                   ((surfaceInfo[j].mc_type == (XVMC_MOCOMP | XVMC_MPEG_2)) ||
                    (surfaceInfo[j].mc_type == (XVMC_IDCT | XVMC_MPEG_2))))
               {
                   if(use_idct != -1) {
                      if(use_idct == 1) {
                         if(surfaceInfo[j].mc_type == 
                                    (XVMC_MOCOMP | XVMC_MPEG_2))
                             continue;
                      } else {
                         if(surfaceInfo[j].mc_type == 
                                    (XVMC_IDCT | XVMC_MPEG_2))
                             continue;
                      }
                   }
                   for(k = 0; k < info[i].num_ports; k++) {
                        /* try to grab a port */
                        if(Success == XvGrabPort(dpy, info[i].base_id + k,
                                        CurrentTime))
                        {   
                            *portID = info[i].base_id + k;
                            *surface_type = surfaceInfo[j].surface_type_id;
                            result = 1;
                            break;
                        }
                    }
                    if(result) {
                       if(surfaceInfo[j].flags & XVMC_INTRA_UNSIGNED)
                           unsignedIntra = 1;
                       if(surfaceInfo[j].mc_type == (XVMC_IDCT | XVMC_MPEG_2))
                           useIDCT = 1;
                       break;
                    }
               }
            }
            XFree(surfaceInfo);
         }
      }
   }

   XvFreeAdaptorInfo(info);

   return result;
}

#ifdef USE_DLOPEN

#define XvMCCreateContext xvmcCreateContext
#define XvMCDestroyContext xvmcDestroyContext
#define XvMCCreateSurface xvmcCreateSurface
#define XvMCDestroySurface xvmcDestroySurface
#define XvMCPutSurface xvmcPutSurface
#define XvMCHideSurface xvmcHideSurface
#define XvMCSyncSurface xvmcSyncSurface
#define XvMCFlushSurface xvmcFlushSurface
#define XvMCGetSurfaceStatus xvmcGetSurfaceStatus
#define XvMCCreateBlocks xvmcCreateBlocks
#define XvMCDestroyBlocks xvmcDestroyBlocks
#define XvMCCreateMacroBlocks xvmcCreateMacroBlocks
#define XvMCDestroyMacroBlocks xvmcDestroyMacroBlocks
#define XvMCRenderSurface xvmcRenderSurface
#define XvMCCopySurfaceToGLXPbuffer xvmcCopySurfaceToGLXPbuffer

Status (*XvMCCreateContext)(Display*, XvPortID, int, int, int, int, XvMCContext*);
Status (*XvMCDestroyContext)(Display*, XvMCContext*);
Status (*XvMCCreateSurface)(Display*, XvMCContext*, XvMCSurface*);
Status (*XvMCDestroySurface)(Display*, XvMCSurface*);
Status (*XvMCPutSurface)(Display*,XvMCSurface*,Drawable,short,short,
                         unsigned short, unsigned short, short, short,
                         unsigned short, unsigned short, int);
Status (*XvMCHideSurface)(Display*, XvMCSurface*);
Status (*XvMCSyncSurface)(Display*, XvMCSurface*);
Status (*XvMCFlushSurface)(Display*, XvMCSurface*);
Status (*XvMCGetSurfaceStatus)(Display*,XvMCSurface*,int*);
Status (*XvMCCreateBlocks)(Display*,XvMCContext*,unsigned int, XvMCBlockArray*);
Status (*XvMCDestroyBlocks)(Display*, XvMCBlockArray*);
Status (*XvMCCreateMacroBlocks)(Display*,XvMCContext*, unsigned int, 
                                XvMCMacroBlockArray*);
Status (*XvMCDestroyMacroBlocks)(Display*, XvMCMacroBlockArray*);
Status (*XvMCRenderSurface)(Display*,XvMCContext*,unsigned int,
                            XvMCSurface*,XvMCSurface*,XvMCSurface*,
                            unsigned int, unsigned int, unsigned int,
                            XvMCMacroBlockArray*, XvMCBlockArray*);
Status (*XvMCCopySurfaceToGLXPbuffer) (Display *,XvMCSurface*,XID,short,short,
                                       unsigned short, unsigned short,
                                       short, short, unsigned int, int);

#define GET_SYM(s, sn) \
   if(!(s = dlsym(handle, sn))) { \
      printf("%s\n", dlerror()); \
      failure = 1; \
   }


static Bool
ResolveFunctions (char *filename)
{
   void *handle;
   Bool failure = 0;

   if(!(handle = dlopen(filename, RTLD_NOW))) {
      printf("%s\n", dlerror());
      return 0;
   }

   GET_SYM(XvMCCreateContext, "XvMCCreateContext");
   GET_SYM(XvMCDestroyContext , "XvMCDestroyContext");
   GET_SYM(XvMCCreateSurface , "XvMCCreateSurface");
   GET_SYM(XvMCDestroySurface , "XvMCDestroySurface");
   GET_SYM(XvMCPutSurface , "XvMCPutSurface");
   GET_SYM(XvMCHideSurface , "XvMCHideSurface");
   GET_SYM(XvMCSyncSurface , "XvMCSyncSurface");
   GET_SYM(XvMCFlushSurface , "XvMCFlushSurface");
   GET_SYM(XvMCGetSurfaceStatus , "XvMCGetSurfaceStatus");
   GET_SYM(XvMCCreateBlocks , "XvMCCreateBlocks");
   GET_SYM(XvMCDestroyBlocks , "XvMCDestroyBlocks");
   GET_SYM(XvMCCreateMacroBlocks , "XvMCCreateMacroBlocks");
   GET_SYM(XvMCDestroyMacroBlocks , "XvMCDestroyMacroBlocks");
   GET_SYM(XvMCRenderSurface , "XvMCRenderSurface");
   GET_SYM(XvMCCopySurfaceToGLXPbuffer , "XvMCCopySurfaceToGLXPbuffer");

   return !failure;
}

#endif

static const int attr_fbconfig[] =
{
   GLX_CONFIG_CAVEAT, GLX_NONE,
   GLX_RENDER_TYPE, GLX_RGBA_BIT,
   GLX_RED_SIZE, 5,
   GLX_GREEN_SIZE, 5,
   GLX_BLUE_SIZE, 5,
   GLX_DOUBLEBUFFER, 1,
   GLX_STEREO, 0,
   GLX_LEVEL, 0,
   GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT,
   GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
   0
};

static void PrintVisual(void)
{
   int r, g, b, a, d, s, v;

   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_RED_SIZE, &r);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_GREEN_SIZE, &g);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_BLUE_SIZE, &b);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_ALPHA_SIZE, &a);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_DEPTH_SIZE, &d);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_STENCIL_SIZE, &s);
   glXGetFBConfigAttrib (display, gl_fbconfig, GLX_VISUAL_ID, &v);

   printf("Visual 0x%x\n", v);
   printf("A:R:G:B = %i:%i:%i:%i\n", a, r, g, b);
   printf("depth = %i, stencil = %i\n", d, s);
}

static int PowerOfTwo(int w)
{
    int Pof2 = 0;
    int i = 12;

    while(--i) {
        if(w & (1 << i)) {
            Pof2 = i;
            if(w & ((1 << i) - 1))
                Pof2++;
            break;
        }
    }
    return Pof2;
}

static int tex_w, tex_h;

void init_display()
{
   int surface_type_id, result, i, value, scrn, num_fbconfigs;
   GLXFBConfig *fbconfigs;
   XVisualInfo *visInfo;
   XSetWindowAttributes attributes;
   Window root;
   int attrib_pbuffer[6] = {GLX_PBUFFER_WIDTH, 0, 
                            GLX_PBUFFER_HEIGHT, 0,
                            GLX_PRESERVED_CONTENTS,
                            0};

   width = horizontal_size;
   height = vertical_size;

   if(chroma_format != CHROMA420)
      error("we only support 4:2:0 chroma formats\n");

   display = XOpenDisplay(NULL);
   root = XDefaultRootWindow(display);
   scrn = XDefaultScreen(display);

   if(!GetPortId(display, &portNum, &surface_type_id))
      error("couldn't find a suitable port\n");



#ifdef USE_DLOPEN
   if(!ResolveFunctions(DLFILENAME))
      error("couldn't resolve necessary functions\n");
#endif

   result = XvMCCreateContext(display, portNum, surface_type_id, 
                              coded_picture_width, coded_picture_height,
                              XVMC_DIRECT, &context);

   if(result != Success)
      error("couldn't create XvMCContext\n");

   for(i = 0; i < numsurfaces; i++) {
      result = XvMCCreateSurface(display, &context, &surfaces[i]);
      if(result != Success) {
          if(i < 4) {
             XvMCDestroyContext(display, &context);
             error("couldn't create enough XvMCSurfaces\n");
          } else {
             numsurfaces = i;
             printf("could only allocate %i surfaces\n", numsurfaces);
          }
      } 
      surface_info[i].reference = 0;
      surface_info[i].sequence_number = 0;      
   }

   slices = slices * mb_width;

   XvMCCreateBlocks(display, &context, slices * 6, &blocks);
   XvMCCreateMacroBlocks(display, &context, slices, &macro_blocks);

   fbconfigs = glXChooseFBConfig(display, scrn, attr_fbconfig, &num_fbconfigs);

   gl_fbconfig = *fbconfigs;

   /* find the first one with no depth buffer */
   for(i = 0; i < num_fbconfigs; i++) {
      glXGetFBConfigAttrib(display, fbconfigs[i], GLX_DEPTH_SIZE, &value);
      if(value == 0) {
         gl_fbconfig = fbconfigs[i];
         break;
      }
   }

   PrintVisual();
 
   visInfo = glXGetVisualFromFBConfig(display, gl_fbconfig);

   attrib_pbuffer[1] = width;
   attrib_pbuffer[3] = bob ? (height/2) : height;
   gl_pbuffer = glXCreatePbuffer(display, gl_fbconfig, attrib_pbuffer);

   gl_context = glXCreateNewContext(display, gl_fbconfig, GLX_RGBA_TYPE, 
                                    NULL, 1);

   attributes.colormap = XCreateColormap(display, root, visInfo->visual,
                                         AllocNone);

   window = XCreateWindow(display, root, 0, 0, width, height, 0,
                          visInfo->depth, InputOutput,
                          visInfo->visual, CWColormap, &attributes);

   gl_window = glXCreateWindow(display, gl_fbconfig, window, NULL);

   XSelectInput(display, window, KeyPressMask | StructureNotifyMask |
                                 Button1MotionMask | ButtonPressMask);
   XMapWindow(display, window);

   glXMakeContextCurrent(display, gl_window, gl_pbuffer, gl_context);
   glDrawBuffer(GL_BACK);
   glReadBuffer(GL_FRONT_LEFT);

   tex_w =  1 << PowerOfTwo(width);
   tex_h = 1 << PowerOfTwo(bob ? (height/2) : height);

   printf("%i x %i texture\n", tex_w, tex_h);

   glClearColor (0.0, 0.0, 0.0, 0.0);

   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, 3, tex_w, tex_h,
                0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
   glEnable(GL_TEXTURE_2D);
   glShadeModel(GL_FLAT);

   glViewport(0, 0, width, height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-1.0, 1.0, -1.0, 1.0, 2, 18.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -8);

#ifdef USE_NV_FENCE
   glGenFencesNV(1, &nvFence);
   glSetFenceNV(&nvFence, GL_ALL_COMPLETED_NV);
#endif

   XSync(display, 0);

   uiclp = uiclip+512;
   for (i= -512; i<512; i++)
      uiclp[i] = (i<-128) ? 0 : ((i>127) ? 255 : i+128);

   iclp = iclip+512;
   for (i= -512; i<512; i++)
      iclp[i] = (i<-128) ? -128 : ((i>127) ? 127 : i);

   niclp = niclip+512;
   for (i= -512; i<512; i++)
   niclp[i] = (i<-256) ? -256 : ((i>255) ? 255 : i);

}

void exit_display()
{
   int i;

   glXMakeContextCurrent(display, None, None, NULL);
   glXDestroyContext(display, gl_context);
   glXDestroyWindow(display, gl_window);
   glXDestroyPbuffer(display, gl_pbuffer);

#ifdef USE_NV_FENCE
   glDeleteFencesNV(1, &nvFence);
#endif

   XDestroyWindow(display, window);

   for(i = 0; i < numsurfaces; i++) {
     XvMCDestroySurface(display, &surfaces[i]);
   }

   XvMCDestroyBlocks(display, &blocks);
   XvMCDestroyMacroBlocks(display, &macro_blocks);
   XvMCDestroyContext(display, &context);
   XvUngrabPort(display, portNum, CurrentTime);
  
   XCloseDisplay(display);
}

void assign_surfaces (void)
{
   int oldest_nonref = -1;
   unsigned long age;
   int i;

   _block = blocks.blocks;
   _next_index = 0;
   _mb = macro_blocks.macro_blocks;
   num_blocks = 0;

   for(age = ~0, i = 0; i < numsurfaces; i++) {
       if(!surface_info[i].reference && 
          (surface_info[i].sequence_number < age))
       {
          age = surface_info[i].sequence_number;
          oldest_nonref = i;
       }  
   } 

   if(pict_type == B_TYPE) {
      next_displayed = target = &surfaces[oldest_nonref];
   } else {
      int oldest_ref = -1;

      for(age = ~0, i = 0; i < numsurfaces; i++) {
          if(surface_info[i].reference &&
             (surface_info[i].sequence_number < age))
          {
             age = surface_info[i].sequence_number;
             oldest_ref = i;
          } 
      }

      if(oldest_ref != -1) {
         int newest_ref = -1;

         for(age = 0, i = 0; i < numsurfaces; i++) {
             if(surface_info[i].reference &&
                (surface_info[i].sequence_number > age))
             {
                age = surface_info[i].sequence_number;
                newest_ref = i;
             }
         }

         if(oldest_ref != newest_ref) 
             surface_info[oldest_ref].reference = 0;
         past = next_displayed = &surfaces[newest_ref];
      } else {
         past = &surfaces[oldest_nonref];
      }
      future = target = &surfaces[oldest_nonref];
      surface_info[oldest_nonref].reference = 1;
   }
   surface_info[oldest_nonref].sequence_number = sequence_number++;
}

static XvMCSurface *last_displayed = NULL;


void display_gl(XvMCSurface *surface, int field)
{
    float tw, th, bob_delta = 0.0;
    int h = vertical_size; 

    if(field != XVMC_FRAME_PICTURE) {
        h >>= 1;
        bob_delta = 0.25 / (float)tex_h;
        if(field == XVMC_TOP_FIELD)
           bob_delta = -bob_delta;
    }

    tw = (float)horizontal_size / (float)tex_w;
    th = (float)h / (float)tex_h;

#ifdef USE_NV_FENCE
    glFinishFenceNV(&nvFence);
#else
    glFinish();
#endif

    XvMCCopySurfaceToGLXPbuffer(display, surface, gl_pbuffer, 0, 0,
                                horizontal_size, h, 0, 0,
                                GL_FRONT_LEFT, field);

    glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 
                         0, 0, 0, 0, horizontal_size, h);

#ifdef USE_NV_FENCE
    glSetFenceNV(&nvFence, GL_ALL_COMPLETED_NV);
#endif

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);

    glTexCoord2f(0.0, 0.0 + bob_delta); glVertex2f(-4.0, -4.0);
    glTexCoord2f(0.0, th + bob_delta); glVertex2f(-4.0, 4.0);
    glTexCoord2f(tw, th + bob_delta); glVertex2f(4.0, 4.0);
    glTexCoord2f(tw, 0.0 + bob_delta); glVertex2f(4.0, -4.0);

    glEnd();

    glXSwapBuffers(display, gl_window);
}

static int x_root = 0;
static int y_root = 0;

int display_frame(int field)
{
    Window dummyWindow;
    int DX, DY, dummy;
    float rot_X, rot_Y;
    XEvent event;

    if((outtype == T_X11) && next_displayed) {
        display_gl(next_displayed, field);
        last_displayed = next_displayed;
    }

    while(XPending(display)) {
        XNextEvent(display, &event);

        switch(event.type) {
        case KeyPress:
            return 0;
        case ButtonPress:
            if(event.xbutton.button != 1) {
	       glLoadIdentity();
               glTranslatef(0.0, 0.0, -8);
            } else {
               XQueryPointer(display, window, &dummyWindow, &dummyWindow,
                             &x_root, &y_root, &dummy, &dummy, &dummy);
            }
            break;
        case MotionNotify:
            DX = event.xmotion.x_root - x_root;
            DY = event.xmotion.y_root - y_root;

            x_root = event.xmotion.x_root;
            y_root = event.xmotion.y_root;

            rot_X = (float)DX / 4.0;
            rot_Y = (float)DY / 4.0;

            glRotatef(rot_Y, 1.0, 0.0, 0.0);
            glRotatef(rot_X, 0.0, 1.0, 0.0);

            break;
        case ConfigureNotify:
            width = event.xconfigure.width;
            height = event.xconfigure.height;
            glViewport(0, 0, width, height);
            break;
        default:
            break;
        }
    }

    return 1;
}

void display_field(int field)
{
    if((outtype == T_X11) && next_displayed) 
        display_gl(next_displayed, field);
}

void display_second_field(int field)
{
    if(last_displayed) 
        display_gl(last_displayed, field);
}

void putlast(int framenum)
{
  if(outtype == T_X11) {
     if (secondfield) {
        /* we could if we were bobbing */
        printf("last frame incomplete, not displayed\n");
     } else if(next_displayed) {
        if(bob)
           display_gl(next_displayed, XVMC_BOTTOM_FIELD);
        else
           display_gl(next_displayed, XVMC_FRAME_PICTURE);
     }
  }
}

void add_macroblock(
   int x,
   int y,
   int mb_type,
   int motion_type,
   int (*PMV)[2][2],
   int (*mv_field_sel)[2],
   int *dmvector,
   int cbp,
   int dct_type
)
{
   int skip = 0;
   _mb->x = x;
   _mb->y = y;
  
   if(mb_type & MB_INTRA) {
      _mb->macroblock_type = XVMC_MB_TYPE_INTRA;
   } else {
      /* XvMC doesn't support skips */
      if(!(mb_type & (MB_BACKWARD | MB_FORWARD))) {
         _mb->macroblock_type = XVMC_MB_TYPE_MOTION_FORWARD;
         motion_type = (pict_struct==FRAME_PICTURE) ? MC_FRAME : MC_FIELD;
         _mb->PMV[0][0][0] = 0;
         _mb->PMV[0][0][1] = 0;
         skip = 1;
      } else {
         _mb->macroblock_type = 0;
         if(mb_type & MB_FORWARD) {
            _mb->macroblock_type = XVMC_MB_TYPE_MOTION_FORWARD;
            _mb->PMV[0][0][0] = PMV[0][0][0];
            _mb->PMV[0][0][1] = PMV[0][0][1];
            _mb->PMV[1][0][0] = PMV[1][0][0];
            _mb->PMV[1][0][1] = PMV[1][0][1];
         }
         if(mb_type & MB_BACKWARD) {
            _mb->macroblock_type |= XVMC_MB_TYPE_MOTION_BACKWARD;
            _mb->PMV[0][1][0] = PMV[0][1][0];
            _mb->PMV[0][1][1] = PMV[0][1][1];
            _mb->PMV[1][1][0] = PMV[1][1][0];
            _mb->PMV[1][1][1] = PMV[1][1][1];
         }
      }
      if((mb_type & MB_PATTERN) && cbp)
        _mb->macroblock_type |= XVMC_MB_TYPE_PATTERN;

      _mb->motion_type = motion_type;

      if(motion_type == MC_DMV) {
         int DMV[2][2];

         if(pict_struct==FRAME_PICTURE) {
             calc_DMV(DMV,dmvector,PMV[0][0][0],PMV[0][0][1]>>1);
             _mb->PMV[0][1][0] = PMV[0][0][0];
             _mb->PMV[0][1][1] = PMV[0][0][1];
             _mb->PMV[1][0][0] = DMV[0][0];
             _mb->PMV[1][0][1] = DMV[0][1] << 1; 
             _mb->PMV[1][1][0] = DMV[1][0];
             _mb->PMV[1][1][1] = DMV[1][1] << 1;
         } else {
             calc_DMV(DMV,dmvector,PMV[0][0][0],PMV[0][0][1]);
             _mb->PMV[0][1][0] = DMV[0][0];
             _mb->PMV[0][1][1] = DMV[0][1];
         }
      }

      if((motion_type == MC_FIELD) || (motion_type == MC_16X8)) {
       if(skip) {
           _mb->motion_vertical_field_select = 
                (pict_struct == BOTTOM_FIELD) ? 1 : 0;
       } else {
           _mb->motion_vertical_field_select = 0;
           if(mv_field_sel[0][0])
              _mb->motion_vertical_field_select |= 1;
           if(mv_field_sel[0][1])
              _mb->motion_vertical_field_select |= 2;
           if(mv_field_sel[1][0])
              _mb->motion_vertical_field_select |= 4;
           if(mv_field_sel[1][1])
              _mb->motion_vertical_field_select |= 8;
         }
      }
   }
   _mb->index = ((unsigned long)_block - (unsigned long)blocks.blocks) >> 7;
   _mb->dct_type = dct_type;
   _mb->coded_block_pattern = cbp;

   _block += _next_index;
   _next_index = 0;
   num_blocks++;
   _mb++;

   if(num_blocks == slices) {
       XvMCSurface *_past = NULL, *_future = NULL;

       switch(pict_type) {
       case I_TYPE: break;
       case P_TYPE: if(!(_past = past)) 
                       _past = target; 
                    break;
       case B_TYPE: _past = past;
                    _future = future;
                    break;
       }
       
       XvMCRenderSurface(display, &context, pict_struct,
                         target, _past, _future, 
                         secondfield ? XVMC_SECOND_FIELD : 0,
                         slices, 0, &macro_blocks, &blocks);
       XvMCFlushSurface(display, target);
       num_blocks = 0;
       _mb = macro_blocks.macro_blocks;
       _block = blocks.blocks;
    }
}

static short preallocBlock[64];

void get_block(void)
{
   int *bp;
   int i;

   ld->block = useIDCT ? (_block + _next_index)  : preallocBlock;

   bp = (int *)ld->block;

   for(i = 0; i < 32; i++)
      bp[i] = 0;
}

void add_block(int intra)
{
   if(!useIDCT) {
     short *blk = ld->block;
     short *_blk = _block + _next_index;
     int i;
     idct(blk);

     if(intra) {
        if(unsignedIntra) {
           for(i=0; i<64; i++) 
              _blk[i] = uiclp[blk[i]];
        } else {
           for(i=0; i<64; i++)
              _blk[i] = iclp[blk[i]];
        }
     } else {
        for(i=0; i<64; i++)
           _blk[i] = niclp[blk[i]];
     }
   }
   _next_index += 64;
}
