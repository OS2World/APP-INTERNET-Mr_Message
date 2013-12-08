#define INCL_DOS
#define INCL_WIN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <os2.h>

#include "SessionManager.h"
#include "compatibility.h"

int main( int argc, char *argv[] )
{
  SessionManager *theSessionMgr = NULL;
  int i, enableSound;
  
  enableSound = 1;

  for ( i=1; i<argc; ++i )
  {
    #ifdef MRM_DEBUG_PRINTF
    if ( stricmp( argv[i], "-debug" ) == 0 )
    {
      debugOutputStream = fopen( "debug.log", "wa" );
      debugf( "Debug log opened.\n" );
      debugf( "Mr. Message for OS/2 version: %s\n", __DATE__ );
    } else if ( stricmp( argv[i], "-showrawincoming" ) == 0 )
    {
      extern int debugShowRawIncoming;
      debugShowRawIncoming = 1;
    } else
    #endif
    if ( stricmp( argv[i], "-nosound" ) == 0 )
    {
      enableSound = 0;
    } else {
      fprintf( stderr, "Unrecognized command line argument: %s", argv[i] );
      fflush( stderr );
      fprintf( stdout, "Unrecognized command line argument: %s", argv[i] );
      fflush( stdout );
      debugf( "Unrecognized command line argument: %s", argv[i] );
      // Send it to all 3 places in case the user isn't logging one of them
    }
  }
  
  initializeCompatibility( enableSound );
  
  theSessionMgr = new SessionManager();
  delete theSessionMgr;
  
  deinitializeCompatibility();
  
  if ( debugOutputStream )
  {
    debugf( "Closing debug log file and exiting program.\n" );
    fclose( debugOutputStream );
  }
  
  return 0;
}

