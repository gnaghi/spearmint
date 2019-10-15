#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <switch.h>

#include <EGL/egl.h>    // EGL library
#include <EGL/eglext.h> // EGL extensions


#include <glad/glad.h>  // glad library (OpenGL loader)
#include "../glad/glad.h"
#include <GL/glext.h> //Needed for OpenGL Extension

#include "../renderercommon/tr_common.h"
#include "../sys/sys_local.h"
#include "nx_icon.h"

#define HANDLED_MODE 1
#define DOCKED_MODE 0

#define DOCKED_WIDTH   1920
#define DOCKED_HEIGHT  1080

#define HANDLED_WIDTH  1280
#define HANDLED_HEIGHT  720

#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

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


static EGLDisplay s_display;
static EGLContext g_EGLContext;
static EGLSurface s_surface;
static EGLConfig  g_EGLConfig;



bool isExtensionSupported(const char *extension)
{
#ifdef GL_NUM_EXTENSIONS
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (int i = 0; i < count; ++i) {
    const char* name = (const char*)glGetStringi(GL_EXTENSIONS, i);
    if (name == NULL)
      continue;
    if (strcmp(extension, name) == 0)
      return true;
  }
  return false;
#else
  GLubyte *where = (GLubyte *)strchr(extension, ' ');
  if (where || *extension == '\0')
    return false;

  const GLubyte *extensions = glGetString(GL_EXTENSIONS);

  const GLubyte *start = extensions;
  for (;;) {
    where = (GLubyte *)strstr((const char *)start, extension);
    if (where == NULL)
      break;

    GLubyte *terminator = where + strlen(extension);
    if (where == start || *(where - 1) == ' ')
      if (*terminator == ' ' || *terminator == '\0')
        return true;

    start = terminator;
  }

  return false;
#endif // GL_NUM_EXTENSIONS
}

void listExtensions(void)
{
#ifdef GL_NUM_EXTENSIONS
  Com_Printf("Supported Extensions : \n");
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (int i = 0; i < count; ++i)
  {
    const char* name = (const char*)glGetStringi(GL_EXTENSIONS, i);
    if (name == NULL)
      continue;
    if (i % 8)
      Com_Printf("\n");
    Com_Printf("%s ", name);
  }
#endif
}


