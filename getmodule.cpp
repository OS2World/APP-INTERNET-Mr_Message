#define INCL_DOSMODULEMGR
#include <os2.h>

HMODULE __export getModuleHandle( void )
{
  HMODULE result;
  DosQueryModuleHandle( "MRMSGGUI.DLL", &result );
  return result;
}

