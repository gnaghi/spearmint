#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <switch.h>

#include <EGL/egl.h>    // EGL library
#include <EGL/eglext.h> // EGL extensions
#include <glad/glad.h>  // glad library (OpenGL loader)

#include "../renderercommon/tr_common.h"
#include "../sys/sys_local.h"
#include "nx_icon.h"


cvar_t *r_allowSoftwareGL; // Don't abort out if a hardware visual can't be obtained
cvar_t *r_allowResize; // make window resizable
cvar_t *r_centerWindow;
cvar_t *r_sdlDriver;
cvar_t *r_forceWindowIcon32;

int qglMajorVersion, qglMinorVersion;
int qglesMajorVersion, qglesMinorVersion;

// GL_ARB_multisample
void (APIENTRYP qglActiveTextureARB) (GLenum texture);
void (APIENTRYP qglClientActiveTextureARB) (GLenum texture);
void (APIENTRYP qglMultiTexCoord2fARB) (GLenum target, GLfloat s, GLfloat t);

// GL_EXT_compiled_vertex_array
void (APIENTRYP qglLockArraysEXT) (GLint first, GLsizei count);
void (APIENTRYP qglUnlockArraysEXT) (void);

// GL_ARB_texture_compression
void (APIENTRYP qglCompressedTexImage2DARB) (GLenum target, GLint level,
    GLenum internalformat,
    GLsizei width, GLsizei height,
    GLint border, GLsizei imageSize,
    const GLvoid *data);


#define GLE(ret, name, ...) name##proc * qgl##name;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_ES_1_1_PROCS;
QGL_ES_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_occlusion_query_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE




qboolean ( * qwglSwapIntervalEXT)( int interval );
void ( * qglMultiTexCoord2fARB )( GLenum texture, float s, float t );
void ( * qglActiveTextureARB )( GLenum texture );
void ( * qglClientActiveTextureARB )( GLenum texture );


void ( * qglLockArraysEXT)( int, int);
void ( * qglUnlockArraysEXT) ( void );


static EGLDisplay g_EGLDisplay;
static EGLContext g_EGLContext;
static EGLSurface g_EGLWindowSurface;
static EGLConfig    g_EGLConfig;


/*
===============
GLimp_InitExtensions
===============
*/
static void GLimp_InitExtensions( void )
{
  if ( !r_allowExtensions->integer )
  {
    Com_Printf( "* IGNORING OPENGL EXTENSIONS *\n" );
    return;
  }

  Com_Printf( "Initializing OpenGL extensions\n" );

  glConfig.textureCompression = TC_NONE;
  qglCompressedTexImage2DARB = NULL;


}

//Le WindowSize vaut 0, ce qui n'est pas normal.
//Il faut comparer avec la version SDL, voir ce qu'elle fait, et limite, remplacer chaque méhode SDL par la native libNX.

static qboolean nx_GLimp_Init(NWindow* win)
{
  Com_Printf( "Launching nx_GLimp_Init\n" );
  g_EGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!g_EGLDisplay)
  {
    Com_Printf("Could not connect to display! error: %d", eglGetError());
    return qfalse;
  }
  Com_Printf( "eglGetDisplay worked.\n" );

  EGLint majorVersion;
  EGLint minorVersion;

  if (!eglInitialize(g_EGLDisplay, &majorVersion, &minorVersion))
  {
    Com_Printf("eglInitialize() failed\n");
    return qfalse;
  }
  Com_Printf( "eglInitialize worked.\n" );

  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
  {
    Com_Printf("Could not set API! error: %d", eglGetError());
    return qfalse;
  }

  // Get an appropriate EGL framebuffer configuration
  EGLConfig config;
  EGLint numConfigs;


  static const EGLint s_configAttribs[] =
  {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE,     8,
    EGL_GREEN_SIZE,   8,
    EGL_BLUE_SIZE,    8,
    EGL_ALPHA_SIZE,   8,
    EGL_DEPTH_SIZE,   24,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };

  eglChooseConfig(g_EGLDisplay, s_configAttribs, &config, 1, &numConfigs);
  if (numConfigs == 0)
  {
    Com_Printf("No config found! error: %d", eglGetError());
    return qfalse;
  }

  Com_Printf( "eglChooseConfig worked.\n" );


  // Create an EGL rendering context
  static const EGLint contextAttributeList[] =
  {
    EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
    EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
    EGL_CONTEXT_MINOR_VERSION_KHR, 3,
    EGL_NONE
  };

  g_EGLContext = eglCreateContext(g_EGLDisplay, config, EGL_NO_CONTEXT, contextAttributeList);
  if (g_EGLContext == EGL_NO_CONTEXT)
  {
    Com_Printf("eglCreateContext() failed. error: %d", eglGetError());
    return qfalse;
  }

  Com_Printf( "eglCreateContext worked.\n" );

