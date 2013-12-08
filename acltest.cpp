#define INCL_WIN
#include <os2.h>
#include "ACLCtl10.h"
#include <stdio.h>
#include <string.h>

MRESULT EXPENTRY clientWinFunc( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  static HRTV viewer = 0;
  
  switch ( msg )
  {
    case WM_CREATE:
      viewer = ACLCreateRichTextView( win, 0, 0, 0, 0, FALSE );
      ACLRTVAddParagraph( viewer, "<b><color red>MartyAmodeo:</b><color black> <leftmargin here>It looks like Aaron's RTV widget will work really well as an IM viewer!<leftmargin 0>" );
      ACLRTVAddParagraph( viewer, "<b><color blue>SomeoneElse:</b><color black> <leftmargin here>I agree whole-heartedly!  \"Quoted\" -- \"<<not a tag>>\"<leftmargin 0>" );
      // WinSetOwner( ACLRTVGetWindow( viewer ), win );
    break;
    case WM_SIZE:
    {
      HWND rtvWin;
      RECTL rectl;
      
      WinQueryWindowRect( win, &rectl );
      
      rtvWin = ACLRTVGetWindow( viewer );
      WinSetWindowPos( rtvWin, HWND_TOP, rectl.xLeft, rectl.yBottom,
       SHORT1FROMMP( mp2 ), SHORT2FROMMP( mp2 ),
       SWP_SIZE | SWP_MOVE | SWP_SHOW );
    }
    break;
  }
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

void main( void )
{
  HAB hab = WinInitialize( 0 );
  HMQ hmq = WinCreateMsgQueue( hab, 0 );
  ULONG frameflgs = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER | 
   FCF_MINMAX | FCF_TASKLIST | FCF_SHELLPOSITION;
  HWND frameWin, clientWin;
  QMSG qmsg;
  ULONG rc;
  
  WinRegisterClass( hab, "ACL Test Client Window", clientWinFunc, 0, 0 );
  
  rc = ACLInitCtl();
  printf( "ACL Init returned: %d\n", rc );
  
  frameWin = WinCreateStdWindow( HWND_DESKTOP, WS_VISIBLE, &frameflgs,
   "ACL Test Client Window", "Test Window", 0, 0, 0, &clientWin );
  
  while ( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
    WinDispatchMsg( hab, &qmsg );
  
  WinDestroyWindow( frameWin );
  WinDestroyMsgQueue( hmq );
  WinTerminate( hab );
}

