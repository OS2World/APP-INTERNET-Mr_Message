#ifndef BUDDY_H_INCLUDED
#define BUDDY_H_INCLUDED

#define INCL_WIN
#include <os2.h>
#include "oscarprotocol.h"

#define BUDDY_STATE_ONLINE  1
#define BUDDY_STATE_AWAY    2
#define BUDDY_STATE_ACTIVE  3
#define BUDDY_STATE_OFFLINE 4

class Buddy
{
public:
  UserInformation userData;
  char *alias;
  RECORDCORE *record;
  HWND imChatWin, imChatClientWin;
  unsigned char isGroup, flashState, oldFlashState, animStep, onServer;
  
  Buddy( buddyListEntry *buddyInfo );
  ~Buddy();
};

#endif