// Create an EGL window surface
  g_EGLWindowSurface = eglCreateWindowSurface(EGL_DEFAULT_DISPLAY, config, win, NULL); //g_EGLDisplay
  if (g_EGLWindowSurface == EGL_NO_SURFACE)
  {
    Com_Printf("Surface creation failed! error: %d", eglGetError());
    return qfalse;
  }

  Com_Printf( "eglCreateWindowSurface worked.\n" );
  eglMakeCurrent(g_EGLDisplay, g_EGLWindowSurface, g_EGLWindowSurface, g_EGLContext);

  {
    EGLint width, height, color, depth, stencil;
    eglQuerySurface(g_EGLDisplay, g_EGLWindowSurface, EGL_WIDTH, &width);
    eglQuerySurface(g_EGLDisplay, g_EGLWindowSurface, EGL_HEIGHT, &height);
    Com_Printf("Window size: %dx%d\n", width, height);
    eglGetConfigAttrib(g_EGLDisplay, g_EGLConfig, EGL_BUFFER_SIZE, &color);
    eglGetConfigAttrib(g_EGLDisplay, g_EGLConfig, EGL_DEPTH_SIZE, &depth);
    eglGetConfigAttrib(g_EGLDisplay, g_EGLConfig, EGL_STENCIL_SIZE, &stencil);
    glConfig.vidWidth = width;
    glConfig.vidHeight = height;
    glConfig.colorBits = color;
    glConfig.depthBits = depth;
    glConfig.stencilBits = stencil;
  }

  Com_Printf( "eglMakeCurrent worked.\n" );
  // set swap interval
  if (eglSwapInterval(g_EGLDisplay, r_swapInterval->integer) == EGL_FALSE)
  {
    Com_Printf("Could not set swap interval\n");
    return qfalse;
  }
  Com_Printf( "Done init for nx.\n" );

  return qtrue;
}


void GLimp_EndFrame( void )
{
  eglSwapBuffers(g_EGLDisplay, g_EGLWindowSurface);
}

void GLimp_Init( qboolean fixedFunction )
{
  Com_Printf("Launching GLimp_Init\n");
  ri.Sys_GLimpInit( );  //Specific init for Switch. So far, it does nothing.

  nx_GLimp_Init( nwindowGetDefault() );

  glConfig.deviceSupportsGamma = qfalse;

  // Load OpenGL routines using glad
  gladLoadGL();
  Com_Printf("Done gladLoadGL\n");

  /*
    strncpy(glConfig.vendor_string, glGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
    strncpy(glConfig.renderer_string, glGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
    strncpy(glConfig.version_string, glGetString(GL_VERSION), sizeof(glConfig.version_string));
    strncpy(glConfig.extensions_string, glGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));
  */
  //Launch Input Init. Maybe, later, send the screen as a parameter.
  Com_Printf("InitIn\n");
  ri.IN_Init( NULL );
}

void GLimp_Shutdown( void )
{
  ri.IN_Shutdown();

  if (g_EGLDisplay)
  {
    eglMakeCurrent(g_EGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_EGLContext)
    {
      eglDestroyContext(g_EGLDisplay, g_EGLContext);
      g_EGLContext = NULL;
    }
    if (g_EGLWindowSurface)
    {
      eglDestroySurface(g_EGLDisplay, g_EGLWindowSurface);
      g_EGLWindowSurface = NULL;
    }
    eglTerminate(g_EGLDisplay);
    g_EGLDisplay = NULL;
  }
}



void    GLimp_EnableLogging( qboolean enable )
{
}

void    GLimp_LogComment( char *comment )
{
}

void GLimp_Minimize( void )
{
}

/*
===============
GLimp_ResizeWindow

Window has been resized, update glconfig
===============
*/
qboolean GLimp_ResizeWindow( int width, int height )
{
  return qtrue;
}
