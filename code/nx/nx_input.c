
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

/*
Switch specific below
*/

static char keysNames[32][32] = {
  "KEY_A", "KEY_B", "KEY_X", "KEY_Y",
  "KEY_LSTICK", "KEY_RSTICK", "KEY_L", "KEY_R",
  "KEY_ZL", "KEY_ZR", "KEY_PLUS", "KEY_MINUS",
  "KEY_DLEFT", "KEY_DUP", "KEY_DRIGHT", "KEY_DDOWN",
  "KEY_LSTICK_LEFT", "KEY_LSTICK_UP", "KEY_LSTICK_RIGHT", "KEY_LSTICK_DOWN",
  "KEY_RSTICK_LEFT", "KEY_RSTICK_UP", "KEY_RSTICK_RIGHT", "KEY_RSTICK_DOWN",
  "KEY_SL_LEFT", "KEY_SR_LEFT", "KEY_SL_RIGHT", "KEY_SR_RIGHT",
  "KEY_TOUCH", "", "", ""
};

typedef struct {
  uint32_t button;
  int key;
} buttonMapping;

//TODO : create a structure with all the informations :
// - Layout
// - Nb of buttons
// - Is there a oystick present

#define MAX_BUTTON_HANDLED_PRO_PAIR     14
static buttonMapping buttonMapHandledProPair[MAX_BUTTON_HANDLED_PRO_PAIR] =
{
  { KEY_MINUS, K_ESCAPE },
  { KEY_PLUS, K_ENTER },
  { KEY_DUP, K_UPARROW },
  { KEY_DRIGHT, K_RIGHTARROW },
  { KEY_DDOWN, K_DOWNARROW },
  { KEY_DLEFT, K_LEFTARROW },
  { KEY_L, K_AUX5 },
  { KEY_R, K_AUX6 },
  { KEY_ZL, K_AUX7 },
  { KEY_ZR, K_AUX8 },
  { KEY_A, K_AUX1 },
  { KEY_B, K_AUX2 },
  { KEY_X, K_AUX3 },
  { KEY_Y, K_AUX4 },
};

//TODO : redo the layout, as in landscape mode, there are no ZL/ZR.
#define MAX_BUTTON_JOYCON_SINGLE     14
static buttonMapping buttonMapJoyconSingle[MAX_BUTTON_JOYCON_SINGLE] =
{
  { KEY_MINUS, K_ESCAPE },
  { KEY_PLUS, K_ESCAPE },
  { KEY_DUP, K_UPARROW },
  { KEY_DRIGHT, K_RIGHTARROW },
  { KEY_DDOWN, K_DOWNARROW },
  { KEY_DLEFT, K_LEFTARROW },
  { KEY_L, K_AUX5 },
  { KEY_R, K_AUX6 },
  { KEY_ZL, K_AUX7 },
  { KEY_ZR, K_AUX8 },
  { KEY_A, K_AUX1 },
  { KEY_B, K_AUX2 },
  { KEY_X, K_AUX3 },
  { KEY_Y, K_AUX4 },
};

//TODO : Do the same with LeftJoycon, RightJoycon, and Pro controller.
//With each #defines

typedef struct
{
  HidControllerID ID;
  HidControllerType type;
  int MaxButtons;
  buttonMapping *ControllerMapping;
  bool LeftJoystick;
  bool RightJoystick;
} ControllerInfo;

#define MAX_CONTROLLERS 10

ControllerInfo ConnectedControllers[MAX_CONTROLLERS];
bool isGamepad = false;

/*
typedef enum {
  CA_UNINITIALIZED,
  CA_DISCONNECTED,  // not talking to a server
  CA_AUTHORIZING,   // not used any more, was checking cd key
  CA_CONNECTING,    // sending request packets to the server
  CA_CHALLENGING,   // sending challenge packets to the server
  CA_CONNECTED,   // netchan_t established, getting gamestate
  CA_LOADING,     // only during cgame initialization, never during main loop
  CA_PRIMED,      // got gamestate, waiting for first frame
  CA_ACTIVE,      // game views should be displayed
  CA_CINEMATIC    // playing a cinematic or a static pic, not connected to a server
} connstate_t;

*/

