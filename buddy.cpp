#define INCL_DOS
#include "buddy.h"
#include "sessionmanager.h"
#include "compatibility.h"
#include <malloc.h>
#include <string.h>

Buddy :: Buddy( buddyListEntry *buddyInfo )
{
  alias = NULL;
  record = NULL;
  buddyInfo->myRecord = NULL;
  imChatWin = 0;
  imChatClientWin = 0;
  isGroup = buddyInfo->numMembers > 0;
  
  if ( buddyInfo->gid || buddyInfo->id )
    onServer = 1;
  else onServer = 0;
  // Server group and user IDs are never 0, so a 0 means we added the
  //  buddy ourselves, not loaded it from the buddy list.
  
  buddyInfo->theBuddyPtr = this;
  
  if ( stricmp( buddyInfo->entryName, "PleaseUpgrade000" ) == 0 )
    return;
  // This is one of AOL's little gotcha's that is placed there by their
  //  current clients.  Nice of them, eh?

  CHECKED_STRDUP( buddyInfo->entryName, userData.screenName );
  // Ensure that this is initialized before any data arrives for the GUI
  
  flashState = BUDDY_STATE_OFFLINE;
  oldFlashState = BUDDY_STATE_OFFLINE;
  animStep = 0;
}

Buddy :: ~Buddy()
{
  if ( alias ) CHECKED_FREE( alias );
  if ( imChatWin )
  {
    WinPostMsg( imChatWin, WM_CLOSE, NULL, NULL );
  }
}

