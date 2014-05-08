/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

// File: vogltest.cpp
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "vogl_core.h"
#include "vogl_colorized_console.h"
#include "vogl_command_line_params.h"

#include <GL/gl.h>
#include <GL/glx.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>

#include "regex/regex.h"

using namespace vogl;

// External function which has a bunch of voglcore tests.
int run_voglcore_tests(int argc, char *argv[]);

#define PI 3.14159265

// Store all system info in one place
typedef struct RenderContextRec
{
    GLXContext ctx;
    Display *dpy;
    Window win;
    int nWinWidth;
    int nWinHeight;
    int nMousePosX;
    int nMousePosY;

} RenderContext;

#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARBFuncPtr;

#ifdef LOAD_LIBGLITRACE
typedef __GLXextFuncPtr (*glXGetProcAddressARBProc)(const GLubyte *procName);
static glXGetProcAddressARBProc glXGetProcAddressARBFuncPtr;

#pragma message("Using LOAD_LIBGLITRACE path.")
typedef void(GLAPIENTRY *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, GLvoid *userParam);
typedef int GLfixed;

#define GL_FUNC(category, ret, name, args, params) typedef ret(*name##_func_ptr_t) args;
#define GL_FUNC_VOID(category, name, args, params) typedef void(*name##_func_ptr_t) args;
#define GL_EXT(name, major, minor)
#include "glfuncs.h"
#undef GL_FUNC
#undef GL_FUNC_VOID
#undef GL_EXT

#define GL_FUNC(category, ret, name, args, params) static name##_func_ptr_t g_##name##_func_ptr;
#define GL_FUNC_VOID(category, name, args, params) static name##_func_ptr_t g_##name##_func_ptr;
#define GL_EXT(name, major, minor)
#include "glfuncs.h"
#undef GL_FUNC
#undef GL_FUNC_VOID
#undef GL_EXT

static void *g_pLib_gl;