static void Key_Event(int key, int value, int time)
{
  Com_QueueEvent(time, SE_KEY, key, value, 0, NULL);
}

static void Joy_Event(int key, int value, int time, int playernum)
{
  assert(playernum < CL_MAX_SPLITVIEW);
  Com_QueueEvent(time, SE_JOYSTICK_AXIS + playernum, key, value, 0, NULL);
}

static void Button_Event(int key, int value, int time, int playernum)
{
  assert(playernum < CL_MAX_SPLITVIEW);
  Com_QueueEvent(time, SE_JOYSTICK_BUTTON + playernum, key, value, 0, NULL);
}

static void Hat_Event(int key, int value, int time, int playernum)
{
  assert(playernum < CL_MAX_SPLITVIEW);
  Com_QueueEvent(time, SE_JOYSTICK_HAT + playernum, key, value, 0, NULL);
}

static void Mouse_Event(int x, int y, int time, int playernum)
{
  assert(playernum < CL_MAX_SPLITVIEW);
  Com_QueueEvent(time, SE_MOUSE + playernum, x / 3072, y / 3072, 0, NULL);  //Todo : make 3072 user-configurable
}

//Playernum is useless for now. But later, to support multiple mouse ?
static void Mouse_Button_Event(int key, int value, int time, int playernum)
{
  assert(playernum < CL_MAX_SPLITVIEW);
  Com_QueueEvent(time, SE_KEY, key, value, 0, NULL);  //Todo : make 3072 user-configurable
}

//Everybodyelse is doing so why can't we ?
static void IN_RescaleAnalog( int *x, int *y, float deadZone )
{
  float analogX = (float) * x;
  float analogY = (float) * y;
  float maximum = 180.0f;
  float magnitude = sqrtf( analogX * analogX + analogY * analogY );

  if ( magnitude >= deadZone )
  {
    float scalingFactor = maximum / magnitude * ( magnitude - deadZone ) / ( maximum - deadZone );
    *x = (int)( analogX * scalingFactor );
    *y = (int)( analogY * scalingFactor );
  }
  else
  {
    *x = 0;
    *y = 0;
  }
}

static void IN_GamepadMove( void )
{
  buttonMapping *AnyJoyButtonMap; //Pointer to the good mapping, according to HidControllerType
  int left_x, left_y;
  int right_x, right_y;
  int joynum = 0; //TODO : loop with the value of an array which contains ID's of connected controllers, the first 4.

  JoystickPosition joyleft, joyright;


  //TODO : Check if there is no problem using all the buttons in the menu.
  //TODO : support other layouts.
  short MaxLayoutButton = ConnectedControllers[joynum].MaxButtons;
  AnyJoyButtonMap = ConnectedControllers[joynum].ControllerMapping;

  u64 kDown = hidKeysDown(ConnectedControllers[joynum].ID); //Get all the pressed buttons.
  u64 kUp = hidKeysUp(ConnectedControllers[joynum].ID); //Get all the pressed buttons.
  u64 kHeld = hidKeysHeld(ConnectedControllers[joynum].ID); //Sometimes, works better...

  for (short i = 0; i < MaxLayoutButton; i++)
  {
    if ( ( kDown & AnyJoyButtonMap[i].button ) )
      Key_Event( AnyJoyButtonMap[i].key, true, in_eventTime);
    else if ( ( kUp & AnyJoyButtonMap[i].button ) )
      Key_Event( AnyJoyButtonMap[i].key, false, in_eventTime);
  }

  hidJoystickRead(&joyleft, ConnectedControllers[joynum].ID, JOYSTICK_LEFT);
  hidJoystickRead(&joyright, ConnectedControllers[joynum].ID, JOYSTICK_RIGHT);

  left_x =  joyleft.dx;
  left_y = -joyleft.dy;
  IN_RescaleAnalog( &left_x, &left_y, 25.0f );  //Todo : tweak this ! Change the value if we are in game or in menu ?

  if (clc.state == CA_DISCONNECTED || clc.state == CA_CINEMATIC )
  {
//    Com_Printf( "We are in Game Menu.\n" );

    //Todo : add support for multiple input. For now, we only support handled mode.

    Mouse_Event( left_x, left_y, in_eventTime, joynum);

    u64 kDown = hidKeysDown(ConnectedControllers[joynum].ID); //Get all the pressed buttons.
    u64 kHeld = hidKeysHeld(ConnectedControllers[joynum].ID); //Sometimes, works better...

    if ( (kDown & KEY_B) )
    {
      Mouse_Button_Event(K_MOUSE1, kDown & KEY_B, in_eventTime, joynum);
    }
    if ( (kDown & KEY_A) )
    {
      Mouse_Button_Event(K_MOUSE2, kDown & KEY_A, in_eventTime, joynum);
    }
  }
  else //We are in-game
  {
    //Todo : deal with all the mess of Joystick axes. Switch has 2 axes on each analogue.
    Joy_Event( 0, left_x, in_eventTime, joynum);
    Joy_Event( 1, left_y, in_eventTime, joynum);
  }
}