/*
===============
GLimp_GetProcAddresses

Get addresses for OpenGL functions.
===============
*/
static qboolean GLimp_GetProcAddresses( qboolean fixedFunction ) {
  qboolean success = qtrue;
  const char *version;

#define GLE( ret, name, ... ) qgl##name = (name##proc *) eglGetProcAddress("gl" #name); \
  if ( qgl##name == NULL ) { \
    Com_Printf( "ERROR: Missing OpenGL function %s\n", "gl" #name ); \
    success = qfalse; \
  }

  // OpenGL 1.0 and OpenGL ES 1.0
  GLE(const GLubyte *, GetString, GLenum name)

  if ( !qglGetString ) {
    Com_Error( ERR_FATAL, "glGetString is NULL" );
  }

  version = (const char *)qglGetString( GL_VERSION );

  if ( !version ) {
    Com_Error( ERR_FATAL, "GL_VERSION is NULL\n" );
  }

  if ( Q_stricmpn( "OpenGL ES", version, 9 ) == 0 ) {
    char profile[6]; // ES, ES-CM, or ES-CL
    sscanf( version, "OpenGL %5s %d.%d", profile, &qglesMajorVersion, &qglesMinorVersion );
    // common lite profile (no floating point) is not supported
    if ( Q_stricmp( profile, "ES-CL" ) == 0 ) {
      qglesMajorVersion = 0;
      qglesMinorVersion = 0;
    }
  } else {
    sscanf( version, "%d.%d", &qglMajorVersion, &qglMinorVersion );
  }

  if ( fixedFunction ) {
    if ( QGL_VERSION_ATLEAST( 1, 2 ) ) {
      QGL_1_1_PROCS;
      QGL_1_1_FIXED_FUNCTION_PROCS;
      QGL_DESKTOP_1_1_PROCS;
      QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
    } else if ( qglesMajorVersion == 1 && qglesMinorVersion >= 1 ) {
      // OpenGL ES 1.1 (2.0 is not backward compatible)
      QGL_1_1_PROCS;
      QGL_1_1_FIXED_FUNCTION_PROCS;
      QGL_ES_1_1_PROCS;
      QGL_ES_1_1_FIXED_FUNCTION_PROCS;
      // error so this doesn't segfault due to NULL desktop GL functions being used
      Com_Error( ERR_FATAL, "Unsupported OpenGL Version: %s\n", version );
    } else {
      Com_Error( ERR_FATAL, "Unsupported OpenGL Version (%s), OpenGL 1.2 is required\n", version );
    }
  } else {
    if ( QGL_VERSION_ATLEAST( 2, 0 ) ) {
      QGL_1_1_PROCS;
      QGL_DESKTOP_1_1_PROCS;
      QGL_1_3_PROCS;
      QGL_1_5_PROCS;
      QGL_2_0_PROCS;
    } else if ( QGLES_VERSION_ATLEAST( 2, 0 ) ) {
      QGL_1_1_PROCS;
      QGL_ES_1_1_PROCS;
      QGL_1_3_PROCS;
      QGL_1_5_PROCS;
      QGL_2_0_PROCS;
      // error so this doesn't segfault due to NULL desktop GL functions being used
      Com_Error( ERR_FATAL, "Unsupported OpenGL Version: %s\n", version );
    } else {
      Com_Error( ERR_FATAL, "Unsupported OpenGL Version (%s), OpenGL 2.0 is required\n", version );
    }
  }

  if ( QGL_VERSION_ATLEAST( 3, 0 ) || QGLES_VERSION_ATLEAST( 3, 0 ) ) {
    QGL_3_0_PROCS;
  }

#undef GLE

  return success;
}


static void setMesaConfig()
{
  // Uncomment below to disable error checking and save CPU time (useful for production):
  //setenv("MESA_NO_ERROR", "1", 1);

  // Uncomment below to enable Mesa logging:
  setenv("EGL_LOG_LEVEL", "debug", 1);
  setenv("MESA_VERBOSE", "all", 1);
  setenv("NOUVEAU_MESA_DEBUG", "1", 1);

  // Uncomment below to enable shader debugging in Nouveau:
  //setenv("NV50_PROG_OPTIMIZE", "0", 1);
  //setenv("NV50_PROG_DEBUG", "1", 1);
  //setenv("NV50_PROG_CHIPSET", "0x120", 1);
}

/*
===============
GLimp_InitExtensions
===============
*/
static void GLimp_InitExtensions( void )
{
//  listExtensions();
  if ( !r_allowExtensions->integer )
  {
    Com_Printf( "* IGNORING OPENGL EXTENSIONS *\n" );
    return;
  }

  Com_Printf( "Initializing OpenGLES extensions\n" );

  qglCompressedTexImage2DARB =  (PFNGLCOMPRESSEDTEXIMAGE2DARBPROC) eglGetProcAddress("glCompressedTexImage2DARB");
  glConfig.textureCompression = TC_S3TC;    //Test TC_S3TC
  Com_Printf( "...using GL_EXT_texture_compression_s3tc\n" );

  glConfig.textureEnvAddAvailable = true;      //false foire aussi
  Com_Printf( "...using GL_EXT_texture_env_add\n" );

  glConfig.textureFilterAnisotropic = qfalse;

  qglMultiTexCoord2fARB = (PFNGLMULTITEXCOORD2FARBPROC) eglGetProcAddress( "glMultiTexCoord2fARB" );
  qglActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC) eglGetProcAddress( "glActiveTextureARB" );
  qglClientActiveTextureARB = (PFNGLCLIENTACTIVETEXTUREARBPROC) eglGetProcAddress( "glClientActiveTextureARB" );

  GLint glint = 0;

  qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glint );
  glConfig.numTextureUnits = (int) glint;
  Com_Printf( "...using GL_ARB_multitexture (max: %i)\n", glConfig.numTextureUnits );

  qglLockArraysEXT = ( void ( APIENTRY * )( GLint, GLint ) ) eglGetProcAddress( "glLockArraysEXT" );
  qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) eglGetProcAddress( "glUnlockArraysEXT" );
  Com_Printf("...using GL_EXT_compiled_vertex_array\n" );

  qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint *)&glConfig.maxAnisotropy );
  Com_Printf("...using GL_EXT_texture_filter_anisotropic (max: %i)\n", glConfig.maxAnisotropy );
  glConfig.textureFilterAnisotropic = qtrue;

  glConfig.deviceSupportsGamma = qfalse;
}


//Detect and set the window size
static qboolean nx_GLimp_SetMode(NWindow* win)
{

  switch (appletGetOperationMode())
  {
  default:
  case AppletOperationMode_Handheld:
    glConfig.displayWidth = HANDLED_WIDTH;
    glConfig.displayHeight = HANDLED_HEIGHT;
    break;
  case AppletOperationMode_Docked:
    glConfig.displayWidth = DOCKED_WIDTH;
    glConfig.displayHeight = DOCKED_HEIGHT;
    break;
  }

  glConfig.displayAspect = (float)glConfig.displayWidth / (float)glConfig.displayHeight;
  Com_Printf("Display aspect: %.3f\n", glConfig.displayAspect );

  Result rsc = nwindowSetCrop(win, 0, 0, glConfig.displayWidth, glConfig.displayHeight);
  if (rsc)
  {
    Com_Printf("Error nwindowSetCrop : %.d\n", rsc );
    return qfalse;
  }

  glConfig.vidWidth = glConfig.displayWidth;
  glConfig.vidHeight = glConfig.displayHeight;
  glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;

  return qtrue;
}

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

