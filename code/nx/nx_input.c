
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <switch.h>

#include "../client/client.h"
#include "../sys/sys_local.h"

static cvar_t *in_keyboardDebug     = NULL;
static char keyboardShiftMap[MAX_KEYS];

//static SDL_Joystick *stick[CL_MAX_SPLITVIEW] = {NULL};

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;

static cvar_t *in_mouse             = NULL;
static cvar_t *in_nograb;

static cvar_t *in_joystick[CL_MAX_SPLITVIEW]      = {NULL};
static cvar_t *in_joystickThreshold[CL_MAX_SPLITVIEW] = {NULL};
static cvar_t *in_joystickNo[CL_MAX_SPLITVIEW]      = {NULL};

static int in_eventTime = 0;

#define CTRL(a) ((a)-'a'+1)

void Key_Event(int key, int value, int time) {
  Com_QueueEvent(time, SE_KEY, key, value, 0, NULL);
}

void IN_Init( void ) {

  Com_DPrintf( "IN_Init Launching\n" );
}

void IN_Frame (void) {
}

void IN_Shutdown( void ) {
  Com_DPrintf( "IN_Shutdown Launching\n" );
}

void IN_Restart( void ) {
 Com_DPrintf( "IN_Restart Launched\n" ); 
}