#include <dlfcn.h>
static void LoadGL()
{
    g_pLib_gl = dlopen("./libvogltrace.so", RTLD_LAZY);
    if (!g_pLib_gl)
    {
        fprintf(stderr, "Failed loading libvogltrace.so!\n");
        exit(EXIT_FAILURE);
    }

    glXGetProcAddressARBFuncPtr = (glXGetProcAddressARBProc)dlsym(g_pLib_gl, "gliGetProcAddressRAD");
    if (!glXGetProcAddressARBFuncPtr)
    {
        fprintf(stderr, "Failed finding dynamic symbol gliGetProcAddressRAD in libvogltrace.so!\n");
        exit(EXIT_FAILURE);
    }

#define GL_FUNC(category, ret, name, args, params)                                              \
    g_##name##_func_ptr = (name##_func_ptr_t) (*glXGetProcAddressARBFuncPtr)((GLubyte *)#name); \
    if (!g_##name##_func_ptr)                                                                   \
    {                                                                                           \
        fprintf(stderr, "Failed getting func %s!\n", #name);                                    \
    }
#define GL_FUNC_VOID(category, name, args, params)                                              \
    g_##name##_func_ptr = (name##_func_ptr_t) (*glXGetProcAddressARBFuncPtr)((GLubyte *)#name); \
    if (!g_##name##_func_ptr)                                                                   \
    {                                                                                           \
        fprintf(stderr, "Failed getting func %s!\n", #name);                                    \
    }
#define GL_EXT(name, major, minor)
#include "glfuncs.h"
#undef GL_FUNC
#undef GL_FUNC_VOID
#undef GL_EXT
}
#define CALL_GL(name) g_##name##_func_ptr

#else
#pragma message("Using implicitly loaded libgl path.")

#define CALL_GL(name) name

static void LoadGL()
{
}
#endif

void EarlyInitGLXfnPointers()
{
    LoadGL();
    glXCreateContextAttribsARBFuncPtr = (glXCreateContextAttribsARBProc)CALL_GL(glXGetProcAddressARB)((GLubyte *)"glXCreateContextAttribsARB");

    //printf("glXCreateContextAttribsARB = %p %p\n", glXCreateContextAttribsARB, &glXCreateContextAttribsARB);
}

//------------------------

void CreateWindow(RenderContext *rcx)
{
    XSetWindowAttributes winAttribs;
    GLint winmask;
    GLint nMajorVer = 0;
    GLint nMinorVer = 0;
    XVisualInfo *visualInfo;

    static int fbAttribs[] =
        {
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_X_RENDERABLE, True,
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_DOUBLEBUFFER, True,
            GLX_RED_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            0
        };

    EarlyInitGLXfnPointers();

    // Tell X we are going to use the display
    rcx->dpy = XOpenDisplay(NULL);

    // Get Version info
    CALL_GL(glXQueryVersion)(rcx->dpy, &nMajorVer, &nMinorVer);
    printf("Supported GLX version - %d.%d\n", nMajorVer, nMinorVer);

    if (nMajorVer == 1 && nMinorVer < 2)
    {
        printf("ERROR: GLX 1.2 or greater is necessary\n");
        XCloseDisplay(rcx->dpy);
        exit(0);
    }

    // Get a new fb config that meets our attrib requirements
    GLXFBConfig *fbConfigs;
    int numConfigs = 0;
    fbConfigs = CALL_GL(glXChooseFBConfig)(rcx->dpy, DefaultScreen(rcx->dpy), fbAttribs, &numConfigs);
    visualInfo = CALL_GL(glXGetVisualFromFBConfig)(rcx->dpy, fbConfigs[0]);

    // Now create an X window
    winAttribs.event_mask = ExposureMask | VisibilityChangeMask |
                            KeyPressMask | PointerMotionMask |
                            StructureNotifyMask;

    winAttribs.border_pixel = 0;
    winAttribs.bit_gravity = StaticGravity;
    winAttribs.colormap = XCreateColormap(rcx->dpy,
                                          RootWindow(rcx->dpy, visualInfo->screen),
                                          visualInfo->visual, AllocNone);
    winmask = CWBorderPixel | CWBitGravity | CWEventMask | CWColormap;

    rcx->win = XCreateWindow(rcx->dpy, DefaultRootWindow(rcx->dpy), 20, 20,
                             rcx->nWinWidth, rcx->nWinHeight, 0,
                             visualInfo->depth, InputOutput,
                             visualInfo->visual, winmask, &winAttribs);

    XMapWindow(rcx->dpy, rcx->win);

    Window root;
    int x, y;
    unsigned int width, height;
    unsigned int border_width;
    unsigned int depth;
    if (XGetGeometry(rcx->dpy, rcx->win, &root, &x, &y, &width, &height, &border_width, &depth) == False)
    {
        fprintf(stderr, "can't get root window geometry\n");
        exit(EXIT_FAILURE);
    }
    printf("%i %i\n", width, height);

    // Also create a new GL context for rendering
    GLint attribs[] =
        {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 2,
            GLX_CONTEXT_MINOR_VERSION_ARB, 0,
            //GLX_CONTEXT_MINOR_VERSION_ARB, 1,
            0
        };
    rcx->ctx = glXCreateContextAttribsARBFuncPtr(rcx->dpy, fbConfigs[0], 0, True, attribs);
    Bool status = CALL_GL(glXMakeCurrent)(rcx->dpy, rcx->win, rcx->ctx);
    if (!status)
    {
        fprintf(stderr, "Failed creating GL context!\n");
        exit(EXIT_FAILURE);
    }

    const GLubyte *s = CALL_GL(glGetString)(GL_VERSION);
    printf("GL Version = %s\n", s);
}

void SetupGLState(RenderContext *rcx)
{
    float aspectRatio = rcx->nWinHeight ? (float)rcx->nWinWidth / (float)rcx->nWinHeight : 1.0f;
    float fYTop = 0.05f;
    float fYBottom = -fYTop;
    float fXLeft = fYTop * aspectRatio;
    float fXRight = fYBottom * aspectRatio;

    CALL_GL(glViewport)(0, 0, rcx->nWinWidth, rcx->nWinHeight);
    CALL_GL(glScissor)(0, 0, rcx->nWinWidth, rcx->nWinHeight);

    CALL_GL(glClearColor)(0.0f, 1.0f, 1.0f, 1.0f);
    CALL_GL(glClear)(GL_COLOR_BUFFER_BIT);

    // Clear matrix stack
    CALL_GL(glMatrixMode)(GL_PROJECTION);
    CALL_GL(glLoadIdentity)();
    CALL_GL(glMatrixMode)(GL_MODELVIEW);
    CALL_GL(glLoadIdentity)();

    // Set the frustrum
    CALL_GL(glFrustum)(fXLeft, fXRight, fYBottom, fYTop, 0.1f, 100.f);
}

void DrawCircle()
{
    float fx = 0.0;
    float fy = 0.0;

    int nDegrees = 0;

    // Use a triangle fan with 36 tris for the circle
    CALL_GL(glBegin)(GL_TRIANGLE_FAN);

    CALL_GL(glVertex2f)(0.0, 0.0);
    for (nDegrees = 0; nDegrees < 360; nDegrees += 10)
    {
        fx = sin((float)nDegrees * PI / 180);
        fy = cos((float)nDegrees * PI / 180);
        CALL_GL(glVertex2f)(fx, fy);
    }
    CALL_GL(glVertex2f)(0.0f, 1.0f);

    CALL_GL(glEnd)();
}

void Draw(RenderContext *rcx)
{
    float fRightX = 0.0;
    float fRightY = 0.0;
    float fLeftX = 0.0;
    float fLeftY = 0.0;

    int nLtEyePosX = (rcx->nWinWidth) / 2 - (0.3 * ((rcx->nWinWidth) / 2));
    int nLtEyePosY = (rcx->nWinHeight) / 2;
    int nRtEyePosX = (rcx->nWinWidth) / 2 + (0.3 * ((rcx->nWinWidth) / 2));
    int nRtEyePosY = (rcx->nWinHeight) / 2;

    double fLtVecMag = 100;
    double fRtVecMag = 100;

    // Use the eyeball width as the minimum
    double fMinLength = (0.1 * ((rcx->nWinWidth) / 2));

    // Calculate the length of the vector from the eyeball
    // to the mouse pointer
    fLtVecMag = sqrt(pow((double)(rcx->nMousePosX - nLtEyePosX), 2.0) +
                     pow((double)(rcx->nMousePosY - nLtEyePosY), 2.0));

    fRtVecMag = sqrt(pow((double)(rcx->nMousePosX - nRtEyePosX), 2.0) +
                     pow((double)(rcx->nMousePosY - nRtEyePosY), 2.0));

    // Clamp the minimum magnatude
    if (fRtVecMag < fMinLength)
        fRtVecMag = fMinLength;
    if (fLtVecMag < fMinLength)
        fLtVecMag = fMinLength;

    // Normalize the vector components
    fLeftX = (float)(rcx->nMousePosX - nLtEyePosX) / fLtVecMag;
    fLeftY = -1.0 * (float)(rcx->nMousePosY - nLtEyePosY) / fLtVecMag;
    fRightX = (float)(rcx->nMousePosX - nRtEyePosX) / fRtVecMag;
    fRightY = -1.0 * ((float)(rcx->nMousePosY - nRtEyePosY) / fRtVecMag);

    CALL_GL(glClear)(GL_COLOR_BUFFER_BIT);

    // Clear matrix stack
    CALL_GL(glMatrixMode)(GL_PROJECTION);
    CALL_GL(glLoadIdentity)();

    CALL_GL(glMatrixMode)(GL_MODELVIEW);
    CALL_GL(glLoadIdentity)();

    // Draw left eyeball
    CALL_GL(glColor3f)(1.0, 1.0, 1.0);
    CALL_GL(glScalef)(0.20, 0.20, 1.0);
    CALL_GL(glTranslatef)(-1.5, 0.0, 0.0);
    DrawCircle();

    // Draw black
    CALL_GL(glColor3f)(0.0, 0.0, 0.0);
    CALL_GL(glScalef)(0.40, 0.40, 1.0);
    CALL_GL(glTranslatef)(fLeftX, fLeftY, 0.0);
    DrawCircle();

    // Draw right eyeball
    CALL_GL(glColor3f)(1.0, 1.0, 1.0);
    CALL_GL(glLoadIdentity)();
    CALL_GL(glScalef)(0.20, 0.20, 1.0);
    CALL_GL(glTranslatef)(1.5, 0.0, 0.0);
    DrawCircle();

    // Draw black
    CALL_GL(glColor3f)(0.0, 0.0, 0.0);
    CALL_GL(glScalef)(0.40, 0.40, 1.0);
    CALL_GL(glTranslatef)(fRightX, fRightY, 0.0);
    DrawCircle();

    // Clear matrix stack
    CALL_GL(glMatrixMode)(GL_MODELVIEW);
    CALL_GL(glLoadIdentity)();

    // Draw Nose
    CALL_GL(glColor3f)(0.5, 0.0, 0.7);
    CALL_GL(glScalef)(0.20, 0.20, 1.0);
    CALL_GL(glTranslatef)(0.0, -1.5, 0.0);

#if 0
	CALL_GL(glBegin)(GL_TRIANGLES);
	CALL_GL(glVertex2f)(0.0, 1.0);
	CALL_GL(glVertex2f)(-0.5, -1.0);
	CALL_GL(glVertex2f)(0.5, -1.0);
	CALL_GL(glEnd)();
#endif

#if 1
    float verts[3][2] =
        {
            { 0.0, 1.0 },
            { -0.5, -1.0 },
            { 0.5, -1.0 }
        };

    CALL_GL(glVertexPointer)(2, GL_FLOAT, sizeof(float) * 2, verts);
    CALL_GL(glEnableClientState)(GL_VERTEX_ARRAY);

    CALL_GL(glDrawArrays)(GL_TRIANGLES, 0, 3);

    CALL_GL(glDisableClientState)(GL_VERTEX_ARRAY);
    CALL_GL(glVertexPointer)(4, GL_FLOAT, 0, NULL);
#endif

    // Purposely unset the context to better test the glXSwapBuffers() hook.
    // This does not work on Mesa 9 with a v2.1 context!
    //CALL_GL(glXMakeCurrent)(rcx->dpy, None, NULL);

    // Display rendering
    CALL_GL(glXSwapBuffers)(rcx->dpy, rcx->win);

    // Now set the context back.
    //CALL_GL(glXMakeContextCurrent)(rcx->dpy, rcx->win, rcx->win, rcx->ctx);
}

void Cleanup(RenderContext *rcx)
{
    // Unbind the context before deleting
    CALL_GL(glXMakeCurrent)(rcx->dpy, None, NULL);

    CALL_GL(glXDestroyContext)(rcx->dpy, rcx->ctx);
    rcx->ctx = NULL;

    XDestroyWindow(rcx->dpy, rcx->win);
    rcx->win = (Window)NULL;

    XCloseDisplay(rcx->dpy);
    rcx->dpy = 0;
}

int main(int argc, char *argv[])
{
    vogl_core_init();

    if (argc > 1)
    {
        // If we were passed any args, send them to voglcore test function.
        return run_voglcore_tests(argc, argv);
    }

    colorized_console::init();
    colorized_console::set_exception_callback();

    console::set_tool_prefix("(vogltest) ");

    Bool bWinMapped = False;
    RenderContext rcx;

    // Set initial window size
    rcx.nWinWidth = 400;
    rcx.nWinHeight = 200;

    // Set initial mouse position
    rcx.nMousePosX = 0;
    rcx.nMousePosY = 0;

    // Setup X window and GLX context
    CreateWindow(&rcx);
    SetupGLState(&rcx);

    // Draw the first frame before checking for messages
    Draw(&rcx);

    Atom wmDeleteMessage = XInternAtom(rcx.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(rcx.dpy, rcx.win, &wmDeleteMessage, 1);

    for (;;)
    {
        XEvent newEvent;
        XWindowAttributes winData;

        // Watch for new X events
        XNextEvent(rcx.dpy, &newEvent);

        switch (newEvent.type)
        {
            case UnmapNotify:
                bWinMapped = False;
                break;
            case MapNotify:
                bWinMapped = True;
            case ConfigureNotify:

                XGetWindowAttributes(rcx.dpy, rcx.win, &winData);
                rcx.nWinHeight = winData.height;
                rcx.nWinWidth = winData.width;
                SetupGLState(&rcx);
                break;
            case MotionNotify:
                rcx.nMousePosX = newEvent.xmotion.x;
                rcx.nMousePosY = newEvent.xmotion.y;
                Draw(&rcx);
                break;
            //case KeyPress:
            case DestroyNotify:
                Cleanup(&rcx);
                exit(EXIT_SUCCESS);
                break;
            case ClientMessage:
                if (newEvent.xclient.data.l[0] == (int)wmDeleteMessage)
                {
                    goto done;
                }
                break;
        }

        if (bWinMapped)
        {
            Draw(&rcx);
        }
    }

done:
    Cleanup(&rcx);

    colorized_console::deinit();

    return EXIT_SUCCESS;
}