static bool nxinitEgl(NWindow* win)
{
  // Connect to the EGL default display
  s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!s_display)
  {
    Com_Printf("Could not connect to display! error: %d", eglGetError());
    return false;
  }

  // Initialize the EGL display connection
  eglInitialize(s_display, NULL, NULL);

  // Select OpenGL (Core) as the desired graphics API
  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
  {
    Com_Printf("Could not set API! error: %d", eglGetError());
    eglTerminate(s_display);
    s_display = NULL;
    return false;
  }

  // Get an appropriate EGL framebuffer configuration
  EGLConfig config;
  EGLint numConfigs;
  static const EGLint framebufferAttributeList[] =
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
  eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs);
  if (numConfigs == 0)
  {
    Com_Printf("No config found! error: %d", eglGetError());
    eglTerminate(s_display);
    s_display = NULL;
    return false;
  }

  // Create an EGL window surface
  s_surface = eglCreateWindowSurface(s_display, config, win, NULL);
  if (!s_surface)
  {
    Com_Printf("Surface creation failed! error: %d", eglGetError());
    eglTerminate(s_display);
    s_display = NULL;
    return false;
  }

  // Create an EGL rendering context
  static const EGLint contextAttributeList[] =
  {
    EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
    EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
    EGL_CONTEXT_MINOR_VERSION_KHR, 3,
    EGL_NONE
  };
  s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
  if (!s_context)
  {
    Com_Printf("Context creation failed! error: %d", eglGetError());
    eglDestroySurface(s_display, s_surface);
    s_surface = NULL;
    eglTerminate(s_display);
    s_display = NULL;
    return false;
  }

  // Connect the context to the surface
  eglMakeCurrent(s_display, s_surface, s_surface, s_context);

  if (eglSwapInterval(s_display, r_swapInterval->integer) == EGL_FALSE)
  {
    Com_Printf("Could not set swap interval\n");
    return qfalse;
  }

//Todo : Fetch these values automatically.
  glConfig.colorBits = 24;
  glConfig.depthBits = 24;
  glConfig.stencilBits = 8;

  glConfig.displayFrequency = 60; // ri.Cvar_VariableIntegerValue( "r_displayRefresh" );
  glConfig.stereoEnabled = qfalse;
  glConfig.isFullscreen = qfalse; 

  return true;
}


void GLimp_EndFrame( void )
{
  if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
  {
    eglSwapBuffers(s_display, s_surface);
  }
  //Todo : check docked or handled mode and change resolution.
}

void GLimp_Init( qboolean fixedFunction )
{
  Com_Printf("Launching GLimp_Init. fixedFunction : %d\n", fixedFunction);
  ri.Sys_GLimpInit( );  //Specific init for Switch. So far, it does nothing.

//Setting various cvars
  ri.Cvar_Set( "r_mode", "-1" );

  // Set mesa configuration (useful for debugging)
  setMesaConfig();

  NWindow* win = nwindowGetDefault();
  nwindowSetDimensions(win, HANDLED_WIDTH, HANDLED_HEIGHT); //Todo : Check dynamically the resolution
  nx_GLimp_SetMode(win);  //Todo : put everything above in this function.

  if (!nxinitEgl(win))
  {
    Com_Printf("Error initEgl\n");
    return;
  }

  // Load OpenGL routines using glad
  gladLoadGL();
  GLimp_GetProcAddresses(fixedFunction);

  // initialize extensions
  GLimp_InitExtensions( );

  Q_strncpyz( glConfig.vendor_string, (char *) glGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
  Q_strncpyz( glConfig.renderer_string, (char *) glGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
  if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
    glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
  Q_strncpyz( glConfig.version_string, (char *) glGetString (GL_VERSION), sizeof( glConfig.version_string ) );

  Com_Printf("vendor_string : %s, renderer_string : %s, version_string . %s\n", glConfig.vendor_string, glConfig.renderer_string, glConfig.version_string);
  ri.IN_Init( NULL );
}

void GLimp_Shutdown( void )
{
  ri.IN_Shutdown();

  if (s_display)
  {
    eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_EGLContext)
    {
      eglDestroyContext(s_display, g_EGLContext);
      g_EGLContext = NULL;
    }
    if (s_surface)
    {
      eglDestroySurface(s_display, s_surface);
      s_surface = NULL;
    }
    eglTerminate(s_display);
    s_display = NULL;
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