static void IN_JoyMove( void )
{
  if (isGamepad)
  {
    IN_GamepadMove();
    return; //Todo : later, remove this return if there are controller AND mouse/keyboard (check if it is possible under Horizon)
  }
  //Todo : support keyboard and mouse here.
}


void IN_Init( void *windowData )
{
  int nbcontrollers;  //How many controllers are connected.
  Com_Printf( "IN_Init Launching\n" );
  hidScanInput();

  in_eventTime = Sys_Milliseconds( );


  for (uint8_t i = 0, nbcontrollers = 0; i < MAX_CONTROLLERS; i++)
  {
    if (hidIsControllerConnected(i))
    {
      HidControllerType CurrentType = (hidGetControllerType(i) & 7);
   
      Com_Printf( "Type %d connected.\n", CurrentType);
      switch (CurrentType)
      {
      case TYPE_PROCONTROLLER:
      case TYPE_HANDHELD:
      case TYPE_JOYCON_PAIR:
        ConnectedControllers[nbcontrollers].ControllerMapping = buttonMapHandledProPair;  //We set here the mapping for each controller.
        ConnectedControllers[nbcontrollers].MaxButtons = MAX_BUTTON_HANDLED_PRO_PAIR;
        ConnectedControllers[nbcontrollers].LeftJoystick = true;  //TODO : create the structure for each controller with this already in.
        ConnectedControllers[nbcontrollers].RightJoystick = true;
        Com_Printf( "Joystick-type detected. id = %d\n", i );
        isGamepad = true;
        break;

      case TYPE_JOYCON_LEFT:
      case TYPE_JOYCON_RIGHT:
        ConnectedControllers[nbcontrollers].ControllerMapping = buttonMapJoyconSingle;  //We set here the mapping for each controller.
        ConnectedControllers[nbcontrollers].MaxButtons = MAX_BUTTON_JOYCON_SINGLE;
        ConnectedControllers[nbcontrollers].LeftJoystick = true;
        ConnectedControllers[nbcontrollers].RightJoystick = false;
        Com_Printf( "Joystick-type detected. id = %d\n", i );
        isGamepad = true;
        break;

      default://TODO : Support keyboard and mouse, later...
        isGamepad = false;
        ConnectedControllers[nbcontrollers].LeftJoystick = false;
        ConnectedControllers[nbcontrollers].RightJoystick = false;
        Com_Printf( "Other device type detected = %d. id = %d\n", CurrentType, i );
        break;
      }
      ConnectedControllers[nbcontrollers].ID = i;
      ConnectedControllers[nbcontrollers].type = CurrentType;

      nbcontrollers++;

    }
  } //End loop all 10 max controllers

  //TODO : add only the 4 pirst controllers to in_joystick.

}

void IN_Frame (void)
{
  qboolean loading;
  in_eventTime = Sys_Milliseconds( );

  hidScanInput();
  IN_JoyMove();

  // If not DISCONNECTED (main menu) or ACTIVE (in game), we're loading
  loading = ( clc.state != CA_DISCONNECTED && clc.state != CA_ACTIVE );
}

void IN_Shutdown( void )
{
  Com_Printf( "IN_Shutdown Launching\n" );
}

void IN_Restart( void )
{
  Com_Printf( "IN_Restart Launched\n" );
  //Todo : free all allocated memory.
  IN_Init( NULL );
}