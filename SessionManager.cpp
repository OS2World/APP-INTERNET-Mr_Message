#define INCL_WIN
#define INCL_GPI
#define INCL_DOS
#define INCL_DOSPROCESS
#define INCL_DOSERRORS

#include <os2.h>
#include <stdio.h>
#include <malloc.h>
#include <process.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "SessionManager.h"
#include "Resource.h"
#include "aclctl10.h"
#include "compatibility.h"

static const char *localBuddyGroupName = "Buddies (stored locally)";
static const char *othersGroupName     = "Others (not in list)";

#define IM_IDLE_TIMEOUT      300
// Timeout before reporting idle status to AOL server

#define BUBBLE_POPUP_TIMEOUT 500
// Time of mouse motionlessness before popping up bubble info/help
// Hard-coded for now

#define TYPING_NOTIFICATION_TIMER 1000
// Minimum time between sending typing notifications

#define SESSION_MGR_WIDTH  640
#define SESSION_MGR_HEIGHT 480
#define BUDDY_LIST_WIDTH   320
#define BUDDY_LIST_HEIGHT  480
#define IM_WINDOW_WIDTH    320
#define IM_WINDOW_HEIGHT   320
// Hard-coded defaults for now

#define SPLASH_WIDTH 396
#define SPLASH_HEIGHT 300
// Change if the bitmap changes

MRESULT EXPENTRY pmPresParamNotifier( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 );
MRESULT EXPENTRY pmPresParamNotifier2( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 );
void renameSessionData( HINI iniFile, char *oldName, char *newName );

const char *UI_EventTypeStrings[] =
{
  "Program Startup",
  "Session Startup",
  "Error Message",
  "First IM with Buddy",
  "IM Received from Buddy",
  "IM Sent to Buddy",
  "Buddy Goes Online",
  "Buddy Goes Offline",
  "Session End",
  "Post-processing for Received Message",
  "Pre-processing for Sent Message"
};

typedef struct
{
  HWND progressDlg;
  sessionThreadInfo **info;
  int myEntry;
} sessionThreadInitData;

void sessionThread( sessionThreadInitData *init )
{
  // Init is allocated outside of this function but is expected to be
  //  freed here when we're done with it.
  
  sessionThreadInfo **info;
  int myEntry;

  debugf( "Session thread started.\n" );
  
  info = init->info;
  myEntry = init->myEntry;

  new OscarSession( (*info)[myEntry].userName, (*info)[myEntry].password,
   (*info)[myEntry].server, (*info)[myEntry].port, (*info)[myEntry].managerWin,
   (*info)[myEntry].record, info, myEntry, (*info)[myEntry].settings,
   init->progressDlg );
  // Doing this in a separate thread allows us to service the UI separately
  //  from communicating with the AOL servers.  Definitely a good thing if
  //  the AOL servers are slow or non-responsive.

  (*info)[myEntry].theSession->serviceSessionUntilClosed();
  
  delete (*info)[myEntry].theSession;
  
  debugf( "Session object was deleted.\n" );
  
  (*info)[myEntry].theSession = NULL;
  (*info)[myEntry].threadID = 0;
  
  CHECKED_FREE( init );
  
  debugf( "Session thread exiting.\n" );
}

static void querySessionSettings( HINI iniFile,
 char *appName, sessionThreadSettings *storeHere )
{
  char buffer[40];
  int i;
  ULONG len;
  unsigned char oldInherit = storeHere->inheritSettings;
  
  len = 1;
  PrfQueryProfileData( iniFile, appName, "Inherit Settings",
   &(storeHere->inheritSettings), &len );
  
  if ( storeHere->inheritSettings && strcmp( appName, "Global Settings" ) )
    return;
  // Don't bother listening to the inherit flag for global settings
  
  storeHere->inheritSettings = 0;
  
  len = sizeof( unsigned long );
  PrfQueryProfileData( iniFile, appName, "Session Flags",
   &(storeHere->sessionFlags), &len );
  
  PrfQueryProfileSize( iniFile, appName, "Profile", &len );
  if ( storeHere->profile && !oldInherit )
  {
    CHECKED_FREE( storeHere->profile );
    storeHere->profile = NULL;
  }
  if ( len )
  {
    CHECKED_MALLOC( len + 1, storeHere->profile );
    PrfQueryProfileData( iniFile, appName, "Profile", storeHere->profile,
     &len );
    storeHere->profile[len] = 0;
  } else {
    storeHere->profile = NULL;
  }
  
  PrfQueryProfileSize( iniFile, appName, "Away Message", &len );
  if ( storeHere->awayMessage && !oldInherit )
  {
    CHECKED_FREE( storeHere->awayMessage );
    storeHere->awayMessage = NULL;
  }
  if ( len )
  {
    CHECKED_MALLOC( len + 1, storeHere->awayMessage );
    PrfQueryProfileData( iniFile, appName, "Away Message",
     storeHere->awayMessage, &len );
    storeHere->awayMessage[len] = 0;
  } else {
    storeHere->awayMessage = NULL;
  }
  
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    sprintf( buffer, "Event %d Flags", i );
    len = 4;
    PrfQueryProfileData( iniFile, appName, (char *)buffer,
     &(storeHere->settingFlags[i]), &len );
    
    sprintf( buffer, "Event %d Sound", i );
    if ( PrfQueryProfileSize( iniFile, appName, (char *)buffer, &len ) )
    {
      if ( storeHere->sounds[i] && !oldInherit )
      {
        CHECKED_FREE( storeHere->sounds[i] );
        storeHere->sounds[i] = NULL;
      }
      CHECKED_MALLOC( len + 1, storeHere->sounds[i] );
      PrfQueryProfileData( iniFile, appName, (char *)buffer,
       storeHere->sounds[i], &len );
      storeHere->sounds[i][len] = 0;
    }
    sprintf( buffer, "Event %d REXX Script", i );
    if ( PrfQueryProfileSize( iniFile, appName, (char *)buffer, &len ) )
    {
      if ( storeHere->rexxScripts[i] && !oldInherit)
      {
        CHECKED_FREE( storeHere->rexxScripts[i] );
        storeHere->rexxScripts[i] = NULL;
      }
      CHECKED_MALLOC( len + 1, storeHere->rexxScripts[i] );
      PrfQueryProfileData( iniFile, appName, (char *)buffer,
       storeHere->rexxScripts[i], &len );
      storeHere->rexxScripts[i][len] = 0;
    }
    sprintf( buffer, "Event %d Shell Script", i );
    if ( PrfQueryProfileSize( iniFile, appName, (char *)buffer, &len ) )
    {
      if ( storeHere->shellCmds[i] && !oldInherit )
      {
        CHECKED_FREE( storeHere->shellCmds[i] );
        storeHere->shellCmds[i] = NULL;
      }
      CHECKED_MALLOC( len + 1, storeHere->shellCmds[i] );
      PrfQueryProfileData( iniFile, appName, (char *)buffer,
       storeHere->shellCmds[i], &len );
      storeHere->shellCmds[i][len] = 0;
    }
  }
}

static void saveSessionSettings( HINI iniFile,
 char *appName, sessionThreadSettings *storeThis )
{
  char buffer[40];
  int i;
  
  PrfWriteProfileData( iniFile, appName, "Inherit Settings",
   &(storeThis->inheritSettings), 1 );
  
  if ( storeThis->inheritSettings && strcmp( appName, "Global Settings" ) )
    return;
  // Always save global settings regardless of the inherit flag

  PrfWriteProfileData( iniFile, appName, "Session Flags",
   &(storeThis->sessionFlags), sizeof( unsigned long ) );
  
  if ( storeThis->profile )
    PrfWriteProfileData( iniFile, appName, "Profile", storeThis->profile,
     strlen( storeThis->profile ) );
  else PrfWriteProfileData( iniFile, appName, "Profile", NULL, 0 );
  
  if ( storeThis->awayMessage )
    PrfWriteProfileData( iniFile, appName, "Away Message",
     storeThis->awayMessage, strlen( storeThis->awayMessage ) );
  else PrfWriteProfileData( iniFile, appName, "Away Message", NULL, 0 );
  
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    sprintf( buffer, "Event %d Flags", i );
    PrfWriteProfileData( iniFile, appName, (char *)buffer,
     &(storeThis->settingFlags[i]), sizeof( unsigned long ) );
    
    sprintf( buffer, "Event %d Sound", i );
    if ( storeThis->sounds[i] )
      PrfWriteProfileData( iniFile, appName, (char *)buffer,
       storeThis->sounds[i], strlen( storeThis->sounds[i] ) );
    else PrfWriteProfileData( iniFile, appName, (char *)buffer, NULL, 0 );
    
    sprintf( buffer, "Event %d REXX Script", i );
    if ( storeThis->rexxScripts[i] )
      PrfWriteProfileData( iniFile, appName, (char *)buffer,
       storeThis->rexxScripts[i], strlen( storeThis->rexxScripts[i] ) );
    else PrfWriteProfileData( iniFile, appName, (char *)buffer, NULL, 0 );
    
    sprintf( buffer, "Event %d Shell Script", i );
    if ( storeThis->shellCmds[i] )
      PrfWriteProfileData( iniFile, appName, (char *)buffer,
       storeThis->shellCmds[i], strlen( storeThis->shellCmds[i] ) );
    else PrfWriteProfileData( iniFile, appName, (char *)buffer, NULL, 0 );
  }
}

void destroySessionSettings( sessionThreadSettings *freeThis )
{
  int i;
  
  if ( !freeThis )
  {
    debugf( "Attempted to free a non-allocated settings block.\n" );
    return;
  }
  
  if ( freeThis->inheritSettings )
    return;
  
  if ( freeThis->profile )
  {
    CHECKED_FREE( freeThis->profile );
    freeThis->profile = NULL;
  }
  
  if ( freeThis->awayMessage )
  {
    CHECKED_FREE( freeThis->awayMessage );
    freeThis->awayMessage = NULL;
  }
  
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    if ( freeThis->sounds[i] )
    {
      CHECKED_FREE( freeThis->sounds[i] );
      freeThis->sounds[i] = NULL;
    }
    if ( freeThis->rexxScripts[i] )
    {
      CHECKED_FREE( freeThis->rexxScripts[i] );
      freeThis->rexxScripts[i] = NULL;
    }
    if ( freeThis->rexxScripts[i] )
    {
      CHECKED_FREE( freeThis->shellCmds[i] );
      freeThis->shellCmds[i] = NULL;
    }
  }
  
  CHECKED_FREE( freeThis );
}

void saveLocalBuddyList( OscarSession *theSession, HINI iniFile )
{
  char *appName, *iniData;
  Buddy **buddyList;
  int numBuddies, i, len;
  
  CHECKED_MALLOC( 14 + strlen( theSession->getUserName() ), appName );
  sprintf( appName, "Buddy List [%s]", theSession->getUserName() );
  
  DosRequestMutexSem( theSession->buddyListAccessMux, SEM_INDEFINITE_WAIT );
  
  buddyList = theSession->getBuddyList( &numBuddies );
  len = 0;
  for ( i=0; i<numBuddies; ++i )
  {
    if ( !buddyList[i] || buddyList[i]->onServer ||
          buddyList[i]->isGroup ) continue;
    len += strlen( buddyList[i]->userData.screenName ) + 1;
  }
  len++;
  
  CHECKED_MALLOC( len, iniData );
  
  len = 0;
  for ( i=0; i<numBuddies; ++i )
  {
    if ( !buddyList[i] || buddyList[i]->onServer ||
          buddyList[i]->isGroup ) continue;
    strcpy( iniData + len, buddyList[i]->userData.screenName );
    len += strlen( buddyList[i]->userData.screenName ) + 1;
    iniData[ len - 1 ] = 0; // NULL separator
  }
  iniData[ len ] = 0;
  // Double NULL at the end
  
  DosReleaseMutexSem( theSession->buddyListAccessMux );
  
  PrfWriteProfileData( iniFile, appName, "Local buddy list additions",
   iniData, len + 1 );
          
  CHECKED_FREE( iniData );
  CHECKED_FREE( appName );
}

MRESULT EXPENTRY mleEnterPerLine( HWND myWin, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  HWND win = WinQueryWindow( myWin, QW_PARENT );
  MRESULT EXPENTRY (*oldProc) ( HWND, ULONG, MPARAM, MPARAM );
  
  switch( msg )
  {
    case WM_CHAR:
    {
      if ( !(SHORT1FROMMP( mp1 ) & KC_KEYUP) &&
            ((SHORT1FROMMP( mp1 ) & KC_VIRTUALKEY) &&
             (SHORT2FROMMP( mp2 ) == VK_NEWLINE ||
              SHORT2FROMMP( mp2 ) == VK_ENTER)) )
      {
        // Pressed ENTER.  If there is something to send, simulate pressing
        //  the SEND button.
        
        ULONG textLen = LONGFROMMR( WinSendMsg( myWin, MLM_QUERYTEXTLENGTH,
         NULL, NULL ) );
        
        if ( textLen )
        {
          WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_SendMessage ), NULL );
        } else {
          WinAlarm( HWND_DESKTOP, WA_WARNING );
        }
        return 0;
      }
    }
    break;
  }
  
  oldProc = (MRESULT EXPENTRY (*) ( HWND, ULONG, MPARAM, MPARAM ))
   WinQueryWindowPtr( win, 12 );
  
  return (*oldProc) ( myWin, msg, mp1, mp2 );
}

typedef struct
{
  const char *userName;
  char *messageType;
  HINI iniFile;
  char **profile;
} messageEditInit;

MRESULT EXPENTRY pmPercentProc( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  ULONG oldval, newval, minval, maxval;
  ULONG oldcol, newcol, t1, t2;
  RECTL rect, rect2, rect3;
  HPS hps;
  switch ( msg ) {
    case WM_CREATE:
      WinSetWindowULong( win, 0, 1 ); // Prevent divide by zero
      WinSetWindowULong( win, 4, 0 );
      WinSetWindowULong( win, 8, 0 );
    break;
    case WM_PAINT:
      hps = WinBeginPaint( win, 0, &rect );
      newval = WinQueryWindowULong( win, 8 );
      maxval = WinQueryWindowULong( win, 0 );
      minval = WinQueryWindowULong( win, 4 );
      
      if ( maxval <= minval )
        maxval = 1;  minval = 0;
      
      WinQueryWindowRect( win, &rect2 );
      if ( newval*100/(maxval-minval) < 25 ) newcol = CLR_DARKRED; else
      if ( newval*100/(maxval-minval) < 50 ) newcol = CLR_RED; else
      if ( newval*100/(maxval-minval) < 75 ) newcol = CLR_YELLOW; else
        newcol = CLR_GREEN;

      newval = newval * (rect2.xRight - rect2.xLeft) / (maxval-minval);
      // Now in coordinates

      if ( rect.xLeft < newval )
      {
        // Some overlap on active percent.  Draw part of the bar.
        rect3.xLeft = rect.xLeft;  rect3.xRight = newval;
        rect3.yBottom = rect.yBottom;  rect3.yTop = rect.yTop;
        WinFillRect( hps, &rect3, newcol );
      }

      if ( rect.xRight > newval )
      {
        // Need to paint some blank space
        rect3.xLeft = newval+1; rect3.xRight = rect.xRight;
        rect3.yBottom = rect.yBottom;  rect3.yTop = rect.yTop;
        WinFillRect( hps, &rect3, CLR_NEUTRAL );
      }
    break;
    case WM_SET_MAXMIN_VAL:
      WinSetWindowULong( win, 0, LONGFROMMP(mp1) );
      WinSetWindowULong( win, 4, LONGFROMMP(mp2) );
    break;
    case WM_SET_CURRENT_VAL:
      WinSetWindowULong( win, 8, LONGFROMMP(mp1) );
      WinInvalidateRect( win, NULL, FALSE );
    break;
    case WM_ADD_TO_CURRENT_VAL:
      oldval = WinQueryWindowULong( win, 8 );
      newval = oldval+LONGFROMMP( mp1 );
      WinSetWindowULong( win, 8, newval );
      maxval = WinQueryWindowULong( win, 0 );
      minval = WinQueryWindowULong( win, 4 );
      
      if ( maxval <= minval )
        maxval = 1;  minval = 0;
      
      if ( oldval*100/(maxval-minval) < 25 ) oldcol = CLR_DARKRED; else
      if ( oldval*100/(maxval-minval) < 50 ) oldcol = CLR_RED; else
      if ( oldval*100/(maxval-minval) < 75 ) oldcol = CLR_YELLOW; else
        oldcol = CLR_GREEN;
      if ( newval*100/(maxval-minval) < 25 ) newcol = CLR_DARKRED; else
      if ( newval*100/(maxval-minval) < 50 ) newcol = CLR_RED; else
      if ( newval*100/(maxval-minval) < 75 ) newcol = CLR_YELLOW; else
        newcol = CLR_GREEN;
      if ( oldcol != newcol )
      {
        // Redraw the whole thing
        WinInvalidateRect( win, NULL, FALSE );
      } else {
        // Just add the new part
        WinQueryWindowRect( win, &rect );
        t1 = rect.xLeft; t2 = rect.xRight;
        rect.xLeft += (t2-t1)*oldval/(maxval-minval);
        rect.xRight = t1+((t2-t1)*newval/(maxval-minval));
        WinInvalidateRect( win, &rect, FALSE );
      }
    break;
  }
  
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmProgressDialog( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
    {
      RECTL desktop, dlgSize;
      SWP swp;
     
      WinQueryWindowRect( HWND_DESKTOP, &desktop );
      WinQueryWindowRect( win, &dlgSize );
      
      swp.x = desktop.xLeft + ((desktop.xRight - desktop.xLeft) / 2) -
       ((dlgSize.xRight - dlgSize.xLeft) / 2);
      swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom) / 2) -
       ((dlgSize.yTop - dlgSize.yBottom) / 2);
      swp.cx = dlgSize.xRight - dlgSize.xLeft;
      swp.cy = dlgSize.yTop - dlgSize.yBottom;
      // Establish defaults
      
      WinSetWindowPos( win, 0, swp.x, swp.y, swp.cx, swp.cy,
       SWP_MOVE | SWP_SHOW );
       
      WinSendDlgItemMsg( win, MRM_SL_ProgressPercent, WM_SET_MAXMIN_VAL,
       MPFROMLONG( 20 ), MPFROMLONG( 0 ) );
    }
    break;
    case WM_CLOSE:
      // This window was fired off and forgotten, so clean it up here.
      WinDestroyWindow( win );
    break;
    case WM_PROGRESSREPORT:
    {
      USHORT spot, total;
      char *messageText;
      
      spot = SHORT1FROMMP( mp1 );
      total = SHORT2FROMMP( mp1 );
      messageText = (char *) PVOIDFROMMP( mp2 );
      
      if ( messageText )
        WinSetDlgItemText( win, MRM_ST_LoginStep, messageText );
      
      WinSendDlgItemMsg( win, MRM_SL_ProgressPercent, WM_ADD_TO_CURRENT_VAL,
       MPFROMLONG( 1 ), NULL );
    }
    break;
  }
  
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmVolumeDialog( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
      WinSendDlgItemMsg( win, MRM_CS_VolumeKnob, CSM_SETRANGE,
       MPFROMSHORT( 0 ), MPFROMSHORT( 100 ) );
      WinSendDlgItemMsg( win, MRM_CS_VolumeKnob, CSM_SETVALUE,
       MPFROMSHORT( (USHORT) getAudioVolume() ), 0 );
    break;
    case WM_CONTROL:
      switch ( SHORT2FROMMP( mp1 ) )
      {
        case CSN_TRACKING:
        case CSN_CHANGED:
          setAudioVolume( LONGFROMMP( mp2 ) );
        break;
      }
    break;
  }
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmMessageEditor( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
    {
      messageEditInit *initData = (messageEditInit *) PVOIDFROMMP( mp2 );
      char *appName, *keyName, *profile;
      ULONG len;
      unsigned short numProfs, i;
      short selectedItem, idx, selectMe;
      RECTL desktop, dlgSize;
      SWP swp;
      
      WinQueryWindowRect( HWND_DESKTOP, &desktop );
      WinQueryWindowRect( win, &dlgSize );
      
      swp.x = desktop.xLeft + ((desktop.xRight - desktop.xLeft) / 2) -
       ((dlgSize.xRight - dlgSize.xLeft) / 2);
      swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom) / 2) -
       ((dlgSize.yTop - dlgSize.yBottom) / 2);
      swp.cx = dlgSize.xRight - dlgSize.xLeft;
      swp.cy = dlgSize.yTop - dlgSize.yBottom;
      // Establish defaults
      
      len = sizeof( SWP );
      PrfQueryProfileData( initData->iniFile, "Profile/Away Dialog",
       "Window position data", &swp, &len );
      // Read INI settings if there are any
      
      WinSetWindowPos( win, 0, swp.x, swp.y, swp.cx, swp.cy,
       SWP_MOVE | SWP_SHOW );
      
      CHECKED_MALLOC( strlen( initData->userName ) + 10, appName );
      sprintf( appName, "%s Settings", initData->userName );
      
      CHECKED_MALLOC( strlen( initData->messageType ) + 12, keyName );
      sprintf( keyName, "Number of %ss", initData->messageType );
      
      numProfs = 0;
      len = sizeof( unsigned short );
      PrfQueryProfileData( initData->iniFile, appName, keyName,
       &numProfs, &len );
      
      sprintf( keyName, "Current %s", initData->messageType );
      
      len = sizeof( short );
      selectedItem = -1;
      PrfQueryProfileData( initData->iniFile, appName, keyName,
       &selectedItem, &len );
      
      if ( numProfs == 0 && *initData->profile )
      {
        // Migration of old INI data... profile was set, but no profile
        //  list exists.  Add the profile to the list.
        profile = stripTags( *initData->profile );
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs,
         LM_INSERTITEM, MPFROMSHORT( LIT_END ), MPFROMP( profile ) ) );
        WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_SELECTITEM,
         MPFROMSHORT( idx ), MPFROMSHORT( TRUE ) );
        CHECKED_FREE( profile );
      }
      
      selectMe = -1;
      for ( i=1; i<numProfs + 1; ++i )
      {
        sprintf( keyName, "%s %d", initData->messageType, i );
        // keyName has room for 11 additional digits (more than enough)
        
        PrfQueryProfileSize( initData->iniFile, appName, keyName, &len );
        len++;
        CHECKED_MALLOC( len, profile );
        PrfQueryProfileData( initData->iniFile, appName, keyName, profile,
         &len );
        profile[len] = 0;
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs,
         LM_INSERTITEM, MPFROMSHORT( LIT_END ), MPFROMP( profile ) ) );
        CHECKED_FREE( profile );
        if ( selectedItem == i )
        {
          selectMe = idx;
        }
      }
      
      if ( selectMe != -1 )
      {
        WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_SELECTITEM,
         MPFROMSHORT( selectMe ), MPFROMSHORT( TRUE ) );
      }
      
      CHECKED_FREE( appName );
      CHECKED_FREE( keyName );
      
      WinSetWindowPtr( win, QWL_USER, initData );
    }
    break;
    case WM_CONTROL:
      if ( SHORT2FROMMP( mp1 ) == LN_SELECT &&
            SHORT1FROMMP( mp1 ) == MRM_LB_RecentlyUsedMsgs )
      {
        char *itemText;
        short idx, textLen;
        
        WinSetDlgItemText( win, MRM_ML_MessageEditor, NULL );
        
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs,
         LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), NULL ) );
        
        if ( idx == LIT_NONE )
        {
          WinEnableWindow( WinWindowFromID( win, MRM_PB_RemoveMessage ),
           FALSE );
          WinEnableWindow( WinWindowFromID( win, MRM_PB_EditMessage ),
           FALSE );
          break;
        }
        
        textLen = SHORT1FROMMR( WinSendDlgItemMsg( win,
         MRM_LB_RecentlyUsedMsgs, LM_QUERYITEMTEXTLENGTH, MPFROMSHORT( idx ),
         NULL ) );
        textLen++;
        CHECKED_MALLOC( textLen, itemText );
        WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_QUERYITEMTEXT,
         MPFROM2SHORT( idx, textLen ), MPFROMP( itemText ) );
        WinSendDlgItemMsg( win, MRM_ML_MessageEditor, MLM_INSERT,
         MPFROMP( itemText ), NULL );
        CHECKED_FREE( itemText );
        
        WinEnableWindow( WinWindowFromID( win, MRM_PB_RemoveMessage ), TRUE );
        WinEnableWindow( WinWindowFromID( win, MRM_PB_EditMessage ), TRUE );
      } else if ( SHORT2FROMMP( mp1 ) == MLN_CHANGE &&
                   SHORT1FROMMP( mp1 ) == MRM_ML_MessageEditor )
      {
        LONG textLen;
        
        textLen = LONGFROMMR( WinSendDlgItemMsg( win, MRM_ML_MessageEditor,
         MLM_QUERYTEXTLENGTH, NULL, NULL ) );
        if ( textLen )
        {
          WinEnableWindow( WinWindowFromID( win, MRM_PB_AddMessage ), TRUE );
        } else {
          WinEnableWindow( WinWindowFromID( win, MRM_PB_AddMessage ), FALSE );
        }
      }
    break;
    case WM_COMMAND:
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case MRM_PB_AddMessage:
        {
          LONG textLen;
          short idx, len;
          char *itemText;
          
          textLen = WinQueryDlgItemTextLength( win, MRM_ML_MessageEditor );
          if ( textLen > 32767 ) len = 32767;
            else len = (short) textLen + 1;
          CHECKED_MALLOC( len, itemText );
          WinQueryDlgItemText( win, MRM_ML_MessageEditor, len, itemText );
          idx = SHORT1FROMMR( WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs,
           LM_INSERTITEM, MPFROMSHORT( LIT_END ), MPFROMP( itemText ) ) );
          CHECKED_FREE( itemText );
          WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_SELECTITEM,
           MPFROMSHORT( idx ), MPFROMLONG( TRUE ) );
        }
        return 0;
        case MRM_PB_RemoveMessage:
          short idx;
          
          idx = SHORT1FROMMR( WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs,
           LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), NULL ) );
          WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_DELETEITEM,
           MPFROMSHORT( idx ), NULL );
        return 0;
        case MRM_PB_EditMessage:
          WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_PB_RemoveMessage ),
           MPFROM2SHORT( CMDSRC_PUSHBUTTON, FALSE ) );
          WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_PB_AddMessage ),
           MPFROM2SHORT( CMDSRC_PUSHBUTTON, FALSE ) );
        return 0;
        case DID_OK:
        {
          messageEditInit *initData = (messageEditInit *)
           WinQueryWindowPtr( win, QWL_USER );
          char *appName, *keyName, *profile;
          unsigned short numProfs, i;
          short selectedItem, idx;
          int textLen;
          
          CHECKED_MALLOC( strlen( initData->userName ) + 10, appName );
          sprintf( appName, "%s Settings", initData->userName );
          
          CHECKED_MALLOC( strlen( initData->messageType ) + 12, keyName );
          
          selectedItem = SHORT1FROMMR( WinSendDlgItemMsg( win,
           MRM_LB_RecentlyUsedMsgs, LM_QUERYSELECTION,
           MPFROMSHORT( LIT_FIRST ), NULL ) );
          if ( selectedItem == LIT_NONE )
          {
            selectedItem = -1;
          } else {
            selectedItem++;
            // 1-based in the INI file
          }
          
          sprintf( keyName, "Current %s", initData->messageType );
          PrfWriteProfileData( initData->iniFile, appName, keyName,
           &selectedItem, sizeof( short ) );
          numProfs = SHORT1FROMMR( WinSendDlgItemMsg( win,
           MRM_LB_RecentlyUsedMsgs, LM_QUERYITEMCOUNT, NULL, NULL ) );
          
          sprintf( keyName, "Number of %ss", initData->messageType );
          PrfWriteProfileData( initData->iniFile, appName, keyName, &numProfs,
           sizeof( unsigned short ) );
          idx = LIT_FIRST;
          
          for ( i=0; i<numProfs; ++i )
          {
            sprintf( keyName, "%s %d", initData->messageType, i+1 );
            textLen = SHORT1FROMMR( WinSendDlgItemMsg( win,
             MRM_LB_RecentlyUsedMsgs, LM_QUERYITEMTEXTLENGTH,
             MPFROMSHORT( i ), NULL ) );
            textLen++;
            CHECKED_MALLOC( textLen, profile );
            WinSendDlgItemMsg( win, MRM_LB_RecentlyUsedMsgs, LM_QUERYITEMTEXT,
             MPFROM2SHORT( i, textLen ), MPFROMP( profile ) );
            PrfWriteProfileData( initData->iniFile, appName, keyName, profile,
             textLen - 1 );
            if ( i + 1 == selectedItem )
            {
              *(initData->profile) = convertTextToAOL( profile );
            }
            CHECKED_FREE( profile );
          }
          
          CHECKED_FREE( appName );
          CHECKED_FREE( keyName );
        }
        break;
      }
    break;
    case WM_DESTROY:
    {
      messageEditInit *initData = (messageEditInit *)
       WinQueryWindowPtr( win, QWL_USER );
      SWP swp;
      
      WinQueryWindowPos( win, &swp );
      if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
      {
        swp.x = WinQueryWindowUShort( win, QWS_XRESTORE );
        swp.y = WinQueryWindowUShort( win, QWS_YRESTORE );
        swp.cx = WinQueryWindowUShort( win, QWS_CXRESTORE );
        swp.cy = WinQueryWindowUShort( win, QWS_CYRESTORE );
      }
      
      PrfWriteProfileData( initData->iniFile, "Profile/Away Dialog",
       "Window position data", &swp, sizeof( SWP ) );
    }
    break;
  }
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmSplashScreen( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_CREATE:
    {
      ULONG waitTLen;
      LONG waitTime;
      
      waitTime = 5000;
      
      if ( PrfQueryProfileSize( HINI_USERPROFILE, "PM_ControlPanel",
            "LogoDisplayTime", &waitTLen ) )
      {
        char *buffer;
        
        CHECKED_MALLOC( waitTLen + 1, buffer );
        if ( PrfQueryProfileData( HINI_USERPROFILE, "PM_ControlPanel",
              "LogoDisplayTime", buffer, &waitTLen ) )
        {
          waitTime = atol( buffer );
          CHECKED_FREE( buffer );
          if ( waitTime == -1 )
          {
            waitTime = 5000;
          }
        }
      }
      
      WinSetWindowULong( win, 0, 0 );
      
      if ( waitTime != 0 )
      {
        HBITMAP bmp;
        HMODULE dll = getModuleHandle();
        HPS hps = WinGetPS( win );
        RECTL desktop;
        eventData theEventData;
        sessionThreadSettings *mySettings = (sessionThreadSettings *)
         PVOIDFROMMP( mp1 );
        
        bmp = GpiLoadBitmap( hps, dll, 1, 0, 0 );
        
        WinReleasePS( hps );
        WinSetWindowULong( win, 0, bmp );
        
        WinQueryWindowRect( HWND_DESKTOP, &desktop );
        WinSetWindowPos( win, 0, ((desktop.xRight - desktop.xLeft) / 2) -
         (SPLASH_WIDTH / 2), ((desktop.yTop - desktop.yBottom) / 2) -
         (SPLASH_HEIGHT / 2), SPLASH_WIDTH, SPLASH_HEIGHT, SWP_SIZE | SWP_MOVE |
         SWP_SHOW );
        
        theEventData.currentUser = NULL;
        theEventData.otherUser = NULL;
        theEventData.message = NULL;
        
        handleMrMessageEvent( EVENT_SPLASHSCREEN, NULL, mySettings,
         &theEventData, 0 );
        
        WinStartTimer( WinQueryAnchorBlock( win ), win, 1, waitTime );
      } else {
        WinPostMsg( win, WM_QUIT, 0, 0 );
      }
    }
    break;
    case WM_PAINT:
    {
      HBITMAP bmp = WinQueryWindowULong( win, 0 );
      RECTL rectl;
      HPS hps;
      POINTL ptl;
      
      if ( bmp == 0 ) break;
      
      hps = WinBeginPaint( win, 0, &rectl );
      
      ptl.x = rectl.xLeft;
      ptl.y = rectl.yBottom;
      
      WinDrawBitmap( hps, bmp, &rectl, &ptl, 0, 0, DBM_NORMAL );
      
      WinEndPaint( hps );
    }
    break;
    case WM_TIMER:
      WinPostMsg( win, WM_QUIT, 0, 0 );
    break;
    case WM_DESTROY:
      if ( WinQueryWindowULong( win, 0 ) )
        GpiDeleteBitmap( WinQueryWindowULong( win, 0 ) );
    break;
  }
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmIMwindow( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch( msg )
  {
    case WM_TYPING_NOTIFY:
    {
      HPOINTER writing = WinQueryWindowULong( win, 28 );
      HPOINTER paper = WinQueryWindowULong( win, 32 );
      HPOINTER status = WinQueryWindowULong( win, 36 );
      
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case 0:
          WinSendMsg( WinQueryWindow( win, QW_PARENT ),
           WM_SETICON, MPFROMLONG( status ), NULL );
          WinSetWindowULong( win, 40, 0 );
        break;
        case 1:
          WinSendMsg( WinQueryWindow( win, QW_PARENT ),
           WM_SETICON, MPFROMLONG( paper ), NULL );
          WinSetWindowULong( win, 40, 1 );
        break;
        case 2:
          WinSendMsg( WinQueryWindow( win, QW_PARENT ),
           WM_SETICON, MPFROMLONG( writing ), NULL );
          WinSetWindowULong( win, 40, 1 );
        break;
        default:
          debugf( "Bad typing notification code (%d) sent to IM window.\n",
           SHORT1FROMMP( mp1 ) );
      }
    }
    break;
    case WM_BUDDYSTATUS:
      if ( LONGFROMMP( mp1 ) )
      {
        ULONG textLen = LONGFROMMR( WinSendDlgItemMsg( win, MRM_ML_IMSender,
         MLM_QUERYTEXTLENGTH, NULL, NULL ) );
        
        WinSetWindowULong( win, 8, 1 );
        WinEnableWindow( WinWindowFromID( win, MRM_SendMessage ),
         textLen != 0 );
      } else {
        WinSetWindowULong( win, 8, 0 );
        WinEnableWindow( WinWindowFromID( win, MRM_SendMessage ), FALSE );
      }
    break;
    case WM_CREATE:
    {
      HWND theWin;
      MLECTLDATA mleData;
      PFNWP oldFunc;
      HRTV viewer;
      HPOINTER writing, paper;
      HMODULE dll = getModuleHandle();
      
      writing = WinLoadPointer( HWND_DESKTOP, dll, 12 );
      paper = WinLoadPointer( HWND_DESKTOP, dll, 13 );
      
      WinSetWindowULong( win, 28, writing );
      WinSetWindowULong( win, 32, paper );
      
      // Create our controls, but wait until we know the window size to
      //  position them.  Wait for the initial WM_SIZE message.
      
      theWin = WinCreateWindow( win, WC_BUTTON, "Send",
       WS_DISABLED | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_DEFAULT |
        BS_PUSHBUTTON, 0, 0, 0, 0, win, HWND_TOP, MRM_SendMessage,
        NULL, NULL );
      
      theWin = WinCreateWindow( win, WC_BUTTON, "Close",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON, 0, 0, 0, 0, win,
       HWND_TOP, DID_CANCEL, NULL, NULL );
      
      mleData.cbCtlData = sizeof( MLECTLDATA );
      mleData.afIEFormat = MLFIE_CFTEXT;
      mleData.cchText = 65536;
      mleData.iptAnchor = 0;
      mleData.iptCursor = 0;
      mleData.cxFormat = 0;
      mleData.cyFormat = 0;
      mleData.afFormatFlags = MLFFMTRECT_MATCHWINDOW;
      
      viewer = ACLCreateRichTextView( win, 0, 0, 0, 0, FALSE );
      WinSetWindowULong( win, 16, viewer );
      theWin = ACLRTVGetWindow( viewer );
      WinSetOwner( theWin, win );
      
      theWin = WinCreateWindow( win, WC_MLE, NULL,
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | MLS_BORDER | MLS_VSCROLL |
       MLS_WORDWRAP, 0, 0, 0, 0, win, HWND_TOP, MRM_ML_IMSender, &mleData,
       NULL );
      
      oldFunc = WinSubclassWindow( theWin, mleEnterPerLine );
      WinSetWindowPtr( win, 12, oldFunc );
      
      WinSetWindowULong( win, 8, 1 );
      WinSetWindowULong( win, 20, 0 );
      WinSetWindowULong( win, 24, 0 );
    }
    break;
    case WM_SIZE:
    {
      int newCx, newCy, buttonHeight, heightLeft;
      HPS hps;
      POINTL points[3];
      HWND theWin;
      HRTV viewer = WinQueryWindowULong( win, 16 );
      
      newCx = SHORT1FROMMP( mp2 );
      newCy = SHORT2FROMMP( mp2 );
      
      theWin = WinWindowFromID( win, MRM_SendMessage );
      hps = WinGetPS( theWin );
      // Make sure we pick up presentation params appropriate to the buttons
      //  when sizing the text.
      
      GpiQueryTextBox( hps, 10, "Send Close", 3, (PPOINTL) points );
      buttonHeight = points[0].y - points[1].y + 8;
      
      WinReleasePS( hps );
      
      WinSetWindowPos( theWin, 0, newCx / 50, newCy / 50, 47 * newCx / 100,
       buttonHeight, SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, DID_CANCEL );
      
      WinSetWindowPos( theWin, 0, 25 * newCx / 50, newCy / 50, 47 * newCx / 100,
       buttonHeight, SWP_SIZE | SWP_MOVE );
      
      heightLeft = newCy - buttonHeight - (newCy / 25);
      
      theWin = ACLRTVGetWindow( viewer );
      
      buttonHeight += newCx / 50;
      
      WinSetWindowPos( theWin, 0, newCx / 50,
       buttonHeight + (heightLeft * 51 / 100), newCx * 24 / 25,
       heightLeft * 49 / 100, SWP_SIZE | SWP_MOVE | SWP_SHOW );
       
      theWin = WinWindowFromID( win, MRM_ML_IMSender );
      
      WinSetWindowPos( theWin, 0, newCx / 50,
       buttonHeight + (heightLeft / 100), newCx * 24 / 25,
       heightLeft * 49 / 100, SWP_SIZE | SWP_MOVE );
    }
    break;
    case WM_FOCUSCHANGE:
    {
      if ( SHORT1FROMMP( mp2 ) )
      {
        // Getting the focus.  Move the focus to the multi-line entry window.
        WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_ML_IMSender ) );
      }
    }
    break;
    case WM_PAINT:
    {
      HPS hps;
      RECTL rect;
      POINTL pt;
      ULONG colorIdx, ret;
      
      ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
       &colorIdx, QPF_ID1COLORINDEX | QPF_NOINHERIT );
      if ( !ret )
      {
        ret = WinQueryPresParam( win, PP_BACKGROUNDCOLOR, 0, NULL, 4,
         &colorIdx, QPF_NOINHERIT );
        if ( !ret )
        {
          ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
           &colorIdx, QPF_ID1COLORINDEX );
        }
      }
      
      hps = WinBeginPaint( win, NULLHANDLE, &rect );
      if ( ret < 4 )
        GpiSetColor( hps, CLR_PALEGRAY );
      else {
        GpiCreateLogColorTable( hps, 0, LCOLF_RGB, 0, 0, NULL );
        GpiSetColor( hps, colorIdx );
      }
      pt.x = rect.xLeft;
      pt.y = rect.yBottom;
      GpiMove( hps, &pt );
      pt.x = rect.xRight;
      pt.y = rect.yTop;
      GpiBox( hps, DRO_FILL, &pt, 0, 0 );
      WinEndPaint( hps );
    }
    break;
    case WM_CLOSE:
    {
      HWND buddyListWin = WinQueryWindowULong( win, 4 );
      Buddy *theBuddy = (Buddy *) WinQueryWindowPtr( win, 0 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr(
       buddyListWin, 24 );
      HWND mgrWin = WinQueryWindowULong( buddyListWin, 16 );
      HWND frameWin = WinQueryWindow( win, QW_PARENT );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      HRTV viewer = WinQueryWindowULong( win, 16 );
      HPOINTER writing = WinQueryWindowULong( win, 28 );
      HPOINTER paper = WinQueryWindowULong( win, 32 );
      char *appName;
      SWP swp;
      int len;
      
      WinDestroyPointer( writing );
      WinDestroyPointer( paper );
      
      WinDestroyWindow( WinWindowFromID( win, MRM_SendMessage ) );
      WinDestroyWindow( WinWindowFromID( win, DID_CANCEL ) );
      WinDestroyWindow( WinWindowFromID( win, MRM_ML_IMSender ) );
      WinDestroyWindow( ACLRTVGetWindow( viewer ) );
      
      WinSendMsg( WinWindowFromID( buddyListWin, MRM_CN_BuddyContainer ),
       CM_SETRECORDEMPHASIS, MPFROMP( theBuddy->record ),
       MPFROM2SHORT( FALSE, CRA_INUSE ) );
      
      theBuddy->imChatWin = 0;
      theBuddy->imChatClientWin = 0;
      
      len = strlen( theSession->getUserName() ) +
       strlen( theBuddy->userData.screenName ) + 31;
      
      CHECKED_MALLOC( len, appName );
      
      sprintf( appName, "[%s <-> %s] Instant Message Window",
       theSession->getUserName(), theBuddy->userData.screenName );
       
      WinQueryWindowPos( frameWin, &swp );
      if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
      {
        swp.x = WinQueryWindowUShort( frameWin, QWS_XRESTORE );
        swp.y = WinQueryWindowUShort( frameWin, QWS_YRESTORE );
        swp.cx = WinQueryWindowUShort( frameWin, QWS_CXRESTORE );
        swp.cy = WinQueryWindowUShort( frameWin, QWS_CYRESTORE );
      }
      
      PrfWriteProfileData( iniFile, appName, "Window position data",
       &swp, sizeof( SWP ) );
      
      CHECKED_FREE( appName );
      
      WinDestroyWindow( frameWin );
      
      return 0;
      // Prevent the default window procedure from sending a WM_QUIT to the
      //  message queue and shutting down the whole app.
    }
    case WM_COMMAND:
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case DID_CANCEL:
          WinSendMsg( win, WM_CLOSE, 0, 0 );
        break;
        case MRM_SendMessage:
        {
          HWND buddyListWin = WinQueryWindowULong( win, 4 );
          HWND mgrWin = WinQueryWindowULong( buddyListWin, 16 );
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( mgrWin, 60 );
          OscarSession *theSession = (OscarSession *) WinQueryWindowPtr(
           buddyListWin, 24 );
          Buddy *theBuddy = (Buddy *) WinQueryWindowPtr( win, 0 );
          OscarData *theIM;
          char *theText, *annotatedMessage, *convertedText;
          ULONG origLen = WinQueryDlgItemTextLength( win, MRM_ML_IMSender );
          ULONG textLen;
          HRTV viewer = WinQueryWindowULong( win, 16 );
          HWND viewerWin = ACLRTVGetWindow( viewer );
          eventData theEventData;
          
          if ( !theBuddy->userData.isOnline )
          {
            WinAlarm( HWND_DESKTOP, WA_WARNING );
            break;
          }
          
          textLen = origLen + 1;
          
          CHECKED_MALLOC( textLen, theText );
          WinQueryDlgItemText( win, MRM_ML_IMSender, textLen, theText );
          theText[ textLen - 1 ] = 0;
          
          convertedText = convertTextToAOL( theText );

          theEventData.currentUser = theSession->getUserName();
          theEventData.otherUser = theBuddy->userData.screenName;
          theEventData.message = theText;
          
          if ( !(handleMrMessageEvent( EVENT_PRE_SEND, theSession->settings,
                  globalSettings, &theEventData, 0 ) &
                 (EVENT_PROCESSED_SCRIPT | EVENT_PROCESSED_SHELL)) )
          {
            // Don't actually send the message if a script is going to
            //  pre-process it.
            theIM = new OscarData( theSession->getSeqNumPointer() );
            
            if ( theBuddy->userData.userClass & 64 )
            {
              // Don't wrap the text message in HTML if it is an ICQ user
              theIM->sendInstantMessage( theBuddy->userData.screenName,
               convertedText, NO_HTML_WRAP );
            } else {
              theIM->sendInstantMessage( theBuddy->userData.screenName,
               convertedText, HTML_WRAP );
            }
            theSession->queueForSend( theIM, 15 );
          }

          theEventData.currentUser = theSession->getUserName();
          theEventData.otherUser = theBuddy->userData.screenName;
          theEventData.message = theText;
          
          handleMrMessageEvent( EVENT_SENT, theSession->settings,
           globalSettings, &theEventData, 0 );
          
          debugf( "Sending message: %s\n", convertedText );
          
          CHECKED_FREE( convertedText );
          
          convertedText = convertTextToRTV( theText );
          
          if ( testSettingsFlag( SET_TIMESTAMPS_ENABLED, theSession->settings,
                globalSettings ) )
          {
            time_t utcTime;
            struct tm *localTime;
            
            CHECKED_MALLOC( strlen( convertedText ) +
             strlen( theSession->getUserName() ) + 88, annotatedMessage );
            
            utcTime = time( NULL );
            localTime = localtime( &utcTime );
            
            sprintf( annotatedMessage, "<b><color red>[%s</b> - %02d:%02d:%02d<b>]:</b><color black> <leftmargin here>%s<leftmargin 0>",
             theSession->getUserName(), localTime->tm_hour, localTime->tm_min,
             localTime->tm_sec, convertedText );
          } else {
            CHECKED_MALLOC( strlen( convertedText ) +
             strlen( theSession->getUserName() ) + 70, annotatedMessage );
            
            sprintf( annotatedMessage, "<b><color red>[%s]:</b><color black> <leftmargin here>%s<leftmargin 0>",
             theSession->getUserName(), convertedText );
          }
          
          CHECKED_FREE( convertedText );
          CHECKED_FREE( theText );
          
          ACLRTVAddParagraph( viewer, annotatedMessage );
          
          WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
           KC_VIRTUALKEY | KC_CTRL, 0 ), MPFROM2SHORT( 0,
           VK_END ) );
          WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
           KC_VIRTUALKEY | KC_KEYUP | KC_CTRL, 0 ), MPFROM2SHORT( 0,
           VK_END ) );
          // Emulate pressing CTRL-END to scroll to the bottom of the viewer
          
          CHECKED_FREE( annotatedMessage );
          
          WinStopTimer( WinQueryAnchorBlock( win ), win, 1 );
          // Stop any typing notification updates
          
          // No need to actually send the "typing stopped" notification
          //  because it is assumed when an IM is sent.
          
          WinSetWindowULong( win, 20, 0 );
          WinSetWindowULong( win, 24, 0 );
          WinSetDlgItemText( win, MRM_ML_IMSender, NULL );
          WinEnableWindow( WinWindowFromID( win, MRM_SendMessage ),
           FALSE );
        }
        break;
      }
    break;
    case WM_TIMER:
    {
      // Typing notification is activated.  Periodically check the length of
      //  text in the IM window and report periodically when typing at rest
      HWND buddyListWin = WinQueryWindowULong( win, 4 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr(
       buddyListWin, 24 );
      Buddy *theBuddy = (Buddy *) WinQueryWindowPtr( win, 0 );
      int prevLen = WinQueryWindowULong( win, 20 );
      int prevState = WinQueryWindowULong( win, 24 );
      int curLen = LONGFROMMR( WinSendDlgItemMsg( win, MRM_ML_IMSender,
       MLM_QUERYTEXTLENGTH, NULL, NULL ) );
      OscarData *typingNotify;
      int type = -1;
      
      if ( curLen == prevLen && curLen && prevState != 1 )
      {
        type = 1;
        // Text has been typed, but no typing is currently going on
        WinSetWindowULong( win, 24, 1 );
      } else if ( curLen == 0 && prevState != 0 )
      {
        type = 0;
        WinStopTimer( WinQueryAnchorBlock( win ), win, 1 );
        // Text has been removed... stop periodic checks
        WinSetWindowULong( win, 24, 0 );
      } else if ( curLen != prevLen && prevState != 2 )
      {
        type = 2;
        // Still typing
        WinSetWindowULong( win, 24, 2 );
      }
      
      WinSetWindowULong( win, 20, curLen );
      if ( type != -1 )
      {
        typingNotify = new OscarData( theSession->getSeqNumPointer() );
        typingNotify->prepareTypingNotification(
         theBuddy->userData.screenName, type );
        theSession->queueForSend( typingNotify, 15 );
      }
    }
    break;
    case WM_CONTROL:
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case MRM_ML_IMSender:
          if ( SHORT2FROMMP( mp1 ) == MLN_CHANGE )
          {
            // Text changed in entry field.
            HWND buddyListWin = WinQueryWindowULong( win, 4 );
            HWND mgrWin = WinQueryWindowULong( buddyListWin, 16 );
            ULONG textLen = LONGFROMMR( WinSendDlgItemMsg( win, MRM_ML_IMSender,
             MLM_QUERYTEXTLENGTH, NULL, NULL ) );
            ULONG allowSend = WinQueryWindowULong( win, 8 );
            sessionThreadSettings *globalSettings = (sessionThreadSettings *)
             WinQueryWindowPtr( mgrWin, 60 );
            OscarSession *theSession = (OscarSession *) WinQueryWindowPtr(
             buddyListWin, 24 );
            Buddy *theBuddy = (Buddy *) WinQueryWindowPtr( win, 0 );
            
            if ( textLen )
            {
              WinSendMsg( buddyListWin, WM_NOTIDLE, 0, 0 );
              if ( testSettingsFlag( SET_TYPING_NOTIFICATIONS,
                    theSession->settings, globalSettings ) )
              {
                OscarData *typingNotify =
                 new OscarData( theSession->getSeqNumPointer() );
                
                if ( WinQueryWindowULong( win, 20 ) == 0 &&
                      WinQueryWindowULong( win, 24 ) != 2 )
                {
                  typingNotify->prepareTypingNotification(
                   theBuddy->userData.screenName, 2 );
                  // Typing begun
                  
                  theSession->queueForSend( typingNotify, 15 );
                  
                  WinStartTimer( WinQueryAnchorBlock( win ), win, 1,
                   TYPING_NOTIFICATION_TIMER );
                  // Start a timer to space out the "text typed" notification
                  WinSetWindowULong( win, 20, textLen );
                  WinSetWindowULong( win, 24, 2 );
                }
              }
            }
            WinEnableWindow( WinWindowFromID( win, MRM_SendMessage ),
             textLen != 0 && allowSend );
          }
        break;
      }
    break;
    case WM_MOUSEMOVE:
    case WM_CHAR:
    {
      HWND buddyListWin = WinQueryWindowULong( win, 4 );
      WinSendMsg( buddyListWin, WM_NOTIDLE, 0, 0 );
    }
    break;
    case WM_POSTMESSAGE:
    {
      HRTV viewer;
      HWND viewerWin;
      char *message = (char *) PVOIDFROMMP( mp1 );
      
      viewer = WinQueryWindowULong( win, 16 );
      ACLRTVAddParagraph( viewer, message );
      viewerWin = ACLRTVGetWindow( viewer );
      
      WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
       KC_VIRTUALKEY | KC_CTRL, 0 ), MPFROM2SHORT( 0, VK_END ) );
      WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
       KC_VIRTUALKEY | KC_KEYUP | KC_CTRL, 0 ), MPFROM2SHORT( 0, VK_END ) );
      // Emulate pressing CTRL-END to scroll to the bottom of the viewer
      
      CHECKED_FREE( message );
    }
    break;
  }
  
  return WinDefWindowProc( win, msg, mp1, mp2 );
}


typedef struct
{
  char *windowText;
} bubbleWindowInitStruct;

MRESULT EXPENTRY pmBubbleWindow( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_CREATE:
    {
      bubbleWindowInitStruct *initParms = (bubbleWindowInitStruct *)
       PVOIDFROMMP( mp1 );
      char *tmpString, *strPos, *oldPos;
      int len, cx, cy, i, numLines, maxSize;
      HPS hps, hpsMem;
      HDC windowDC, memDC;
      HBITMAP backgroundBmp;
      SIZEL psSize;
      POINTL textBox[3];
      SWP winPos;
      DEVOPENSTRUC dop = {0L, "DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
      BITMAPINFOHEADER2 bmpInfo = { 0 };
      SIZEL sizl = { 0, 0 };
      FATTRS fat = { 0 };
      PFONTMETRICS pfm;
      char *theFont = "WarpSans";
      LONG tmpLong, numFonts, hRes, vRes;
      RECTL desktop;
      int ratio, monitorCenterX, monitorEdgeX;

      len = strlen( initParms->windowText );
      CHECKED_MALLOC( len + 1, tmpString );
      strcpy( tmpString, initParms->windowText );
      WinSetWindowPtr( win, 0, tmpString );
      
      psSize.cx = 0;
      psSize.cy = 0;
      windowDC = WinOpenWindowDC( win );
      hps = GpiCreatePS( WinQueryAnchorBlock( win ), windowDC, &psSize,
       PU_PELS | GPIF_DEFAULT | GPIT_NORMAL | GPIA_ASSOC );
      WinSetWindowULong( win, 4, hps );
      
      DevQueryCaps( windowDC, CAPS_HORIZONTAL_FONT_RES, 1, &hRes );
      DevQueryCaps( windowDC, CAPS_VERTICAL_FONT_RES,   1, &vRes );
      
      tmpLong = 0;
      numFonts = GpiQueryFonts( hps, QF_PUBLIC, theFont, &tmpLong, 0, NULL );
      
      if ( numFonts <= 0 )
      {
        // Fall back to Helv
        theFont = "Helvetica";
        tmpLong = 0;
        numFonts = GpiQueryFonts( hps, QF_PUBLIC, theFont, &tmpLong,
         0, NULL );
      }
      
      if ( numFonts > 0 )
      {
        CHECKED_MALLOC( numFonts * sizeof( FONTMETRICS ), pfm );
        
        GpiQueryFonts( hps, QF_PUBLIC, theFont, &numFonts,
         sizeof( FONTMETRICS ), pfm );
         
        for ( i=0; i<numFonts; ++i )
        {
          if ( (pfm[i].fsDefn & FM_DEFN_OUTLINE) ||
                (pfm[i].sNominalPointSize < 100 &&
                 pfm[i].sXDeviceRes == hRes &&
                 pfm[i].sYDeviceRes == vRes &&
                 (pfm[i].fsDefn & FM_DEFN_GENERIC)) )
          {
            fat.usRecordLength = sizeof( FATTRS );
            fat.fsSelection = FATTR_SEL_BOLD;
            fat.lMatch = pfm[i].lMatch;
            strcpy( fat.szFacename, theFont );
            
            GpiCreateLogFont( hps, NULL, 1, &fat );
            GpiSetCharSet( hps, 1 );
            
            if ( pfm[i].fsDefn & FM_DEFN_OUTLINE )
            {
              SIZEF sizef;
              fat.fsSelection = 0;
              sizef.cx = (65536 * hRes * 8) / 72;
              sizef.cy = (65536 * vRes * 8) / 72;
              GpiSetCharBox( hps, &sizef );
            }
            
            break;
          }
        }
        CHECKED_FREE( pfm );
      }
      
      len = strlen( tmpString );
      maxSize = 0;
      for ( i=0; i<len; ++i )
      {
        if ( tmpString[i] != '\n' ) maxSize++;
          else maxSize = 0;
        
        if ( maxSize > 39 && tmpString[i] == ' ' )
        {
          tmpString[i] = '\n';
          maxSize = 0;
        }
      }
      // Preprocess by trying to split lines longer than 40 characters
      
      numLines = 0;
      strPos = strchr( tmpString, '\n' );
      maxSize = 0;
      oldPos = tmpString;
      
      while ( strPos )
      {
        *strPos = 0;
        if ( *oldPos )
        {
          GpiQueryTextBox( hps, strlen( oldPos ), oldPos, 3, (POINTL *)textBox );
          if ( textBox[2].x - textBox[0].x + 1 > maxSize )
            maxSize = textBox[2].x - textBox[0].x + 1;
          numLines++;
        }
        *strPos = '\n';
        oldPos = strPos + 1;
        strPos = strchr( oldPos, '\n' );
      }
      
      if ( *oldPos )
      {
        GpiQueryTextBox( hps, strlen( oldPos ), oldPos, 3, (POINTL *)textBox );
        if ( textBox[2].x - textBox[0].x + 1 > maxSize )
          maxSize = textBox[2].x - textBox[0].x + 1;
        numLines++;
      }
      
      WinSetWindowULong( win, 20, textBox[0].y - textBox[1].y + 1 );
      
      cx = maxSize + 16;
      cy = ((textBox[0].y - textBox[1].y + 1) * numLines) + 16;
      // Add 8 pixel breathing room on each side of the window
      
      memDC = DevOpenDC( WinQueryAnchorBlock( win ), OD_MEMORY, "*", 5L,
       (PDEVOPENDATA) &dop, 0 );
      hpsMem = GpiCreatePS( WinQueryAnchorBlock( win ), memDC, &sizl,
       PU_PELS | GPIA_ASSOC );
      
      bmpInfo.cbFix = sizeof( BITMAPINFOHEADER2 );
      bmpInfo.cx = cx;
      bmpInfo.cy = cy;
      bmpInfo.cPlanes = 1;
      bmpInfo.cBitCount = 24;
      backgroundBmp = GpiCreateBitmap( hps, &bmpInfo, 0, NULL, NULL );
      
      GpiSetBitmap( hpsMem, backgroundBmp );

      WinSetWindowULong( win, 8, memDC );
      WinSetWindowULong( win, 12, hpsMem );
      WinSetWindowULong( win, 16, backgroundBmp );
      
      WinQueryWindowPos( win, &winPos );
      WinQueryWindowRect( HWND_DESKTOP, &desktop );
      
      ratio = (desktop.xRight - desktop.xLeft) /
               (desktop.yTop - desktop.yBottom );
      
      if ( ratio < 1 ) ratio = 1;
      monitorEdgeX = 0;
      
      for ( i=0; i<ratio; i++ )
      {
        // Support multiple monitors arranged horizontally.
        monitorCenterX = ((desktop.xRight - desktop.xLeft) / (2 * ratio)) +
         monitorEdgeX;
        monitorEdgeX = ((desktop.xRight - desktop.xLeft) / ratio) * (i+1);
        
        if ( (winPos.x - desktop.xLeft) > monitorCenterX &&
              (winPos.x - desktop.xLeft) <= monitorEdgeX )
        {
          // Put the window to the left of the current location
          WinSetWindowPos( win, 0, winPos.x - cx - 1, winPos.y - cy, cx - 1,
           cy - 1, SWP_MOVE | SWP_SIZE | SWP_SHOW );
          break;
        } else if ( winPos.x - desktop.xLeft <= monitorCenterX )
        {
          // Put the window to the right
          WinSetWindowPos( win, 0, winPos.x, winPos.y - cy, cx - 1, cy - 1,
           SWP_MOVE | SWP_SIZE | SWP_SHOW );
          break;
        }
      }
    }
    break;
    case WM_MOVE:
    {
      // Grab the bitmap from our new location if something forced the bubble
      //  to move (like SNAP's multihead window centering code)
      HPS hps = WinGetPS( win );
      HPS hpsMem = WinQueryWindowULong( win, 12 );
      POINTL bmpPts[3];
      unsigned char *bmpData;
      int cx, cy, i;
      RECTL rect;
      BITMAPINFO2 bmpInf2 = { 0 };
      
      WinQueryWindowRect( win, &rect );
      cx = rect.xRight - rect.xLeft + 1;
      cy = rect.yTop - rect.yBottom + 1;
      
      bmpPts[0].x = 0;      bmpPts[0].y = 0;
      bmpPts[1].x = cx - 1; bmpPts[1].y = cy - 1;
      bmpPts[2].x = 0;      bmpPts[2].y = 0;
      GpiBitBlt( hpsMem, hps, 3, (PPOINTL) bmpPts, ROP_SRCCOPY, BBO_IGNORE );
      
      CHECKED_MALLOC( cx * (cy+1) * 3, bmpData );

      bmpInf2.cbFix = 16;
      bmpInf2.cx = 0;
      bmpInf2.cy = 0;
      bmpInf2.cPlanes = 1;
      bmpInf2.cBitCount = 24;
      if ( GpiQueryBitmapBits( hpsMem, 0, cy - 1, (char *)bmpData, &bmpInf2 ) != cy - 1 )
      {
        debugf( "Error querying bitmap bits: 0x%lx\n",
         WinGetLastError( WinQueryAnchorBlock( win ) ) );
      }
      
      for ( i=0; i<cx*(cy+1)*3; ++i )
        bmpData[i] = (unsigned char) (0x7f + (bmpData[i] / 4));
      
      bmpInf2.cbFix = 16;
      
      if ( GpiSetBitmapBits( hpsMem, 0, cy - 1, (char *)bmpData, &bmpInf2 ) != cy - 1 )
      {
        debugf( "Error setting bitmap bits: 0x%lx\n",
         WinGetLastError( WinQueryAnchorBlock( win ) ) );
      }
      WinReleasePS( hps );
      CHECKED_FREE( bmpData );
      WinInvalidateRect( win, NULL, TRUE );
    }
    break;
    case WM_DESTROY:
    {
      HPS hps = WinQueryWindowULong( win, 4 );
      HPS hpsMem = WinQueryWindowULong( win, 12 );
      HBITMAP hbm = WinQueryWindowULong( win, 16 );
      HDC memDC = WinQueryWindowULong( win, 8 );
      char *windowText = (char *) WinQueryWindowPtr( win, 0 );
      if ( windowText )
      {
        CHECKED_FREE( windowText );
        WinSetWindowPtr( win, 16, NULL );
      }
      GpiSetBitmap( hpsMem, 0 );
      GpiDeleteBitmap( hbm );
      GpiDestroyPS( hpsMem );
      DevCloseDC( memDC );
      GpiDeleteSetId( hps, 1 );
      GpiDestroyPS( hps );
    }
    break;
    case WM_PAINT:
    {
      HPS hps = WinQueryWindowULong( win, 4 );
      HPS hpsMem = WinQueryWindowULong( win, 12 );
      HBITMAP hbm = WinQueryWindowULong( win, 16 );
      char *windowText = (char *) WinQueryWindowPtr( win, 0 );
      char *strPos, *oldPos;
      POINTL bmpPts[3];
      POINTL pt = { 8, 8 };
      RECTL updateRect;

      hps = WinBeginPaint( win, hps, &updateRect );
      bmpPts[0].x = updateRect.xLeft;      bmpPts[0].y = updateRect.yBottom;
      bmpPts[1].x = updateRect.xRight;     bmpPts[1].y = updateRect.yTop;
      bmpPts[2].x = updateRect.xLeft;      bmpPts[2].y = updateRect.yBottom;
      GpiBitBlt( hps, hpsMem, 3, (PPOINTL) bmpPts, ROP_SRCCOPY, BBO_IGNORE );
      
      oldPos = NULL;
      strPos = strrchr( windowText, '\n' );
      if ( strPos )
      {
        while ( strPos )
        {
          if ( *(strPos + 1) )
          {
            GpiCharStringPosAt( hps, &pt, &updateRect, CHS_CLIP,
             strlen( strPos + 1 ), strPos + 1, NULL );
            pt.y += WinQueryWindowULong( win, 20 );
          }
          if ( oldPos ) *oldPos = '\n';
          *strPos = 0;
          oldPos = strPos;
          strPos = strrchr( windowText, '\n' );
        }
      }
      
      if ( *windowText )
        GpiCharStringPosAt( hps, &pt, &updateRect, CHS_CLIP,
         strlen( windowText ), windowText, NULL );
      
      if ( oldPos ) *oldPos = '\n';
      WinEndPaint( hps );
    }
    break;
    case WM_BUTTON1DOWN:
    case WM_BUTTON2DOWN:
    case WM_BUTTON3DOWN:
      WinDestroyWindow( win );
    break;
  }
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

#define MRM_BUTTONTYPE_ONLINEFILTER  1
#define MRM_BUTTONTYPE_AWAYFILTER    2
#define MRM_BUTTONTYPE_OFFLINEFILTER 3

typedef struct
{
  int buttonType;
  MRESULT EXPENTRY (*oldWindowProc) ( HWND, ULONG, MPARAM, MPARAM );
  HWND bubbleWin;
  USHORT oldX, oldY;
} buttonHelpInfo;

MRESULT EXPENTRY pmBubbleHelpCapableButton( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  buttonHelpInfo *myInfo;
  
  myInfo = (buttonHelpInfo *) WinQueryWindowPtr( win, QWL_USER );
  if ( !myInfo ) return NULL;
  
  switch ( msg )
  {
    case WM_TIMER:
    {
      bubbleWindowInitStruct bubbleInit;
      POINTL pointerPosAbs, pointerPos;
      RECTL windowRect;
      
      if ( SHORT1FROMMP( mp1 ) != 1 )
        break;
      
      WinStopTimer( WinQueryAnchorBlock( win ), win, 1 );
      
      if ( myInfo->bubbleWin ) WinDestroyWindow( myInfo->bubbleWin );
      
      WinQueryPointerPos( HWND_DESKTOP, &pointerPosAbs );
      pointerPos.x = pointerPosAbs.x;
      pointerPos.y = pointerPosAbs.y;
      WinMapWindowPoints( HWND_DESKTOP, win, &pointerPos, 1 );
      WinQueryWindowRect( win, &windowRect );
      
      if ( (pointerPos.x > windowRect.xRight) ||
           (pointerPos.x < windowRect.xLeft) ||
           (pointerPos.y > windowRect.yTop) ||
           (pointerPos.y < windowRect.yBottom) )
      {
        // Cursor is outside of this window now
        return NULL;
      }
      
      switch ( myInfo->buttonType )
      {
        case 1:
          bubbleInit.windowText = "Filter out Online users";
        break;
        case 2:
          bubbleInit.windowText = "Filter out Away users";
        break;
        case 3:
          bubbleInit.windowText = "Filter out Offline users";
        break;
        default:
          WinAlarm( HWND_DESKTOP, WA_WARNING );
          debugf( "Bubble help requested over an unknown button!" );
          if ( myInfo->oldWindowProc )
            return myInfo->oldWindowProc( win, msg, mp1, mp2 );
          return NULL;
      }
      
      myInfo->bubbleWin = WinCreateWindow( HWND_DESKTOP,
       "MrMessage Bubble Window", NULL, 0, pointerPosAbs.x,
       pointerPosAbs.y - 10, 0, 0, HWND_DESKTOP, HWND_TOP, 9000, &bubbleInit, NULL );
      
      break;
    }
    case WM_MOUSEMOVE:
    {
      if ( SHORT1FROMMP( mp1 ) == myInfo->oldX &&
            SHORT2FROMMP( mp1 ) == myInfo->oldY )
        return NULL;
      
      myInfo->oldX = SHORT1FROMMP( mp1 );
      myInfo->oldY = SHORT2FROMMP( mp1 );
      
      if ( myInfo->bubbleWin ) WinDestroyWindow( myInfo->bubbleWin );
      myInfo->bubbleWin = 0;
      WinStartTimer( WinQueryAnchorBlock( win ), win, 1, BUBBLE_POPUP_TIMEOUT );
    }
    break;
  }
  
  if ( myInfo->oldWindowProc )
    return myInfo->oldWindowProc( win, msg, mp1, mp2 );
  
  return NULL;
}

MRESULT EXPENTRY pmBubbleHelpCapableWindow( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  MRESULT EXPENTRY (*oldProc) ( HWND, ULONG, MPARAM, MPARAM );
  HWND parentWin = WinQueryWindow( win, QW_PARENT );
  
  switch ( msg )
  {
    case WM_TIMER:
    {
      // Time to pop up some bubble information if appropriate
      OscarSession *theSession = (OscarSession *)
       WinQueryWindowPtr( parentWin, 24 );
      Buddy **buddyList;
      RECORDCORE *record;
      QUERYRECORDRECT rectQuery;
      RECTL windowRect, recordRect;
      POINTL pointerPos, pointerPosAbs;
      int i, numBuddies;
      
      if ( SHORT1FROMMP( mp1 ) != 1 )
        break;
      
      WinStopTimer( WinQueryAnchorBlock( win ), win, 1 );
      
      if ( WinQueryFocus( HWND_DESKTOP ) != win )
      {
        // If we're in a popup menu or some such thing, we don't want
        //  bubble help popping up.
        break;
      }
      
      DosRequestMutexSem( theSession->buddyListAccessMux, SEM_INDEFINITE_WAIT );
      
      buddyList = theSession->getBuddyList( &numBuddies );
      rectQuery.cb = sizeof( QUERYRECORDRECT );
      rectQuery.fRightSplitWindow = 0;
      rectQuery.fsExtent = CMA_TREEICON | CMA_ICON | CMA_TEXT;
      WinQueryPointerPos( HWND_DESKTOP, &pointerPosAbs );
      pointerPos.x = pointerPosAbs.x;
      pointerPos.y = pointerPosAbs.y;
      WinMapWindowPoints( HWND_DESKTOP, win, &pointerPos, 1 );
      for ( i=0; i<numBuddies; ++i )
      {
        if ( !buddyList[i] ) continue;
        record = buddyList[i]->record;
        if ( !record ) continue;
        rectQuery.pRecord = record;
        
        WinQueryWindowRect( win, &windowRect );
        
        WinSendMsg( win, CM_QUERYRECORDRECT, MPFROMP( &recordRect ),
         MPFROMP( &rectQuery ) );
        
        if ( pointerPos.x >= recordRect.xLeft &&
              pointerPos.x <= recordRect.xRight &&
              pointerPos.y >= recordRect.yBottom &&
              pointerPos.y <= recordRect.yTop )
        {
          // We have a hit!
          HWND bubbleWin = WinQueryWindowULong( parentWin, 36 );
          HPOINTER folderIcon = WinQueryWindowULong( parentWin, 12 );
          bubbleWindowInitStruct bubbleInit;
          
          if ( bubbleWin ) WinDestroyWindow( bubbleWin );
          
          WinSetWindowPtr( parentWin, 44, record );
          
          if ( record->hptrIcon == folderIcon )
          {
            CHECKED_MALLOC( 19 + strlen( record->pszTree ),
             bubbleInit.windowText );
            sprintf( bubbleInit.windowText, "Buddy Group Name: %s",
             record->pszTree );
          } else {
            if ( buddyList[i]->alias )
            {
              char *tmpString;
              CHECKED_MALLOC( 8 + strlen( buddyList[i]->alias ), tmpString );
              sprintf( tmpString, "Alias: %s", buddyList[i]->alias );
              bubbleInit.windowText =
               buddyList[i]->userData.getUserBlurbString( tmpString );
              CHECKED_FREE( tmpString );
            } else {
              bubbleInit.windowText =
               buddyList[i]->userData.getUserBlurbString( NULL );
            }
          }
          
          WinSetCapture( HWND_DESKTOP, win );
          
          bubbleWin = WinCreateWindow( HWND_DESKTOP, "MrMessage Bubble Window",
           NULL, 0, pointerPosAbs.x, pointerPosAbs.y - 10, 0, 0, win,
           HWND_TOP, 9000, &bubbleInit, NULL );
          
          CHECKED_FREE( bubbleInit.windowText );
          
          WinSetWindowULong( parentWin, 36, bubbleWin );
          
          break;
        }
      }
      DosReleaseMutexSem( theSession->buddyListAccessMux );
    }
    break;
    case WM_MOUSEMOVE:
    {
      static int oldX = -1000000;
      static int oldY = -1000000;
      int newX, newY;
      
      newX = SHORT1FROMMP( mp1 );
      newY = SHORT2FROMMP( mp1 );
      
      if ( oldX != newX || oldY != newY )
      {
        HAB ab = WinQueryAnchorBlock( win );
        HWND bubbleWin;
        RECTL recordRect;
        QUERYRECORDRECT rectQuery;
        RECORDCORE *record = (RECORDCORE *) WinQueryWindowPtr( parentWin, 44 );
        
        oldX = newX;
        oldY = newY;
        
        WinStopTimer( ab, win, 1 );
        
        if ( !record )
        {
          WinStartTimer( ab, win, 1, BUBBLE_POPUP_TIMEOUT );
          break;
        }
        
        rectQuery.cb = sizeof( QUERYRECORDRECT );
        rectQuery.fRightSplitWindow = 0;
        rectQuery.fsExtent = CMA_TREEICON | CMA_ICON | CMA_TEXT;
        rectQuery.pRecord = record;
        WinSendMsg( win, CM_QUERYRECORDRECT, MPFROMP( &recordRect ),
         MPFROMP( &rectQuery ) );
         
        if ( newX < recordRect.xLeft || newX > recordRect.xRight ||
              newY < recordRect.yBottom || newY > recordRect.yTop )
        {
          WinSetCapture( HWND_DESKTOP, NULLHANDLE );
          
          bubbleWin = WinQueryWindowULong( parentWin, 36 );
          if ( bubbleWin )
          {
            WinDestroyWindow( bubbleWin );
            WinSetWindowULong( parentWin, 36, 0 );
            WinSetWindowPtr( parentWin, 44, NULL );
          }
          WinStartTimer( ab, win, 1, BUBBLE_POPUP_TIMEOUT );
        }
      }
      WinSendMsg( parentWin, WM_NOTIDLE, 0, 0 );
    }
    break;
    case WM_CHAR:
      WinSendMsg( parentWin, WM_NOTIDLE, 0, 0 );
    break;
    case WM_DESTROY:
    {
      HWND bubbleWin = WinQueryWindowULong( parentWin, 36 );
      if ( bubbleWin ) WinDestroyWindow( bubbleWin );
    }
    break;
    case WM_BUTTON1DOWN:
    case WM_BUTTON2DOWN:
    case WM_BUTTON3DOWN:
    {
      HWND bubbleWin = WinQueryWindowULong( parentWin, 36 );
      if ( bubbleWin ) WinDestroyWindow( bubbleWin );
    }
    break;
    case WM_FOCUSCHANGE:
    {
      HWND bubbleWin = WinQueryWindowULong( parentWin, 36 );
      if ( bubbleWin && SHORT1FROMMP( mp2 ) == FALSE &&
            LONGFROMMP( mp1 ) != bubbleWin )
      {
        WinDestroyWindow( bubbleWin );
      }
    }
    break;
  }

  oldProc = (MRESULT EXPENTRY (*) ( HWND, ULONG, MPARAM, MPARAM ))
   WinQueryWindowPtr( parentWin, 32 );
  
  if ( oldProc )
    return oldProc( win, msg, mp1, mp2 );
  
  return NULL;
}

typedef struct
{
  int excludeTypes;
  Buddy **buddyList;
  int numBuddies;
} FilterInfo;

int APIENTRY buddyListFilter( RECORDCORE *record, FilterInfo *filterInf )
{
  int i, found;
  
  found = 0;
  
  for ( i=0; i<filterInf->numBuddies; ++i )
  {
    if ( !filterInf->buddyList[i] ) continue;
    
    if ( record && record == filterInf->buddyList[i]->record )
    {
      found = 1;
      
      if ( filterInf->buddyList[i]->isGroup ) return 1;
      // Never filter an entire group
      
      if ( (filterInf->excludeTypes & (2|8) ) &&
            filterInf->buddyList[i]->userData.isOnline )
      {
        // Exclude all online
        if ( !filterInf->buddyList[i]->animStep ||
              filterInf->buddyList[i]->oldFlashState != BUDDY_STATE_OFFLINE )
        {
          return 0;
        }
      }
      if ( (filterInf->excludeTypes & 1) &&
            !(filterInf->buddyList[i]->userData.isOnline) )
      {
        // Exclude all offline
        if ( !filterInf->buddyList[i]->animStep )
        {
          return 0;
        }
      }
      if ( (filterInf->excludeTypes & 4 ) &&
            ((filterInf->buddyList[i]->userData.userClass & 32) || 
             (filterInf->buddyList[i]->userData.userStatus & 1)) )
      {
        // Exclude all away
        if ( !filterInf->buddyList[i]->animStep )
        {
          return 0;
        }
      }
      break;
    }
  }
  
  return found;
}

static char *stripScreenName( char *screenName )
{
  char *retStr;
  int i, j, len;
  
  len = strlen( screenName );
  if ( !len ) return NULL;
  
  CHECKED_STRDUP( screenName, retStr );
  
  for ( i=0, j=0; i<len; ++i )
  {
    if ( screenName[i] != ' ' )
    {
      retStr[j] = tolower( screenName[i] );
      ++j;
    }
  }
  return retStr;
}

MRESULT EXPENTRY pmSingleEntryDlgProc( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
    {
      char **textLabels = (char **) PVOIDFROMMP( mp2 );
      RECTL desktop, rectl;
      
      WinSetWindowText( win, textLabels[0] );
      WinSetDlgItemText( win, MRM_ST_SingleEntryLabel, textLabels[1] );
      
      WinQueryWindowRect( HWND_DESKTOP, &desktop );
      WinQueryWindowRect( win, &rectl );
      
      WinSetWindowPos( win, 0, ((desktop.xRight - desktop.xLeft) / 2) -
       ((rectl.xRight - rectl.xLeft) / 2),
       ((desktop.yTop - desktop.yBottom) / 2) -
       ((rectl.yTop - rectl.yBottom) / 2), 0, 0, SWP_MOVE | SWP_SHOW );
    }
    break;
  }
  
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

static void uniquifySettingsStrings( sessionThreadSettings *theSettings )
{
  int i;
  
  if ( !theSettings->inheritSettings ) return;
  
  if ( theSettings->profile )
  {
    CHECKED_STRDUP( theSettings->profile, theSettings->profile );
  }
  
  if ( theSettings->awayMessage )
  {
    CHECKED_STRDUP( theSettings->awayMessage, theSettings->awayMessage );
  }
  
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    if ( theSettings->sounds[i] )
      CHECKED_STRDUP( theSettings->sounds[i], theSettings->sounds[i] );
    if ( theSettings->rexxScripts[i] )
      CHECKED_STRDUP( theSettings->rexxScripts[i], theSettings->rexxScripts[i] );
    if ( theSettings->shellCmds[i] )
      CHECKED_STRDUP( theSettings->shellCmds[i], theSettings->shellCmds[i] );
  }
  
  theSettings->inheritSettings = 0;
}

MRESULT EXPENTRY pmBuddyList( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_WINPPCHANGED:
    {
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      ULONG ppType = LONGFROMMP( mp2 );
      ULONG lenRet;
      char keyName[10];
      char buffer[1024];
      HWND theWin;
      // Presentation parameter changed on a subclassed child window
      
      if ( SHORT1FROMMP( mp1 ) )
      {
        theWin = WinWindowFromID( win, SHORT1FROMMP( mp1 ) );
      } else {
        theWin = win;
      }
      
      lenRet = WinQueryPresParam( theWin, ppType, 0, NULL, 1024, buffer, 0 );
      // No way to query PP size that I know of.  Goofy.
      
      if ( lenRet )
      {
        char *appName;
        
        CHECKED_MALLOC( strlen( theSession->getUserName() ) + 38, appName );
        
        sprintf( appName, "Buddy List [%s] Presentation Parameters",
         theSession->getUserName() );
        sprintf( keyName, "%d", ppType );
        
        PrfWriteProfileData( iniFile, appName, keyName, buffer, lenRet );
        
        CHECKED_FREE( appName );
      }
    }
    break;
    case WM_NOTIDLE:
      WinStopTimer( WinQueryAnchorBlock( win ), win, 3 );
      WinStartTimer( WinQueryAnchorBlock( win ), win, 3,
       IM_IDLE_TIMEOUT * 1000 );
      if ( WinQueryWindowULong( win, 60 ) )
      {
        OscarSession *theSession = (OscarSession *)
         WinQueryWindowPtr( win, 24 );
        OscarData *idleData = new OscarData( theSession->getSeqNumPointer() );
        
        idleData->prepareSetIdle( 0 );
        theSession->queueForSend( idleData, 5 );
        WinSetWindowULong( win, 60, 0 );
        debugf( "Re-activated after being idle.\n" );
      }
      // If we have reported idle-ness, report that we are active again.
    break;
    case WM_TIMER:
    {
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      Buddy **buddyList;
      Buddy *theBuddy;
      int i, numBuddies;
      
      // Timer ID 1 is reserved for the bubble help and is handled by the
      //  subclassed window procedure.
      
      if ( SHORT1FROMMP( mp1 ) == 2 )
      {
        // Initial timeout for initialization of entry status.
        
        WinStopTimer( WinQueryAnchorBlock( win ), win, 2 );
        DosRequestMutexSem( theSession->buddyListAccessMux,
         SEM_INDEFINITE_WAIT );
         
        buddyList = theSession->getBuddyList( &numBuddies );
        
        for ( i=0; i<numBuddies; ++i )
        {
          // Assume all records have been initialized after timeout so
          //  we can start playing audible notifications for users coming
          //  and going.
          theBuddy = buddyList[i];
          if ( theBuddy )
            theBuddy->userData.statusUninit = 0;
        }
        DosReleaseMutexSem( theSession->buddyListAccessMux );
      } else if ( SHORT1FROMMP( mp1 ) == 3 )
      {
        // Idle timeout.  Only have to report idle-ness once.  The server
        //  will keep posting updates to listening clients.
        
        WinStopTimer( WinQueryAnchorBlock( win ), win, 3 );
        
        if ( WinQueryWindowULong( win, 60 ) == 0 )
        {
          OscarData *idleData = new OscarData( theSession->getSeqNumPointer() );
          idleData->prepareSetIdle( IM_IDLE_TIMEOUT );
          theSession->queueForSend( idleData, 5 );
          WinSetWindowULong( win, 60, 1 );
          debugf( "Reporting idle status (idle for %d seconds).\n",
           IM_IDLE_TIMEOUT );
        } else {
          debugf( "Attempted to report idle status twice in a row.  Ignoring.\n" );
        }
      } else if ( SHORT1FROMMP( mp1 ) == 4 )
      {
        // Flashing changes for icon status
        
        HPOINTER offlineIco = WinQueryWindowULong( win, 28 );
        HPOINTER onlineIco = WinQueryWindowULong( win, 20 );
        HPOINTER hotOnlineIco = WinQueryWindowULong( win, 40 );
        HPOINTER awayIco = WinQueryWindowULong( win, 8 );
        unsigned char theState;
        
        DosRequestMutexSem( theSession->buddyListAccessMux,
         SEM_INDEFINITE_WAIT );
        
        buddyList = theSession->getBuddyList( &numBuddies );
        
        for ( i=0; i<numBuddies; ++i )
        {
          // Assume all records have been initialized after timeout so
          //  we can start playing audible notifications for users coming
          //  and going.
          theBuddy = buddyList[i];
        
          if ( !theBuddy || !theBuddy->record ) continue;
          // The PLEASEUPGRADE000 entry gets added to the buddy list but
          // doesn't have a record created for it in the container.
          
          if ( DosRequestMutexSem( WinQueryWindowULong( win, 80 ), 0 ) != 0 )
            break;
          // Operations on container are not currently safe... skip
          
          if ( theBuddy->animStep )
          {
            if ( theBuddy->animStep % 2 )
            {
              theState = theBuddy->flashState;
            } else {
              theState = theBuddy->oldFlashState;
            }
            
            switch ( theState )
            {
              case BUDDY_STATE_ONLINE:
                theBuddy->record->hptrIcon = onlineIco;
              break;
              case BUDDY_STATE_AWAY:
                theBuddy->record->hptrIcon = awayIco;
              break;
              case BUDDY_STATE_ACTIVE:
                theBuddy->record->hptrIcon = hotOnlineIco;
              break;
              case BUDDY_STATE_OFFLINE:
                theBuddy->record->hptrIcon = offlineIco;
              break;
            }
            theBuddy->animStep--;
            
            WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_INVALIDATERECORD,
             MPFROMP( &(theBuddy->record) ),
             MPFROM2SHORT( 1, CMA_NOREPOSITION ) );
            
            if ( !theBuddy->animStep )
            {
              FilterInfo theFilter;
              
              theSession->numFlashing--;
              if ( !theSession->numFlashing )
              {
                WinStopTimer( WinQueryAnchorBlock( win ), win, 4 );
              }
              
              theFilter.excludeTypes = WinQueryWindowULong( win, 48 );
              theFilter.buddyList =
               theSession->getBuddyList( &theFilter.numBuddies );
              
              WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
               MPFROMP( buddyListFilter ), MPFROMP( &theFilter ) );
            }
          }
          DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
        }
        DosReleaseMutexSem( theSession->buddyListAccessMux );
      }
    }
    break;
    case WM_REBOOTSESSION:
    {
      HWND menuWin = WinQueryWindowULong( win, 68 );
      HMODULE dll = getModuleHandle();
      HWND dlg;
      HWND *storeHere;
      HEV theSem;
      
      // mp1 = semaphore for creation of progress window
      // mp2 = pointer to HWND of progress window
      
      WinEnableWindow( WinWindowFromID( win, MRM_CN_BuddyContainer ), FALSE );
      
      WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
       MPFROMLONG( WinQueryWindowULong( win, 28 ) ), NULL );
      // Set buddy list window icon to unplugged
      
      dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmProgressDialog, dll,
       MRM_Progress, NULL );
      
      theSem = LONGFROMMP( mp1 );
      storeHere = (HWND *) PVOIDFROMMP( mp2 );
      *storeHere = dlg;
      DosPostEventSem( theSem );
    }
    break;
    case WM_SESSIONUP:
    {
      HWND menuWin = WinQueryWindowULong( win, 68 );
      HWND progressDlg = LONGFROMMP( mp1 );
      
      // mp1 = HWND of progress window (to close)
      
      WinEnableWindow( WinWindowFromID( win, MRM_CN_BuddyContainer ), TRUE );
      
      if ( !WinIsMenuItemChecked( menuWin, MRM_SessionStatus ) )
      {
        WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
         MPFROMLONG( WinQueryWindowULong( win, 92 ) ), NULL );
      } else {
        WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
         MPFROMLONG( WinQueryWindowULong( win, 96 ) ), NULL );
      }
      // Set buddy list window icon back to the appropriate status

      WinDestroyWindow( progressDlg );
    }
    break;
    case WM_IMRECEIVED:
    {
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      sessionThreadSettings *globalSettings = (sessionThreadSettings *)
       WinQueryWindowPtr( mgrWin, 60 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      char *receivedFrom = (char *) PVOIDFROMMP( mp1 );
      char *theMessage = (char *) PVOIDFROMMP( mp2 );
      char *convertedMessage, *annotatedMessage;
      Buddy **buddyList;
      Buddy *theBuddy;
      int newWin, numBuddies;
      HRTV viewer;
      HWND viewerWin;
      eventData theEventData;
      
      theBuddy = theSession->getBuddyFromName( receivedFrom );
      
      if ( !theBuddy )
      {
        // Received an IM from someone not on our buddy list.  Add them
        //  on the client side so we can track their status.
        OscarData *addBuddy = new OscarData( theSession->getSeqNumPointer() );
        buddyListEntry newBuddy;
        HPOINTER hotOnlineIco = WinQueryWindowULong( win, 40 );
        FilterInfo theFilter;
        
        theBuddy = (Buddy *) WinQueryWindowPtr( win, 88 );
        
        if ( !theBuddy )
        {
          // Create a group for orphans
          newBuddy.entryName = (char *)othersGroupName;
          newBuddy.gid = 0;
          newBuddy.id = 0;
          newBuddy.beenHere = 0;
          newBuddy.memberIDs = NULL;
          newBuddy.numMembers = 1;
          newBuddy.parentRecord = NULL;
          newBuddy.myRecord = NULL;
          theBuddy = theSession->createBuddy( &newBuddy, 1 );
          // Buddy list will be reallocated as a result of this call
          buddyList = theSession->getBuddyList( &numBuddies );
          WinSetWindowPtr( win, 88, theBuddy );
        }
        
        newBuddy.entryName = receivedFrom;
        newBuddy.parentRecord = theBuddy->record;
        // Group of orphaned buddies
        newBuddy.gid = 0;
        newBuddy.id = 0;
        newBuddy.beenHere = 0;
        newBuddy.memberIDs = NULL;
        newBuddy.numMembers = 0;
        newBuddy.myRecord = NULL;
        
        theBuddy = theSession->createBuddy( &newBuddy, 1 );
        // Create an entry for the buddy we received the IM from
        
        theBuddy->userData.statusUninit = 0;
        theBuddy->userData.isOnline = 1;
        // We know they're online since they just sent us an IM.  No need
        //  to wait for the AOL servers to tell us their status.  This will
        //  allow us to pop up the IM window immediately.
        
        theBuddy->record->hptrIcon = hotOnlineIco;
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_INVALIDATERECORD,
         MPFROMP( &(theBuddy->record) ),
         MPFROM2SHORT( 1, CMA_NOREPOSITION ) );
         
        buddyList = theSession->getBuddyList( &numBuddies );
        
        theFilter.excludeTypes = WinQueryWindowULong( win, 48 );
        theFilter.buddyList = buddyList;
        theFilter.numBuddies = numBuddies;
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
         MPFROMP( buddyListFilter ), MPFROMP( &theFilter ) );
        // Ensure that the user status is reflected in the window icon.
        //  This will not be the case (without this call) if someone who is on
        //  the *server* buddy list is removed, and then re-added.
        
        addBuddy->prepareAddClientSideBuddy( receivedFrom );
        theSession->queueForSend( addBuddy, 10 );
        // Tell the server to update us on the status of this buddy
      }
      
      newWin = 0;
      if ( !theBuddy->imChatWin )
      {
        // Chat window is not already up.
        NOTIFYRECORDENTER recordEnter;
        
        DosRequestMutexSem( WinQueryWindowULong( win, 80 ),
         SEM_INDEFINITE_WAIT );
        
        recordEnter.hwndCnr = WinWindowFromID( win, MRM_CN_BuddyContainer );
        recordEnter.fKey = TRUE;
        recordEnter.pRecord = theBuddy->record;
        
        if ( !theBuddy->userData.isOnline )
        {
          // Might be invisible.  Buddy is online, but looks offline.
          // Set the status to online so that we can pop up the window
          //  and allow messaging.  Temporary hack fix, because buddy
          //  status will probably not be updated to offline by the
          //  server when they go offline, so this will be a false
          //  reading on Mr. Message.
          theBuddy->userData.isOnline = 1;
        }
       
        WinSendMsg( win, WM_CONTROL, MPFROM2SHORT( MRM_CN_BuddyContainer,
         CN_ENTER ), MPFROMP( &recordEnter ) );
        
        if ( !theBuddy->imChatWin )
        {
          // Failed to open the window for some reason.
          WinAlarm( HWND_DESKTOP, WA_ERROR );
          debugf( "Failed to open buddy IM chat window for %s.\n",
           receivedFrom );
          break;
        }
        
        newWin = 1;
        
        DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
      }
      
      // Chat window is up, so populate it with the received message.
      
      convertedMessage = convertTagsToRTV( theMessage );
      
      debugf( "Converted message: %s\n", convertedMessage );
      
      if ( testSettingsFlag( SET_TIMESTAMPS_ENABLED, theSession->settings,
            globalSettings ) )
      {
        time_t utcTime;
        struct tm *localTime;
        
        CHECKED_MALLOC( strlen( convertedMessage ) +
         strlen( theBuddy->record->pszTree ) + 89, annotatedMessage );
        
        utcTime = time( NULL );
        localTime = localtime( &utcTime );
        
        sprintf( annotatedMessage, "<b><color blue>[%s</b> - %02d:%02d:%02d<b>]:</b><color black> <leftmargin here>%s<leftmargin 0>",
         theBuddy->record->pszTree, localTime->tm_hour, localTime->tm_min,
         localTime->tm_sec, convertedMessage );
      } else {
        CHECKED_MALLOC( strlen( convertedMessage ) +
         strlen( theBuddy->record->pszTree ) + 71, annotatedMessage );
        
        sprintf( annotatedMessage, "<b><color blue>[%s]:</b><color black> <leftmargin here>%s<leftmargin 0>",
         theBuddy->record->pszTree, convertedMessage );
      }
      
      theEventData.currentUser = theSession->getUserName();
      theEventData.otherUser = receivedFrom;
      theEventData.message = convertedMessage; 
      
      if ( handleMrMessageEvent( EVENT_POST_RECEIVE, theSession->settings,
            globalSettings, &theEventData, newWin ) &
           (EVENT_PROCESSED_SCRIPT | EVENT_PROCESSED_SHELL) )
      {
        // There was a script or shell program assigned to this event.
        // The intention of this event is to allow a shell or script to
        //  modify the message before the user sees it, so don't automatically
        //  display the message.  Wait for the script or program to do it
        //  through the appropriate interface (subcommand handler for script,
        //  named pipe for shell program).
        
        CHECKED_FREE( convertedMessage );
        CHECKED_FREE( annotatedMessage );
        CHECKED_FREE( receivedFrom );
        CHECKED_FREE( theMessage );
        break;
      }
      
      viewer = WinQueryWindowULong( theBuddy->imChatClientWin, 16 );
      ACLRTVAddParagraph( viewer, annotatedMessage );
      viewerWin = ACLRTVGetWindow( viewer );
      
      WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
       KC_VIRTUALKEY | KC_CTRL, 0 ), MPFROM2SHORT( 0, VK_END ) );
      WinPostMsg( viewerWin, WM_CHAR, MPFROM2SHORT(
       KC_VIRTUALKEY | KC_KEYUP | KC_CTRL, 0 ), MPFROM2SHORT( 0, VK_END ) );
      // Emulate pressing CTRL-END to scroll to the bottom of the viewer
      
      theEventData.currentUser = theSession->getUserName();
      theEventData.otherUser = receivedFrom;
      theEventData.message = convertedMessage; 
      handleMrMessageEvent( EVENT_RECEIVED, theSession->settings,
       globalSettings, &theEventData, newWin );
      // Set noSound to whether or not this is a new window because we don't
      //  want the "first time" sound playing over the "message arrived" sound,
      //  in case there are old audio drivers which can't do stream mixing.
      
      CHECKED_FREE( convertedMessage );
      CHECKED_FREE( annotatedMessage );
      CHECKED_FREE( receivedFrom );
      CHECKED_FREE( theMessage );
      
      WinSendMsg( theBuddy->imChatClientWin, WM_TYPING_NOTIFY,
       MPFROMLONG( 0 ), NULL );
      // Reset typing notification to "nothing typed" after receiving an IM
    }
    break;
    case WM_REGISTERWAKEUPSEM:
      WinSetWindowULong( win, 4, LONGFROMMP( mp1 ) );
      debugf( "Session wakeup semaphore was registered.\n" );
    break;
    case WM_ADDBUDDY:
    {
      RECORDINSERT recordInsert;
      buddyListEntry *buddyInfo = (buddyListEntry *) PVOIDFROMMP( mp1 );
      HEV wakeupSem = LONGFROMMP( mp2 );
      HPOINTER offlineIco = WinQueryWindowULong( win, 28 );
      HPOINTER folder = WinQueryWindowULong( win, 12 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      Buddy *thisBuddy = (Buddy *)buddyInfo->theBuddyPtr;
      FilterInfo theFilter;
      char *appName, *strippedSN;
      ULONG length;
      
      debugf( "Adding buddy: %s\n", buddyInfo->entryName );
      
      DosRequestMutexSem( WinQueryWindowULong( win, 80 ), SEM_INDEFINITE_WAIT );
      
      buddyInfo->myRecord = (RECORDCORE *) PVOIDFROMMR( WinSendDlgItemMsg( win,
       MRM_CN_BuddyContainer, CM_ALLOCRECORD, MPFROMLONG( 0 ),
       MPFROMSHORT( 1 ) ) );
      ((Buddy *)(buddyInfo->theBuddyPtr))->record = buddyInfo->myRecord;
       
      buddyInfo->myRecord->flRecordAttr = CRA_EXPANDED;
      buddyInfo->myRecord->ptlIcon.x = 0;
      buddyInfo->myRecord->ptlIcon.y = 0;
      buddyInfo->myRecord->hptrMiniIcon = 0;
      buddyInfo->myRecord->hbmMiniBitmap = 0;
      buddyInfo->myRecord->hptrIcon = 0;
      buddyInfo->myRecord->hbmBitmap = 0;
      
      if ( buddyInfo->numMembers )
      {
        // This is a group, not a buddy.
        buddyInfo->myRecord->hptrIcon = folder;
      } else {
        buddyInfo->myRecord->hptrIcon = offlineIco;
      }
      
      CHECKED_MALLOC( strlen( theSession->getUserName() ) + 9, appName );
      sprintf( appName, "%s Aliases", theSession->getUserName() );
      
      strippedSN = stripScreenName( buddyInfo->entryName );
      
      if ( !strippedSN )
      {
        length = 0;
      } else {
        PrfQueryProfileSize( iniFile, appName, strippedSN, &length );
      }
      
      if ( length )
      {
        CHECKED_MALLOC( length, thisBuddy->alias );
        PrfQueryProfileData( iniFile, appName, strippedSN,
         thisBuddy->alias, &length );
        buddyInfo->myRecord->pszTree = thisBuddy->alias;
      } else {
        buddyInfo->myRecord->pszTree = thisBuddy->userData.screenName;
      }
      
      CHECKED_FREE( strippedSN );
      CHECKED_FREE( appName );
      
      buddyInfo->myRecord->pTreeItemDesc = NULL;
      
      recordInsert.cb = sizeof( RECORDINSERT );
      recordInsert.pRecordOrder = (RECORDCORE *)CMA_END;
      recordInsert.pRecordParent = buddyInfo->parentRecord;
      recordInsert.fInvalidateRecord = TRUE;
      recordInsert.zOrder = CMA_TOP;
      recordInsert.cRecordsInsert = 1;
      
      WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_INSERTRECORD,
       MPFROMP( buddyInfo->myRecord ), MPFROMP( &recordInsert ) );
      
      DosRequestMutexSem( theSession->buddyListAccessMux, SEM_INDEFINITE_WAIT );
      
      theFilter.excludeTypes = WinQueryWindowULong( win, 48 );
      theFilter.buddyList = theSession->getBuddyList( &theFilter.numBuddies );
      
      if ( theFilter.excludeTypes )
      {
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
         MPFROMP( buddyListFilter ), MPFROMP( &theFilter ) );
      } else {
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER, NULL, NULL );
      }
      DosReleaseMutexSem( theSession->buddyListAccessMux );
      DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
      if ( wakeupSem ) DosPostEventSem( wakeupSem );
    }
    break;
    case WM_BUDDYSTATUS:
    {
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      sessionThreadSettings *globalSettings = (sessionThreadSettings *)
       WinQueryWindowPtr( mgrWin, 60 );
      UserInformation *userInfo = (UserInformation *) PVOIDFROMMP( mp1 );
      RECORDCORE *record;
      HPOINTER offlineIco = WinQueryWindowULong( win, 28 );
      HPOINTER onlineIco = WinQueryWindowULong( win, 20 );
      HPOINTER hotOnlineIco = WinQueryWindowULong( win, 40 );
      HPOINTER awayIco = WinQueryWindowULong( win, 8 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      Buddy **buddyList;
      char *formattedName = NULL;
      int numBuddies, i, found, retry;
      unsigned char statusChange = 0, idleChange = 0, statusUninit = 0,
       awayChange = 0;
      FilterInfo theFilter;
      
      theFilter.excludeTypes = WinQueryWindowULong( win, 48 );
      
      DosRequestMutexSem( theSession->buddyListAccessMux, SEM_INDEFINITE_WAIT );
      buddyList = theSession->getBuddyList( &numBuddies );
      theFilter.buddyList = buddyList;
      theFilter.numBuddies = numBuddies;
      
      found = 0;
      retry = 0;
      
      DosRequestMutexSem( WinQueryWindowULong( win, 80 ), SEM_INDEFINITE_WAIT );
      
      lookForReformattedSN:
      
      for ( i=0; i<numBuddies; ++i )
      {
        if ( !buddyList[i] ) continue;
        record = buddyList[i]->record;
        if ( !record ) continue;
        if ( stricmp( userInfo->screenName,
              buddyList[i]->userData.screenName ) == 0 )
        {
          if ( strcmp( userInfo->screenName,
                buddyList[i]->userData.screenName ) != 0 )
          {
            // Capitalization changes.  Update the buddy list window if needed.
            strcpy( buddyList[i]->userData.screenName, userInfo->screenName );
            if ( buddyList[i]->record->pszTree ==
                  buddyList[i]->userData.screenName )
            {
              WinSendDlgItemMsg( win, MRM_CN_BuddyContainer,
               CM_INVALIDATERECORD, MPFROMP( &record ),
               MPFROM2SHORT( 1, CMA_TEXTCHANGED ) );
            }
          }
          
          if ( userInfo->isOnline != buddyList[i]->userData.isOnline )
            statusChange = 1;
          if ( userInfo->userStatus != buddyList[i]->userData.userStatus ||
                userInfo->userClass != buddyList[i]->userData.userClass )
            awayChange = 1;
          if ( userInfo->idleMinutes != buddyList[i]->userData.idleMinutes )
            idleChange = 1;
          if ( buddyList[i]->userData.statusUninit )
            statusUninit = 1;
          
          if ( statusChange || awayChange || idleChange )
          {
            buddyList[i]->userData.incorporateData( *userInfo );
            if ( userInfo->isOnline )
            {
              if ( (userInfo->userClass & 32) || 
                    (userInfo->userStatus & 1) )
              {
                debugf( "%s is away.\n", record->pszTree );
                record->hptrIcon = awayIco;
                
                buddyList[i]->oldFlashState = buddyList[i]->flashState;
                buddyList[i]->flashState = BUDDY_STATE_AWAY;
                
                if ( awayChange )
                {
                  OscarData *awayReq =
                   new OscarData( theSession->getSeqNumPointer() );
                  awayReq->prepareRequestUserInfo(
                   buddyList[i]->userData.screenName, 3 );
                  theSession->queueForSend( awayReq, 0 );
                }
                if ( statusChange )
                {
                  // User went from off-line directly to "away".
                  // Technically, this is an arrival, so inform the user.
                  
                  if ( !statusUninit )
                  {
                    eventData theEventData;
                    theEventData.currentUser = theSession->getUserName();
                    theEventData.otherUser = buddyList[i]->userData.screenName;
                    theEventData.message = "ONLINE";
                    
                    handleMrMessageEvent( EVENT_ARRIVE, theSession->settings,
                     globalSettings, &theEventData, 0 );
                  }
                }
              } else {
                debugf( "%s is on line.\n", record->pszTree );
                record->hptrIcon = onlineIco;
                
                buddyList[i]->oldFlashState = buddyList[i]->flashState;
                buddyList[i]->flashState = BUDDY_STATE_ONLINE;
                
                if ( buddyList[i]->userData.idleMinutes < 10 )
                {
                  debugf( "%s has been active in the last 10 minutes.\n",
                   record->pszTree );
                  record->hptrIcon = hotOnlineIco;
                  buddyList[i]->flashState = BUDDY_STATE_ACTIVE;
                }
                if ( statusChange || awayChange )
                {
                  OscarData *profileReq =
                   new OscarData( theSession->getSeqNumPointer() );
                  profileReq->prepareRequestUserInfo(
                   buddyList[i]->userData.screenName, 1 );
                  theSession->queueForSend( profileReq, 0 );
                  
                  if ( !statusUninit && statusChange )
                  {
                    eventData theEventData;
                    theEventData.currentUser = theSession->getUserName();
                    theEventData.otherUser = buddyList[i]->userData.screenName;
                    theEventData.message = "ONLINE";
                    
                    handleMrMessageEvent( EVENT_ARRIVE, theSession->settings,
                     globalSettings, &theEventData, 0 );
                  }
                }
              }
              if ( buddyList[i]->imChatClientWin )
              {
                WinSendMsg( buddyList[i]->imChatClientWin, WM_BUDDYSTATUS,
                 MPFROMLONG( 1 ), NULL );
                // Same window message used elsewhere, but different params.
                //  1 indicates that the user is at least connected to AOL.
              }
            } else {
              debugf( "%s is off line.\n", record->pszTree );
              record->hptrIcon = offlineIco;
              
              buddyList[i]->oldFlashState = buddyList[i]->flashState;
              buddyList[i]->flashState = BUDDY_STATE_OFFLINE;
              
              if ( !statusUninit )
              {
                eventData theEventData;
                theEventData.currentUser = theSession->getUserName();
                theEventData.otherUser = buddyList[i]->userData.screenName;
                theEventData.message = "OFFLINE";
                
                handleMrMessageEvent( EVENT_LEAVE, theSession->settings,
                 globalSettings, &theEventData, 0 );
              }
              
              if ( buddyList[i]->imChatClientWin )
              {
                WinSendMsg( buddyList[i]->imChatClientWin, WM_BUDDYSTATUS,
                 MPFROMLONG( 0 ), NULL );
                // Same window message used elsewhere, but different params.
                //  0 indicates that the user is offline.
              }
            }
            
            if ( buddyList[i]->flashState != buddyList[i]->oldFlashState )
            {
              if ( !buddyList[i]->animStep )
              {
                theSession->numFlashing++;
              }
              buddyList[i]->animStep = 8;
              
              WinStartTimer( WinQueryAnchorBlock( win ), win, 4, 500 );
            }
          } else {
            buddyList[i]->userData.incorporateData( *userInfo );
            debugf( "%s user status has not changed.\n", record->pszTree );
          }
          
          WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_INVALIDATERECORD,
           MPFROMP( &record ), MPFROM2SHORT( 1, CMA_NOREPOSITION ) );
          
          if ( buddyList[i]->imChatWin )
          {
            WinSetWindowULong( buddyList[i]->imChatClientWin, 36,
             record->hptrIcon );
            
            if ( WinQueryWindowULong( buddyList[i]->imChatClientWin, 40 ) == 0 )
              WinSendMsg( buddyList[i]->imChatWin, WM_SETICON,
               MPFROMLONG( record->hptrIcon ), NULL );
          }
          found = 1;
        }
      }  
      
      if ( !found && !retry )
      {
        int len;
        int cpos = 0;
        
        formattedName = userInfo->screenName;
        len = strlen( formattedName );
        
        CHECKED_MALLOC( len + 1, userInfo->screenName );
        
        for ( i=0; i<len; ++i )
        {
          if ( formattedName[i] != ' ' )
          {
            userInfo->screenName[cpos] = formattedName[i];
            cpos++;
          }
        }
        userInfo->screenName[cpos] = 0;
        retry = 1;
        
        goto lookForReformattedSN;
      } else if ( retry )
      {
        if ( found )
        {
          for ( i=0; i<numBuddies; ++i )
          {
            if ( !buddyList[i] ) continue;
            record = buddyList[i]->record;
            if ( !record ) continue;
            if ( stricmp( userInfo->screenName,
                  buddyList[i]->userData.screenName ) == 0 )
            {
              if ( record->pszTree != buddyList[i]->alias )
              {
                record->pszTree = formattedName;
                WinSendDlgItemMsg( win, MRM_CN_BuddyContainer,
                 CM_INVALIDATERECORD, MPFROMP( &record ),
                 MPFROM2SHORT( 1, CMA_TEXTCHANGED ) );
              }
              CHECKED_FREE( buddyList[i]->userData.screenName );
              buddyList[i]->userData.screenName = formattedName;
            }
          }
          debugf( "Screen name '%s' was updated to be '%s'.\n",
           userInfo->screenName, formattedName );
        } else {
          CHECKED_FREE( userInfo->screenName );
          userInfo->screenName = formattedName;
          debugf( "Received user status for %s (not in buddy list).\n",
           formattedName );
        }
      }
      if ( found )
      {
        if ( theFilter.excludeTypes )
        {
          WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
           MPFROMP( buddyListFilter ), MPFROMP( &theFilter ) );
        } else {
          WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
           NULL, NULL );
        }
      }
      
      DosReleaseMutexSem( theSession->buddyListAccessMux );
      DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
      delete userInfo;
    }
    break;
    case WM_SESSIONPOSTINIT:
    {
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      char *appName, *iniData, *origData;
      ULONG dataLen;
      OscarData *addBuddy;
      buddyListEntry newBuddy;
      Buddy *localBuddyGroup;
      
      dataLen = 0;
      CHECKED_MALLOC( strlen( theSession->getUserName() ) + 14, appName );
      sprintf( appName, "Buddy List [%s]", theSession->getUserName() );
      PrfQueryProfileSize( iniFile, appName, "Local buddy list additions",
       &dataLen );
      
      if ( dataLen )
      {
        newBuddy.entryName = (char *)localBuddyGroupName;
        newBuddy.gid = 0;
        newBuddy.id = 0;
        newBuddy.beenHere = 0;
        newBuddy.memberIDs = NULL;
        newBuddy.numMembers = 1;  // Signify that it's a group
        newBuddy.parentRecord = NULL;
        newBuddy.myRecord = NULL;
        localBuddyGroup = theSession->createBuddy( &newBuddy, 1 );
        WinSetWindowPtr( win, 84, localBuddyGroup );
        
        CHECKED_MALLOC( dataLen, iniData );
        
        PrfQueryProfileData( iniFile, appName, "Local buddy list additions",
         iniData, &dataLen );
        
        origData = iniData;
        
        while ( *iniData )
        {
          Buddy *theNewBuddy;
          
          newBuddy.entryName = iniData;
          newBuddy.gid = 0;
          newBuddy.id = 0;
          newBuddy.beenHere = 0;
          newBuddy.memberIDs = NULL;
          newBuddy.numMembers = 0;
          newBuddy.parentRecord = localBuddyGroup->record;
          newBuddy.myRecord = NULL;
          theNewBuddy = theSession->createBuddy( &newBuddy, 1 );
          
          if ( theNewBuddy )
          {
            addBuddy = new OscarData( theSession->getSeqNumPointer() );
            addBuddy->prepareAddClientSideBuddy( iniData );
            theSession->queueForSend( addBuddy, 10 );
          }
          
          iniData += strlen( iniData ) + 1;
        }
        CHECKED_FREE( origData );
      }
      CHECKED_FREE( appName );
    }
    break;
    case WM_CREATE:
    {
      HWND theWin, popupMenu;
      CNRINFO cnrInfo;
      HMODULE dll = getModuleHandle();
      HPOINTER onlineIco = WinLoadPointer( HWND_DESKTOP, dll, 3 );
      HPOINTER awayIco = WinLoadPointer( HWND_DESKTOP, dll, 4 );
      HPOINTER offlineIco = WinLoadPointer( HWND_DESKTOP, dll, 5 );
      HPOINTER folder = WinLoadPointer( HWND_DESKTOP, dll, 2 );
      HPOINTER hotOnlineIco = WinLoadPointer( HWND_DESKTOP, dll, 6 );
      HPOINTER meHere = WinLoadPointer( HWND_DESKTOP, dll, 1 );
      HPOINTER meAway = WinLoadPointer( HWND_DESKTOP, dll, 8 );
      HPOINTER onlineFilteredIco = WinLoadPointer( HWND_DESKTOP, dll, 9 );
      HPOINTER awayFilteredIco = WinLoadPointer( HWND_DESKTOP, dll, 10 );
      HPOINTER offlineFilteredIco = WinLoadPointer( HWND_DESKTOP, dll, 11 );
      BTNCDATA ctrlData;
      PFNWP oldFunc;
      HMTX containerAccess;
      buttonHelpInfo *floatingHelpInfo;
      
      DosCreateMutexSem( NULL, &containerAccess, 0, FALSE );
      WinSetWindowULong( win, 80, containerAccess );
      
      WinStartTimer( WinQueryAnchorBlock( win ), win, 2, 10000 );
      // Timeout to assume that all initial user updates have arrived to
      //  start audible notifications.
      
      WinSetWindowULong( win, 28, offlineIco );
      WinSetWindowULong( win, 12, folder );
      WinSetWindowULong( win, 20, onlineIco );
      WinSetWindowULong( win, 8, awayIco );
      WinSetWindowULong( win, 32, 0 );
      WinSetWindowULong( win, 36, 0 );
      WinSetWindowULong( win, 40, hotOnlineIco );
      WinSetWindowULong( win, 48, 0 );
      WinSetWindowULong( win, 72, 0 );
      WinSetWindowULong( win, 84, 0 );
      WinSetWindowULong( win, 88, 0 );
      WinSetWindowULong( win, 92, meHere );
      WinSetWindowULong( win, 96, meAway );
      WinSetWindowULong( win, 100, onlineFilteredIco );
      WinSetWindowULong( win, 104, awayFilteredIco );
      WinSetWindowULong( win, 108, offlineFilteredIco );
      
      popupMenu = WinLoadMenu( HWND_DESKTOP, dll, MRM_BuddyPopup );
      WinSetWindowULong( win, 64, popupMenu );
      
      popupMenu = WinLoadMenu( HWND_DESKTOP, dll, MRM_BuddyListPopup );
      WinSetWindowULong( win, 68, popupMenu );
      
      theWin = WinCreateWindow( win, WC_CONTAINER, "Mr. Message Buddy List",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | CCS_AUTOPOSITION | CCS_EXTENDSEL |
       CCS_MINIICONS, 0, 0, 0, 0, win, HWND_TOP, MRM_CN_BuddyContainer, NULL,
       NULL );

      oldFunc = WinSubclassWindow( theWin, pmPresParamNotifier2 );
      WinSetWindowPtr( win, 76, oldFunc );
      
      cnrInfo.cb = sizeof( CNRINFO );
      cnrInfo.pSortRecord = NULL;
      cnrInfo.pFieldInfoLast = NULL;
      cnrInfo.pFieldInfoObject = NULL;
      cnrInfo.pszCnrTitle = NULL;
      cnrInfo.flWindowAttr = CV_TREE | CV_ICON | CV_MINI | CA_DRAWICON |
       CA_TREELINE;
      cnrInfo.ptlOrigin.x = 0;
      cnrInfo.ptlOrigin.y = 0;
      cnrInfo.cDelta = 0;
      cnrInfo.cRecords = 0;
      cnrInfo.slBitmapOrIcon.cx = 0;
      cnrInfo.slBitmapOrIcon.cy = 0;
      cnrInfo.slTreeBitmapOrIcon.cx = 0;
      cnrInfo.slTreeBitmapOrIcon.cy = 0;
      cnrInfo.hbmExpanded = 0;
      cnrInfo.hbmCollapsed = 0;
      cnrInfo.hptrExpanded = 0;
      cnrInfo.hptrCollapsed = 0;
      cnrInfo.cyLineSpacing = -1;
      cnrInfo.cxTreeIndent = -1;
      cnrInfo.cxTreeLine = -1;
      cnrInfo.cFields = 0;
      cnrInfo.xVertSplitbar = -1;
      WinSendMsg( theWin, CM_SETCNRINFO, MPFROMP( &cnrInfo ),
       MPFROMLONG( CMA_FLWINDOWATTR ) );
      
      WinSetWindowPtr( win, 32, WinSubclassWindow( theWin,
       pmBubbleHelpCapableWindow ) );
      
      ctrlData.cb = sizeof( BTNCDATA );
      ctrlData.fsCheckState = 0;
      ctrlData.fsHiliteState = 0;
      ctrlData.hImage = onlineIco;
      
      theWin = WinCreateWindow( win, WC_BUTTON, "",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON | BS_MINIICON |
        BS_AUTOSIZE, 0, 0, 0, 0, win, HWND_TOP, MRM_PB_FilterOnline,
        &ctrlData, NULL );
      CHECKED_MALLOC( sizeof( buttonHelpInfo ), floatingHelpInfo );
      WinSetWindowPtr( theWin, QWL_USER, floatingHelpInfo );
      floatingHelpInfo->buttonType = 1;
      floatingHelpInfo->bubbleWin = 0;
      floatingHelpInfo->oldWindowProc = WinSubclassWindow( theWin,
       pmBubbleHelpCapableButton );
      
      ctrlData.hImage = awayIco;
      theWin = WinCreateWindow( win, WC_BUTTON, "",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON | BS_MINIICON |
        BS_AUTOSIZE, 0, 0, 0, 0, win, HWND_TOP, MRM_PB_FilterAway,
        &ctrlData, NULL );
      CHECKED_MALLOC( sizeof( buttonHelpInfo ), floatingHelpInfo );
      WinSetWindowPtr( theWin, QWL_USER, floatingHelpInfo );
      floatingHelpInfo->buttonType = 2;
      floatingHelpInfo->bubbleWin = 0;
      floatingHelpInfo->oldWindowProc = WinSubclassWindow( theWin,
       pmBubbleHelpCapableButton );
      
      ctrlData.hImage = offlineIco;
      theWin = WinCreateWindow( win, WC_BUTTON, "",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON | BS_MINIICON |
        BS_AUTOSIZE, 0, 0, 0, 0, win, HWND_TOP, MRM_PB_FilterOffline,
        &ctrlData, NULL );
      CHECKED_MALLOC( sizeof( buttonHelpInfo ), floatingHelpInfo );
      WinSetWindowPtr( theWin, QWL_USER, floatingHelpInfo );
      floatingHelpInfo->buttonType = 3;
      floatingHelpInfo->bubbleWin = 0;
      floatingHelpInfo->oldWindowProc = WinSubclassWindow( theWin,
       pmBubbleHelpCapableButton );
    }
    break;
    case WM_COMMAND:
    {
      HMODULE dll = getModuleHandle();
      int filterTypes = WinQueryWindowULong( win, 48 );
      HPOINTER onlineIco = WinQueryWindowULong( win, 20 );
      HPOINTER awayIco = WinQueryWindowULong( win, 8 );
      HPOINTER offlineIco = WinQueryWindowULong( win, 28 );
      HPOINTER hotOnlineIco = WinQueryWindowULong( win, 40 );
      HPOINTER onlineFilteredIco = WinQueryWindowULong( win, 100 );
      HPOINTER awayFilteredIco = WinQueryWindowULong( win, 104 );
      HPOINTER offlineFilteredIco = WinQueryWindowULong( win, 108 );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      BTNCDATA ctrlData;
      WNDPARAMS wndParms;
      
      ctrlData.cb = sizeof( BTNCDATA );
      ctrlData.fsCheckState = 0;
      ctrlData.fsHiliteState = 0;
      wndParms.fsStatus = WPM_CBCTLDATA | WPM_CTLDATA;
      wndParms.cbCtlData = sizeof( BTNCDATA );
      wndParms.pCtlData = &ctrlData;
      
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case MRM_PB_FilterOnline:
          filterTypes = filterTypes ^ (2|8);
          if ( filterTypes & (2|8) )
          {
            ctrlData.hImage = onlineFilteredIco;
          } else {
            ctrlData.hImage = onlineIco;
          }
          WinSendDlgItemMsg( win, MRM_PB_FilterOnline, WM_SETWINDOWPARAMS,
           MPFROMP( &wndParms ), NULL );
          WinSetWindowULong( win, 48, filterTypes );
        break;
        case MRM_PB_FilterAway:
          filterTypes = filterTypes ^ 4;
          if ( filterTypes & 4 )
          {
            ctrlData.hImage = awayFilteredIco;
          } else {
            ctrlData.hImage = awayIco;
          }
          WinSendDlgItemMsg( win, MRM_PB_FilterAway, WM_SETWINDOWPARAMS,
           MPFROMP( &wndParms ), NULL );
          WinSetWindowULong( win, 48, filterTypes );
        break;
        case MRM_PB_FilterOffline:
          filterTypes = filterTypes ^ 1;
          if ( filterTypes & 1 )
          {
            ctrlData.hImage = offlineFilteredIco;
          } else {
            ctrlData.hImage = offlineIco;
          }
          WinSendDlgItemMsg( win, MRM_PB_FilterOffline, WM_SETWINDOWPARAMS,
           MPFROMP( &wndParms ), NULL );
          WinSetWindowULong( win, 48, filterTypes );
        break;
        case MRM_SessionSettings:
          // From context menu of container background
          WinSetWindowPtr( mgrWin, 64, WinQueryWindowPtr( win, 0 ) );
          // Set the "selected record" override and do the normal processing
          WinSendMsg( mgrWin, WM_COMMAND, mp1, MPFROM2SHORT(
           SHORT1FROMMP( mp2 ) & ~CMDSRC_MENU, SHORT2FROMMP( mp2 ) ) );
        break;
        case MRM_SendMessage:
        {
          NOTIFYRECORDENTER notify;
          
          DosRequestMutexSem( WinQueryWindowULong( win, 80 ),
           SEM_INDEFINITE_WAIT );
          notify.pRecord = (RECORDCORE *) WinQueryWindowPtr( win, 72 );
          WinSendMsg( win, WM_CONTROL, MPFROM2SHORT( MRM_CN_BuddyContainer,
           CN_ENTER ), MPFROMP( &notify ) );
          DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
        }
        break;
        case MRM_SetAlias:
        {
          CNREDITDATA editData = {0};
          editData.cb = sizeof( CNREDITDATA );
          editData.pRecord = (RECORDCORE *) WinQueryWindowPtr( win, 72 );
          editData.id = MRM_CN_BuddyContainer;
          editData.hwndCnr = WinWindowFromID( win, MRM_CN_BuddyContainer );
          WinPostMsg( WinWindowFromID( win, MRM_CN_BuddyContainer ),
           CM_OPENEDIT, MPFROMP( &editData ), NULL );
        }
        break;
        case MRM_RemoveBuddy:
        {
          int numBuddies, i, j;
          RECORDCORE *record = (RECORDCORE *) WinQueryWindowPtr( win, 72 );
          Buddy **buddyList;
          HWND mgrWin = WinQueryWindowULong( win, 16 );
          HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
          
          DosRequestMutexSem( theSession->buddyListAccessMux,
           SEM_INDEFINITE_WAIT );
          buddyList = theSession->getBuddyList( &numBuddies );
          
          for ( i=0; i<numBuddies; ++i )
          {
            if ( !buddyList[i] || !buddyList[i]->record ) continue;
            
            if ( buddyList[i]->record == record )
            {
              if ( buddyList[i]->isGroup )
              {
                if ( WinMessageBox( HWND_DESKTOP, win,
                      "Are you sure you want to delete this entire group from your buddy list?",
                      "Delete a buddy group...", 0,
                      MB_YESNO | MB_QUERY | MB_DEFBUTTON2 | MB_APPLMODAL |
                      MB_MOVEABLE ) == MBID_YES )
                {
                  RECORDCORE *childRec = (RECORDCORE *) PVOIDFROMMR(
                   WinSendDlgItemMsg( win, MRM_CN_BuddyContainer,
                    CM_QUERYRECORD, MPFROMP( record ),
                    MPFROM2SHORT( CMA_LASTCHILD, CMA_ITEMORDER ) ) );
                  
                  while ( childRec )
                  {
                    for ( j=0; j<numBuddies; ++j )
                    {
                      if ( buddyList[j] && buddyList[j]->record == childRec )
                      {
                        WinSetWindowPtr( win, 72, buddyList[j]->record );
                        WinSendMsg( win, WM_COMMAND,
                         MPFROMSHORT( MRM_RemoveBuddy ), NULL );
                        // Recurse into this window message in case there is
                        //  recursion in the tree.
                        break;
                      }
                    }
                    
                    childRec = (RECORDCORE *) PVOIDFROMMR(
                     WinSendDlgItemMsg( win, MRM_CN_BuddyContainer,
                      CM_QUERYRECORD, MPFROMP( record ),
                      MPFROM2SHORT( CMA_LASTCHILD, CMA_ITEMORDER ) ) );
                  }
                } else break;
              }
              
              if ( !buddyList[i]->onServer )
              {
                OscarData *removeRequest =
                 new OscarData( theSession->getSeqNumPointer() );
                
                removeRequest->prepareRemoveClientSideBuddy(
                 buddyList[i]->userData.screenName );
                theSession->queueForSend( removeRequest, 10 );
              }
              // Tell the server to stop giving us updates on this user
              
              if ( buddyList[i] == WinQueryWindowPtr( win, 84 ) )
                WinSetWindowPtr( win, 84, NULL );
              
              if ( buddyList[i] == WinQueryWindowPtr( win, 88 ) )
                WinSetWindowPtr( win, 88, NULL );
              
              delete buddyList[i];
              buddyList[i] = NULL;
              
              WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_REMOVERECORD,
               MPFROMP( &record ), MPFROM2SHORT( 1, CMA_FREE | CMA_INVALIDATE ) );
              
              saveLocalBuddyList( theSession, iniFile );
              
              break;
            }
          }
          
          DosReleaseMutexSem( theSession->buddyListAccessMux );
        }
        break;
        case MRM_ProfileRefresh:
        {
          int requestType, numBuddies, i;
          OscarData *messageReq;
          RECORDCORE *record = (RECORDCORE *) WinQueryWindowPtr( win, 72 );
          Buddy **buddyList;
          
          DosRequestMutexSem( theSession->buddyListAccessMux,
           SEM_INDEFINITE_WAIT );
          buddyList = theSession->getBuddyList( &numBuddies );
          
          for ( i=0; i<numBuddies; ++i )
          {
            if ( !buddyList[i] || !buddyList[i]->record ) continue;
            
            if ( buddyList[i]->record == record || !record )
            {
              if ( !buddyList[i]->userData.isOnline ) continue;
              
              if ( (buddyList[i]->userData.userClass & 32) || 
                    (buddyList[i]->userData.userStatus & 1) )
              {
                requestType = 3;
              } else {
                requestType = 1;
              }
              messageReq = new OscarData( theSession->getSeqNumPointer() );
              messageReq->prepareRequestUserInfo(
               buddyList[i]->userData.screenName, requestType );
              theSession->queueForSend( messageReq, 0 );
              if ( record ) break;
            }
          }
          DosReleaseMutexSem( theSession->buddyListAccessMux );
        }
        break;
        case MRM_SessionStatus:
        {
          HWND menuWin = WinQueryWindowULong( win, 68 );
          OscarData *awayStatus =
           new OscarData( theSession->getSeqNumPointer() );
          
          if ( WinIsMenuItemChecked( menuWin, MRM_SessionStatus ) )
          {
            WinCheckMenuItem( menuWin, MRM_SessionStatus, FALSE );
            awayStatus->prepareSetClientAway( NULL );
            debugf( "Returning from \"Away\" status.\n" );
            WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
             MPFROMLONG( WinQueryWindowULong( win, 92 ) ), NULL );
          } else {
            WinCheckMenuItem( menuWin, MRM_SessionStatus, TRUE );
            if ( theSession->settings->awayMessage )
            {
              awayStatus->prepareSetClientAway(
               theSession->settings->awayMessage );
            } else {
              awayStatus->prepareSetClientAway(
               "I am currently away from my computer." );
            }
            debugf( "Setting \"Away\" status and message.\n" );
            WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
             MPFROMLONG( WinQueryWindowULong( win, 96 ) ), NULL );
          }
          theSession->queueForSend( awayStatus, 5 );
        }
        break;
        case MRM_NewBuddy:
        {
          char *textLabels[2] = {
           "Add a buddy to the buddy list...",
           "Enter the screen name of the buddy you wish to add to the buddy list."
          };
          HWND mgrWin = WinQueryWindowULong( win, 16 );
          HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
          int len;
          char *buddyName;
          HWND dlg;
          buddyListEntry newBuddy;
          OscarData *addBuddy;
          Buddy *theBuddy;
          
          dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmSingleEntryDlgProc,
           dll, MRM_SingleEntryWindow, &(textLabels) );
          len = 0;
          
          if ( !dlg )
          {
            WinAlarm( HWND_DESKTOP, WA_ERROR );
            break;
          }
          
          while ( !len )
          {
            if ( WinProcessDlg( dlg ) == DID_CANCEL )
            {
              WinDestroyWindow( dlg );
              return NULL;
            }
            
            len = WinQueryWindowTextLength(
             WinWindowFromID( dlg, MRM_EN_SingleEntryField ) );
            
            if ( !len )
            {
              WinAlarm( HWND_DESKTOP, WA_WARNING );
              WinSetWindowUShort( dlg, QWS_FLAGS,
               WinQueryWindowUShort( dlg, QWS_FLAGS ) & (~FF_DLGDISMISSED) );
              WinShowWindow( dlg, TRUE );
            }
          }
          
          CHECKED_MALLOC( len + 1, buddyName );
          WinQueryDlgItemText( dlg, MRM_EN_SingleEntryField, len + 1,
           buddyName );
          WinDestroyWindow( dlg );
          buddyName[ len ] = 0;
          
          debugf( "Requested to add buddy: %s\n", buddyName );
          
          theBuddy = (Buddy *) WinQueryWindowPtr( win, 84 );
          if ( !theBuddy )
          {
            newBuddy.entryName = (char *)localBuddyGroupName;
            newBuddy.gid = 0;
            newBuddy.id = 0;
            newBuddy.beenHere = 0;
            newBuddy.memberIDs = NULL;
            newBuddy.numMembers = 1; // signify that it's a group
            newBuddy.parentRecord = NULL;
            newBuddy.myRecord = NULL;
            theBuddy = theSession->createBuddy( &newBuddy, 1 );
            WinSetWindowPtr( win, 84, theBuddy );
          }
          
          newBuddy.entryName = buddyName;
          newBuddy.gid = 0;
          newBuddy.id = 0;
          newBuddy.beenHere = 0;
          newBuddy.memberIDs = NULL;
          newBuddy.numMembers = 0;
          newBuddy.parentRecord = theBuddy->record;
          newBuddy.myRecord = NULL;
          theBuddy = theSession->createBuddy( &newBuddy, 0 );
          
          if ( theBuddy )
          {
            addBuddy = new OscarData( theSession->getSeqNumPointer() );
            addBuddy->prepareAddClientSideBuddy( buddyName );
            theSession->queueForSend( addBuddy, 10 );
            saveLocalBuddyList( theSession, iniFile );
          }
          
          CHECKED_FREE( buddyName );
        }
        break;
        case MRM_SessionProfile:
        {
          HWND mgrWin = WinQueryWindowULong( win, 16 );
          HWND menuWin = WinQueryWindowULong( win, 68 );
          HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
          HWND dlg;
          OscarData *profileChange;
          messageEditInit initData;
          char *profilePtr;
          
          WinEnableMenuItem( menuWin, MRM_SessionProfile, FALSE );
          
          initData.userName = theSession->getUserName();
          initData.iniFile = iniFile;
          initData.messageType = "Profile";
          initData.profile = &profilePtr;
          
          profilePtr = theSession->settings->profile;
          
          dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmMessageEditor,
           dll, MRM_MessageChooser, &initData );
          
          if ( WinProcessDlg( dlg ) == DID_OK )
          {
            char *appName;
            
            if ( theSession->settings->inheritSettings )
            {
              uniquifySettingsStrings( theSession->settings );
            }
            
            if ( theSession->settings->profile )
            {
              CHECKED_FREE( theSession->settings->profile );
            }
            
            theSession->settings->profile = profilePtr;
            
            profileChange = new OscarData( theSession->getSeqNumPointer() );
            profileChange->prepareSetClientProfile(
             theSession->settings->profile );
            theSession->queueForSend( profileChange, 5 );
            
            CHECKED_MALLOC( strlen( theSession->getUserName() ) + 10, appName );
            sprintf( appName, "%s Settings", theSession->getUserName() );
            saveSessionSettings( iniFile, appName, theSession->settings );
            CHECKED_FREE( appName );
          }
          
          WinDestroyWindow( dlg );
          WinEnableMenuItem( menuWin, MRM_SessionProfile, TRUE );
        }
        break;
        case MRM_AwayMessage:
        {
          HWND mgrWin = WinQueryWindowULong( win, 16 );
          HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
          HWND menuWin = WinQueryWindowULong( win, 68 );
          HWND dlg;
          OscarData *awayChange;
          messageEditInit initData;
          char *profilePtr;
          
          WinEnableMenuItem( menuWin, MRM_AwayMessage, FALSE );
          
          initData.userName = theSession->getUserName();
          initData.iniFile = iniFile;
          initData.messageType = "Away Message";
          initData.profile = &profilePtr;
          
          profilePtr = theSession->settings->awayMessage;
          
          dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmMessageEditor,
           dll, MRM_MessageChooser, &initData );
          
          WinSetWindowText( dlg, "Create or select your away message..." );
          
          if ( WinProcessDlg( dlg ) == DID_OK )
          {
            char *appName;
            
            if ( theSession->settings->inheritSettings )
            {
              uniquifySettingsStrings( theSession->settings );
            }
            
            if ( theSession->settings->awayMessage )
            {
              CHECKED_FREE( theSession->settings->awayMessage );
            }
            
            theSession->settings->awayMessage = profilePtr;
            
            if ( WinIsMenuItemChecked( menuWin, MRM_SessionStatus ) )
            {
              awayChange = new OscarData( theSession->getSeqNumPointer() );
              awayChange->prepareSetClientAway(
               theSession->settings->awayMessage );
              theSession->queueForSend( awayChange, 5 );
            }
            
            CHECKED_MALLOC( strlen( theSession->getUserName() ) + 10, appName );
            sprintf( appName, "%s Settings", theSession->getUserName() );
            saveSessionSettings( iniFile, appName, theSession->settings );
            CHECKED_FREE( appName );
          }
          
          WinDestroyWindow( dlg );
          WinEnableMenuItem( menuWin, MRM_AwayMessage, TRUE );
        }

        break;
        case DID_CANCEL:
          WinSendMsg( win, WM_CLOSE, NULL, NULL );
        break;
      }
      
      DosRequestMutexSem( WinQueryWindowULong( win, 80 ), SEM_INDEFINITE_WAIT );
      
      if ( filterTypes )
      {
        FilterInfo theFilter;
        
        DosRequestMutexSem( theSession->buddyListAccessMux,
         SEM_INDEFINITE_WAIT );
        
        theFilter.excludeTypes = filterTypes;
        theFilter.buddyList = theSession->getBuddyList( &theFilter.numBuddies );
        
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER,
         MPFROMP( buddyListFilter ), MPFROMP( &theFilter ) );
        
        DosReleaseMutexSem( theSession->buddyListAccessMux );
      } else {
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_FILTER, NULL, NULL );
      }
      
      DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
    }
    break;
    case WM_SIZE:
    {
      USHORT newCx = SHORT1FROMMP( mp2 );
      USHORT newCy = SHORT2FROMMP( mp2 );
      HWND theWin;
      RECTL bRec;
      int bHeight;
      
      theWin = WinWindowFromID( win, MRM_PB_FilterOnline );
      WinQueryWindowRect( theWin, &bRec );
      bHeight = bRec.yTop - bRec.yBottom;
      
      WinSetWindowPos( theWin, 0, 0, 2, (newCx - 8) / 3, bHeight,
       SWP_SIZE | SWP_MOVE );
      theWin = WinWindowFromID( win, MRM_PB_FilterAway );
      WinSetWindowPos( theWin, 0, 4 + ((newCx - 8) / 3), 2,
       (newCx - 8) / 3, bHeight, SWP_SIZE | SWP_MOVE );
      theWin = WinWindowFromID( win, MRM_PB_FilterOffline );
      WinSetWindowPos( theWin, 0, 8 + (2 * ((newCx - 8) / 3)), 2,
       (newCx - 8) / 3, bHeight, SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, MRM_CN_BuddyContainer );
      WinSetWindowPos( theWin, 0, 0, bHeight + 4, newCx, newCy - (bHeight + 4),
       SWP_SIZE | SWP_MOVE );
    }
    break;
    case WM_PAINT:
    {
      HPS hps;
      RECTL rect;
      POINTL pt;
      ULONG colorIdx, ret;
      
      ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
       &colorIdx, QPF_ID1COLORINDEX | QPF_NOINHERIT );
      if ( !ret )
      {
        ret = WinQueryPresParam( win, PP_BACKGROUNDCOLOR, 0, NULL, 4,
         &colorIdx, QPF_NOINHERIT );
        if ( !ret )
        {
          ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
           &colorIdx, QPF_ID1COLORINDEX );
        }
      }
      
      hps = WinBeginPaint( win, NULLHANDLE, &rect );
      if ( ret < 4 )
        GpiSetColor( hps, CLR_PALEGRAY );
      else {
        GpiCreateLogColorTable( hps, 0, LCOLF_RGB, 0, 0, NULL );
        GpiSetColor( hps, colorIdx );
      }
      pt.x = rect.xLeft;
      pt.y = rect.yBottom;
      GpiMove( hps, &pt );
      pt.x = rect.xRight;
      pt.y = rect.yTop;
      GpiBox( hps, DRO_FILL, &pt, 0, 0 );
      WinEndPaint( hps );
    }
    break;
    case WM_INITULONGS:
    {
      MINIRECORDCORE *myRecord = (MINIRECORDCORE *) PVOIDFROMMP( mp1 );
      HWND mgrWin = LONGFROMMP( mp2 );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      char *appName, *ppKeys, *ppCurKey, *ppData;
      ULONG tmpLong, tmpLong2, dataLen;
      
      CHECKED_MALLOC( 38 + strlen( myRecord->pszIcon ), appName );
      sprintf( appName, "Buddy List [%s]", myRecord->pszIcon );

      tmpLong = 0;
      tmpLong2 = sizeof( ULONG );
      PrfQueryProfileData( iniFile, appName, "Filtration", &tmpLong,
       &tmpLong2 );
      // The initial filter settings
      
      if ( tmpLong & (2|8) )
        WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_PB_FilterOnline ), NULL );
      
      if ( tmpLong & 1 )
        WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_PB_FilterOffline ), NULL );

      if ( tmpLong & 4 )
        WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_PB_FilterAway ), NULL );

      WinShowWindow( WinQueryWindow( win, QW_PARENT ), TRUE );
      WinSetFocus( HWND_DESKTOP,
       WinWindowFromID( win, MRM_CN_BuddyContainer ) );
      WinSetWindowPtr( win, 0, myRecord );
      // My record in the session manager window
      WinSetWindowULong( win, 16, mgrWin );
      // Window handle to session manager
      
      sprintf( appName, "Buddy List [%s] Presentation Parameters",
       myRecord->pszIcon );
      
      if ( PrfQueryProfileSize( iniFile, appName, NULL, &dataLen ) && dataLen )
      {
        CHECKED_MALLOC( dataLen, ppKeys );
        
        if ( PrfQueryProfileData( iniFile, appName, NULL, ppKeys, &dataLen ) )
        {
          ppCurKey = ppKeys;
          while ( *ppCurKey )
          {
            if ( PrfQueryProfileSize( iniFile, appName, ppCurKey, &dataLen ) &&
                  dataLen )
            {
              CHECKED_MALLOC( dataLen, ppData );
              
              if ( PrfQueryProfileData( iniFile, appName, ppCurKey, ppData,
                    &dataLen ) )
              {
                WinSetPresParam( WinWindowFromID( win, MRM_CN_BuddyContainer ),
                 atol( ppCurKey ), dataLen, ppData );
                CHECKED_FREE( ppData );
              }
              ppCurKey += strlen( ppCurKey ) + 1;
            }
          }
          CHECKED_FREE( ppKeys );
        }
      }
      CHECKED_FREE( appName );
    }
    break;
    case WM_CLOSE:
    {
      RECORDCORE *record;
      MINIRECORDCORE *myRecord = (MINIRECORDCORE *) WinQueryWindowPtr( win, 0 );
      HEV wakeupSem = WinQueryWindowULong( win, 4 );
      HPOINTER offlineIco = WinQueryWindowULong( win, 28 );
      HPOINTER folder = WinQueryWindowULong( win, 12 );
      HPOINTER onlineIco = WinQueryWindowULong( win, 20 );
      HPOINTER awayIco = WinQueryWindowULong( win, 8 );
      HPOINTER hotOnlineIco = WinQueryWindowULong( win, 40 );
      HPOINTER meHere = WinQueryWindowULong( win, 92 );
      HPOINTER meAway = WinQueryWindowULong( win, 96 );
      HPOINTER onlineFilterIco = WinQueryWindowULong( win, 100 );
      HPOINTER awayFilterIco = WinQueryWindowULong( win, 104 );
      HPOINTER offlineFilterIco = WinQueryWindowULong( win, 108 );
      HWND mgrWin = WinQueryWindowULong( win, 16 );
      HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
      HWND frameWin = WinQueryWindow( win, QW_PARENT );
      OscarSession *theSession = (OscarSession *) WinQueryWindowPtr( win, 24 );
      HWND buddyPopup = WinQueryWindowULong( win, 64 );
      HWND buddyListPopup = WinQueryWindowULong( win, 68 );
      sessionThreadSettings *globalSettings = (sessionThreadSettings *)
       WinQueryWindowPtr( mgrWin, 60 );
      Buddy **buddyList;
      int numBuddies, i;
      SWP swp;
      char *appName;
      ULONG tmpLong;
      void *extraData;
      eventData theEventData;
      
      DosCloseMutexSem( WinQueryWindowULong( win, 80 ) );
      WinSetWindowULong( win, 80, 0 );
      
      theEventData.currentUser = theSession->getUserName();
      theEventData.otherUser = NULL;
      theEventData.message = NULL;
      handleMrMessageEvent( EVENT_ENDSESSION, theSession->settings,
       globalSettings, &theEventData, 0 );
     
      DosRequestMutexSem( theSession->buddyListAccessMux, SEM_INDEFINITE_WAIT );
      buddyList = theSession->getBuddyList( &numBuddies );
      for ( i=0; i<numBuddies; ++i )
      {
        if ( buddyList[i] && buddyList[i]->imChatWin )
        {
          WinSendMsg( buddyList[i]->imChatWin, WM_CLOSE, 0, 0 );
          buddyList[i]->imChatWin = 0;
          buddyList[i]->imChatClientWin = 0;
        }
      }
      DosReleaseMutexSem( theSession->buddyListAccessMux );
      
      CHECKED_MALLOC( 14 + strlen( myRecord->pszIcon ), appName );
      sprintf( appName, "Buddy List [%s]", myRecord->pszIcon );
      
      WinQueryWindowPos( frameWin, &swp );
      if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
      {
        swp.x = WinQueryWindowUShort( frameWin, QWS_XRESTORE );
        swp.y = WinQueryWindowUShort( frameWin, QWS_YRESTORE );
        swp.cx = WinQueryWindowUShort( frameWin, QWS_CXRESTORE );
        swp.cy = WinQueryWindowUShort( frameWin, QWS_CYRESTORE );
      }
      
      if ( swp.cx && swp.cy )
        PrfWriteProfileData( iniFile, appName, "Window position data",
         &swp, sizeof( SWP ) );
      // When we get booted, the window is already closed by the time
      //  we get here, so don't clobber the position information with 0s.
      
      tmpLong = WinQueryWindowULong( win, 48 );
      PrfWriteProfileData( iniFile, appName, "Filtration", &tmpLong,
       sizeof( ULONG ) );
      
      CHECKED_FREE( appName );
      
      debugf( "Deleting buddies from buddy list container.\n" );
      do {
        record = (RECORDCORE *) WinSendDlgItemMsg( win, MRM_CN_BuddyContainer,
         CM_QUERYRECORD, NULL, MPFROM2SHORT( CMA_LAST, CMA_ITEMORDER ) );
        if ( !record || (LONG)record == -1 ) break;
        
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_REMOVERECORD,
         MPFROMP( &record ), MPFROM2SHORT( 1, CMA_FREE ) );
      } while ( 1 );
      
      WinDestroyPointer( offlineIco );
      WinDestroyPointer( onlineIco );
      WinDestroyPointer( hotOnlineIco );
      WinDestroyPointer( awayIco );
      WinDestroyPointer( folder );
      WinDestroyPointer( meHere );
      WinDestroyPointer( meAway );
      WinDestroyPointer( onlineFilterIco );
      WinDestroyPointer( awayFilterIco );
      WinDestroyPointer( offlineFilterIco );
      
      debugf( "Closed buddy list window.\n" );
      WinDestroyWindow( WinWindowFromID( win, MRM_CN_BuddyContainer ) );
      extraData = WinQueryWindowPtr( WinWindowFromID( win, MRM_PB_FilterOnline ),
                   QWL_USER );
      WinDestroyWindow( WinWindowFromID( win, MRM_PB_FilterOnline ) );
      CHECKED_FREE( extraData );
      extraData = WinQueryWindowPtr( WinWindowFromID( win, MRM_PB_FilterOffline ),
                   QWL_USER );
      WinDestroyWindow( WinWindowFromID( win, MRM_PB_FilterOffline ) );
      CHECKED_FREE( extraData );
      extraData = WinQueryWindowPtr( WinWindowFromID( win, MRM_PB_FilterAway ),
                   QWL_USER );
      WinDestroyWindow( WinWindowFromID( win, MRM_PB_FilterAway ) );
      CHECKED_FREE( extraData );
      WinDestroyWindow( buddyPopup );
      WinDestroyWindow( buddyListPopup );
      WinDestroyWindow( WinQueryWindow( win, QW_PARENT ) );
      // Kill this frame window
      
      debugf( "Waking up session thread.\n" );
      DosPostEventSem( wakeupSem );
      // Tell the session object to pack it in
      
      WinSendMsg( mgrWin, WM_SESSIONCLOSED, MPFROMP( myRecord ), 0 );
      
      return 0;
      // Prevent the default window procedure from sending a WM_QUIT to the
      //  message queue and shutting down the whole app.
    }
    case WM_MENUEND:
    {
      RECORDCORE *record = (RECORDCORE *) WinQueryWindowPtr( win, 72 );
      RECTL rectl;
      POINTL pts[2];
      HWND buddyPopup = WinQueryWindowULong( win, 64 );
      HWND buddyListPopup = WinQueryWindowULong( win, 68 );
      
      if ( DosRequestMutexSem( WinQueryWindowULong( win, 80 ), 0 ) != 0 )
        break;
      
      // Remove the source emphasis if any records have it and invalidate
      //  the appropriate area of the container to make sure that this
      //  emphasis is correctly reflected.
      
      if ( buddyPopup != LONGFROMMP( mp2 ) &&
            buddyListPopup != LONGFROMMP( mp2 ) )
        break;
      // Only operate on popup windows
      
      WinShowWindow( LONGFROMMP( mp2 ), FALSE );
      
      if ( record && (LONG)record != -1 )
      {
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_SETRECORDEMPHASIS,
         MPFROMP( record ), MPFROM2SHORT( FALSE, CRA_SOURCE ) );
      } else {
        WinSendDlgItemMsg( win, MRM_CN_BuddyContainer, CM_SETRECORDEMPHASIS,
         MPFROMP( NULL ), MPFROM2SHORT( FALSE, CRA_SOURCE ) );
      }
      
      WinQueryWindowRect( LONGFROMMP( mp2 ), &rectl );
      pts[0].x = rectl.xLeft;
      pts[0].y = rectl.yBottom;
      pts[1].x = rectl.xRight;
      pts[1].y = rectl.yTop;
      WinMapWindowPoints( LONGFROMMP( mp2 ), WinWindowFromID( win,
       MRM_CN_BuddyContainer ), pts, 2 );
      rectl.xLeft = pts[0].x;
      rectl.yBottom = pts[0].y;
      rectl.xRight = pts[1].x;
      rectl.yTop = pts[1].y;
      WinInvalidateRect( WinWindowFromID( win, MRM_CN_BuddyContainer ),
       &rectl, TRUE );

      DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
    }
    break;
    case WM_CONTROL:
    {
      if ( SHORT1FROMMP( mp1 ) == MRM_CN_BuddyContainer )
      {
        switch ( SHORT2FROMMP( mp1 ) )
        {
          case CN_REALLOCPSZ:
          {
            CNREDITDATA *editData = (CNREDITDATA *) PVOIDFROMMP( mp2 );
            OscarSession *theSession = (OscarSession *)
             WinQueryWindowPtr( win, 24 );
            Buddy **buddyList;
            Buddy *theBuddy;
            int i, numBuddies;
            
            if ( !editData->pRecord || !editData->cbText ) return FALSE;
            
            theBuddy = NULL;
            
            DosRequestMutexSem( theSession->buddyListAccessMux,
             SEM_INDEFINITE_WAIT );
            
            buddyList = theSession->getBuddyList( &numBuddies );
            
            for ( i=0; i<numBuddies; ++i )
            {
              if ( buddyList[i]->record && buddyList[i]->record ==
                    editData->pRecord )
              {
                theBuddy = buddyList[i];
                break;
              }
            }
            
            DosReleaseMutexSem( theSession->buddyListAccessMux );
            
            if ( !theBuddy )
              return MRFROMLONG( FALSE );
            
            if ( theBuddy->alias == editData->pRecord->pszTree )
            {
              CHECKED_REALLOC( theBuddy->alias, editData->cbText,
               theBuddy->alias );
              *(editData->ppszText) = theBuddy->alias;
            } else {
              if ( theBuddy->alias )
              {
                CHECKED_REALLOC( theBuddy->alias, editData->cbText,
                 theBuddy->alias );
              } else {
                CHECKED_MALLOC( editData->cbText, theBuddy->alias );
              }
              *(editData->ppszText) = theBuddy->alias;
            }
            
            return MPFROMLONG( TRUE );
          }
          case CN_BEGINEDIT:
          {
            OscarSession *theSession = (OscarSession *)
             WinQueryWindowPtr( win, 24 );
            DosRequestMutexSem( WinQueryWindowULong( win, 80 ),
             SEM_INDEFINITE_WAIT );
            DosSuspendThread( theSession->getDataReceiverThreadID() );
          }
          break;
          case CN_ENDEDIT:
          {
            HWND mgrWin = WinQueryWindowULong( win, 16 );
            HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
            OscarSession *theSession = (OscarSession *)
             WinQueryWindowPtr( win, 24 );
            CNREDITDATA *editData = (CNREDITDATA *) PVOIDFROMMP( mp2 );
            char *text, *strippedSN;
            Buddy **buddyList;
            Buddy *theBuddy;
            int i, numBuddies;
            
            if ( !editData->pRecord ) return FALSE;
            
            DosRequestMutexSem( theSession->buddyListAccessMux,
             SEM_INDEFINITE_WAIT );
            
            theBuddy = NULL;
            buddyList = theSession->getBuddyList( &numBuddies );
            
            for ( i=0; i<numBuddies; ++i )
            {
              if ( buddyList[i]->record && buddyList[i]->record ==
                    editData->pRecord )
              {
                theBuddy = buddyList[i];
                break;
              }
            }
            
            DosReleaseMutexSem( theSession->buddyListAccessMux );
            
            if ( !theBuddy || !theBuddy->alias ) break;
            
            if ( theBuddy->imChatWin )
            {
              CHECKED_MALLOC( 31 + strlen( theSession->getUserName() ) +
               strlen( theBuddy->alias ), text );
              
              sprintf( text, "[%s <-> %s] Instant Message Window",
               theSession->getUserName(), theBuddy->alias );
              WinSetWindowText( theBuddy->imChatWin, text );
              CHECKED_FREE( text );
            }
            
            CHECKED_MALLOC( 9 + strlen( theSession->getUserName() ), text );
            sprintf( text, "%s Aliases", theSession->getUserName() );
            
            strippedSN = stripScreenName( theBuddy->userData.screenName );
            
            PrfWriteProfileData( iniFile, text, strippedSN,
             theBuddy->alias, strlen( theBuddy->alias ) + 1 );
            
            CHECKED_FREE( strippedSN );
            CHECKED_FREE( text );
            
            DosReleaseMutexSem( WinQueryWindowULong( win, 80 ) );
            DosResumeThread( theSession->getDataReceiverThreadID() );
          }
          break;
          case CN_CONTEXTMENU:
          {
            RECORDCORE *record = (RECORDCORE *) PVOIDFROMMP( mp2 );
            HWND menuWin;
            POINTL pt;
            
            if ( !record )
            {
              // Pop-up menu for background window
              menuWin = WinQueryWindowULong( win, 68 );
            } else {
              // Pop-up on an object
              menuWin = WinQueryWindowULong( win, 64 );
            }
            
            WinQueryPointerPos( HWND_DESKTOP, &pt );
            
            WinSetWindowPtr( win, 72, record );
            
            WinPopupMenu( HWND_DESKTOP, win, menuWin, pt.x, pt.y, 0,
             PU_KEYBOARD | PU_MOUSEBUTTON1 | PU_MOUSEBUTTON2 );
            
            WinSendMsg( WinWindowFromID( win, MRM_CN_BuddyContainer ),
             CM_SETRECORDEMPHASIS, MPFROMP( record ),
             MPFROM2SHORT( TRUE, CRA_SOURCE ) );
          }
          break;
          case CN_ENTER:
          {
            // A record was double-clicked or enter was pressed on one.
            // Open an IM window.
            
            NOTIFYRECORDENTER *notify = (NOTIFYRECORDENTER *)
             PVOIDFROMMP( mp2 );
            ULONG fcf = FCF_TITLEBAR | FCF_MINMAX | FCF_SYSMENU | FCF_TASKLIST |
                        FCF_SIZEBORDER | FCF_ICON | FCF_NOMOVEWITHOWNER;
            HMODULE dll = getModuleHandle();
            HWND mgrWin = WinQueryWindowULong( win, 16 );
            HINI iniFile = WinQueryWindowULong( mgrWin, 20 );
            HPOINTER folder = WinQueryWindowULong( win, 12 );
            OscarSession *theSession = (OscarSession *)
             WinQueryWindowPtr( win, 24 );
            sessionThreadSettings *globalSettings = (sessionThreadSettings *)
             WinQueryWindowPtr( mgrWin, 60 );
            char *text;
            HWND imFrame, imClient;
            RECTL desktop;
            SWP swp;
            ULONG tmpLong, tmpLong2;
            Buddy **buddyList;
            Buddy *theBuddy;
            int i, numBuddies;
            eventData theEventData;
            
            if ( !notify->pRecord ) break;
            
            debugf( "About to open IM chat window.\n" );
            
            DosRequestMutexSem( theSession->buddyListAccessMux,
             SEM_INDEFINITE_WAIT );
            
            theBuddy = NULL;
            buddyList = theSession->getBuddyList( &numBuddies );
            for ( i=0; i<numBuddies; ++i )
            {
              if ( buddyList[i] && buddyList[i]->record &&
                    notify->pRecord == buddyList[i]->record )
              {
                theBuddy = buddyList[i];
                break;
              }
            }
            
            DosReleaseMutexSem( theSession->buddyListAccessMux );
            
            if ( !theBuddy )
            {
              debugf( "Couldn't find the buddy we just picked!\n" );
              WinAlarm( HWND_DESKTOP, WA_WARNING );
              break;
            }
            
            if ( theBuddy->isGroup ) break;
            // Don't do anything if the user double-clicked the background or
            //  a buddy group.
            
            if ( theBuddy->imChatWin )
            {
              debugf( "IM chat window for this buddy already exists.\n" );
              WinSetFocus( HWND_DESKTOP, theBuddy->imChatWin );
              break;
            }
            
            if ( !theBuddy->userData.isOnline )
            {
              theEventData.currentUser = theSession->getUserName();
              theEventData.otherUser = theBuddy->userData.screenName;
              theEventData.message = "NOT ONLINE";
              
              handleMrMessageEvent( EVENT_ERRORBOX, theSession->settings,
               globalSettings, &theEventData, 0 );
              WinMessageBox( HWND_DESKTOP, win,
               "This user is currently not on-line.  You cannot send them an instant message.",
               "Cannot send an instant message", 999,
               MB_CANCEL | MB_APPLMODAL | MB_MOVEABLE );
              break;
            }
            
            WinSendMsg( WinWindowFromID( win, MRM_CN_BuddyContainer ),
             CM_SETRECORDEMPHASIS, MPFROMP( notify->pRecord ),
             MPFROM2SHORT( TRUE, CRA_INUSE ) );
            
            tmpLong = strlen( notify->pRecord->pszTree );
            tmpLong2 = strlen( theBuddy->userData.screenName );
            if ( tmpLong2 > tmpLong ) tmpLong = tmpLong2;
            
            tmpLong += strlen( theSession->getUserName() ) + 31;
             
            CHECKED_MALLOC( tmpLong, text );
            
            sprintf( text, "[%s <-> %s] Instant Message Window",
             theSession->getUserName(), notify->pRecord->pszTree );
            
            imFrame = WinCreateStdWindow( HWND_DESKTOP, WS_ANIMATE, &fcf,
             "MrMessage Instant Message Window", text, 0, dll, 1,
             &imClient );
            
            WinSetOwner( imFrame, win );
            
            theBuddy->imChatWin = imFrame;
            theBuddy->imChatClientWin = imClient;
            
            WinSetWindowPtr( imClient, 0, theBuddy );
            WinSetWindowULong( imClient, 4, win );
            
            WinQueryWindowRect( HWND_DESKTOP, &desktop );
            
            swp.x = desktop.xLeft +
             ((desktop.xRight - desktop.xLeft - IM_WINDOW_WIDTH) / 2);
            swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom -
             IM_WINDOW_HEIGHT) / 2);
            swp.cx = IM_WINDOW_WIDTH;
            swp.cy = IM_WINDOW_HEIGHT;
            // Establish defaults
            
            sprintf( text, "[%s <-> %s] Instant Message Window",
             theSession->getUserName(), theBuddy->userData.screenName );
            // Need the aliased one for the window title, but the actual
            //  screen name for the profile lookup.
            
            tmpLong = sizeof( SWP );
            PrfQueryProfileData( iniFile, text, "Window position data",
             &swp, &tmpLong );
            // Read INI settings if there are any
            
            WinSendMsg( imFrame, WM_SETICON, MPFROMLONG(
             theBuddy->record->hptrIcon ), NULL );
            WinSetWindowULong( imClient, 36, theBuddy->record->hptrIcon );
            WinSetWindowULong( imClient, 40, 0 );
            
            WinSetWindowPos( imFrame, 0, swp.x, swp.y, swp.cx, swp.cy,
             SWP_SIZE | SWP_MOVE | SWP_SHOW );
            
            WinSetFocus( HWND_DESKTOP,
             WinWindowFromID( imClient, MRM_ML_IMSender ) );
            
            CHECKED_FREE( text );
            
            theEventData.currentUser = theSession->getUserName();
            theEventData.otherUser = theBuddy->userData.screenName;
            theEventData.message = NULL;
            
            handleMrMessageEvent( EVENT_FIRSTCOMM, theSession->settings,
             globalSettings, &theEventData, 0 );
          }
          break;
        }
      }
    }
    break;
    case WM_MOUSEMOVE:
    case WM_CHAR:
      WinSendMsg( win, WM_NOTIDLE, 0, 0 );
    break;
  }
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmSessionEditor( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
    {
      char buffer[ 1024 ];
      ULONG attrLen;
      HWND ownerWin = WinWindowFromID( 
       WinQueryWindow( win, QW_OWNER ), FID_CLIENT );
      HWND buttonWin = WinWindowFromID( ownerWin, MRM_CreateSession );
      
      WinCheckButton( win, MRM_CB_AccountExists, TRUE );
      WinEnableWindow( WinWindowFromID( win, MRM_CB_AccountExists ), FALSE );
      WinSendDlgItemMsg( win, MRM_EN_Password, EM_SETTEXTLIMIT,
       MPFROMSHORT( 16 ), NULL );
      // Password is limited to 16 characters by AOL
      
      sessionEditorInit *initData = (sessionEditorInit *) PVOIDFROMMP( mp2 );
      if ( initData )
      {
        char buffer[10];
        
        WinSetWindowText( win, "Modify a Mr. Message Session" );
        WinSetDlgItemText( win, MRM_EN_ScreenName, initData->userName );
        WinSetDlgItemText( win, MRM_EN_Password, initData->password );
        WinSetDlgItemText( win, DID_OK, "~Modify Session" );
        WinSetDlgItemText( win, MRM_EN_ServerName, initData->server );
        WinSetDlgItemText( win, MRM_EN_ServerPort,
         ltoa( initData->port, (char *)buffer, 10 ) );
      } else {
        WinSetDlgItemText( win, MRM_EN_ServerName, "login.oscar.aol.com" );
        WinSetDlgItemText( win, MRM_EN_ServerPort, "5190" );
      }
      
      attrLen = WinQueryPresParam( ownerWin, PP_FONTNAMESIZE, 0, NULL, 1024,
       buffer, 0 );
      if ( attrLen )
      {
        WinSetPresParam( win, PP_FONTNAMESIZE, attrLen, buffer );
        // Copy the default font over to this window
      }
      attrLen = WinQueryPresParam( buttonWin, PP_FONTNAMESIZE, 0, NULL, 1024,
       buffer, 0 );
      if ( attrLen )
      {
        WinSetPresParam( WinWindowFromID( win, DID_OK ), PP_FONTNAMESIZE,
         attrLen, buffer );
        WinSetPresParam( WinWindowFromID( win, DID_CANCEL ), PP_FONTNAMESIZE,
         attrLen, buffer );
      }
    }
    break;
    case WM_COMMAND:
    {
      if ( SHORT1FROMMP( mp1 ) == DID_OK )
      {
        // Make sure we have a username and password before we can OK this.
        LONG theLen;
        
        theLen = WinQueryDlgItemTextLength( win, MRM_EN_ScreenName );
        if ( !theLen )
        {
          // Didn't have a username
          WinAlarm( HWND_DESKTOP, WA_WARNING );
          WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_EN_ScreenName ) );
          return 0;
        }
        
        theLen = WinQueryDlgItemTextLength( win, MRM_EN_Password );
        if ( !theLen )
        {
          // Didn't have a password
          WinAlarm( HWND_DESKTOP, WA_WARNING );
          WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_EN_Password ) );
          return 0;
        }
        
        theLen = WinQueryDlgItemTextLength( win, MRM_EN_ServerName );
        if ( !theLen )
        {
          // Didn't have a server name
          WinAlarm( HWND_DESKTOP, WA_WARNING );
          WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_EN_ServerName ) );
          return 0;
        }
        
        theLen = WinQueryDlgItemTextLength( win, MRM_EN_ServerPort );
        if ( !theLen )
        {
          // Didn't have a server port
          WinAlarm( HWND_DESKTOP, WA_WARNING );
          WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_EN_ServerPort ) );
          return 0;
        }
      }
    }
    break;
  }
  
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmPresParamNotifier2( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  MRESULT EXPENTRY (*oldProc) ( HWND, ULONG, MPARAM, MPARAM );
  switch ( msg )
  {
    case WM_PRESPARAMCHANGED:
      WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_WINPPCHANGED,
       MPFROMSHORT( WinQueryWindowUShort( win, QWS_ID ) ), mp1 );
    break;
  }
  oldProc = (MRESULT EXPENTRY (*) ( HWND, ULONG, MPARAM, MPARAM ))
   WinQueryWindowPtr( WinQueryWindow( win, QW_PARENT ), 76 );
   
  return oldProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmPresParamNotifier( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  MRESULT EXPENTRY (*oldProc) ( HWND, ULONG, MPARAM, MPARAM );
  switch ( msg )
  {
    case WM_PRESPARAMCHANGED:
      WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_WINPPCHANGED,
       MPFROMSHORT( WinQueryWindowUShort( win, QWS_ID ) ), mp1 );
    break;
    case WM_CHAR:
      if ( !(SHORT1FROMMP( mp1 ) & KC_KEYUP) &&
            SHORT1FROMMP( mp1 ) & KC_VIRTUALKEY )
      {
        USHORT theVK = SHORT2FROMMP( mp2 );
        if ( theVK == VK_TAB )
        {
          if ( WinQueryWindowUShort( win, QWS_ID ) == 104 )
          {
            WinSetFocus( HWND_DESKTOP, 
             WinWindowFromID( WinQueryWindow( win, QW_PARENT ), 100 ) );
          } else {
            WinSetFocus( HWND_DESKTOP, 
             WinWindowFromID( WinQueryWindow( win, QW_PARENT ),
             WinQueryWindowUShort( win, QWS_ID ) + 1 ) );
          }
        }
        if ( theVK == VK_BACKTAB )
        {
          if ( WinQueryWindowUShort( win, QWS_ID ) == 100 )
          {
            WinSetFocus( HWND_DESKTOP,
             WinWindowFromID( WinQueryWindow( win, QW_PARENT ), 104 ) );
          } else {
            WinSetFocus( HWND_DESKTOP,
             WinWindowFromID( WinQueryWindow( win, QW_PARENT ),
             WinQueryWindowUShort( win, QWS_ID ) - 1 ) );
          }
        }
      }
    break;
  }
  
  oldProc = (MRESULT EXPENTRY (*) ( HWND, ULONG, MPARAM, MPARAM ))
   WinQueryWindowPtr( WinQueryWindow( win, QW_PARENT ), 32 +
    ((WinQueryWindowUShort( win, QWS_ID ) - 100) * 4) );
  
  // Window procedures are stored in the parent window from 32 on in order of
  //  window IDs.  Window ID - 100 is the index into this array of window
  //  procedure addresses.

  return oldProc( win, msg, mp1, mp2 );
}

typedef struct
{
  sessionThreadInfo **info;
  sessionThreadSettings *globalSettings;
} settingsDialogInit;

MRESULT EXPENTRY pmSettings( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
      WinSetWindowPtr( win, QWL_USER, PVOIDFROMMP( mp2 ) );
    break;
    case WM_CONTROL:
    {
      if ( ((SHORT1FROMMP( mp1 ) == MRM_LB_SettingsScope ||
             SHORT1FROMMP( mp1 ) == MRM_LB_EventType) &&
            SHORT2FROMMP( mp1 ) == LN_SELECT) ||
           ((SHORT1FROMMP( mp1 ) == MRM_EN_EventPlaySoundFile ||
             SHORT1FROMMP( mp1 ) == MRM_EN_EventRunREXXScript ||
             SHORT1FROMMP( mp1 ) == MRM_EN_EventRunCommand) &&
            SHORT2FROMMP( mp1 ) == EN_KILLFOCUS) ||
           ((SHORT1FROMMP( mp1 ) == MRM_CB_EventPlaySound ||
             SHORT1FROMMP( mp1 ) == MRM_CB_EventRunREXX ||
             SHORT1FROMMP( mp1 ) == MRM_CB_EventRun ||
             SHORT1FROMMP( mp1 ) == MRM_CB_EnableTimestamps ||
             SHORT1FROMMP( mp1 ) == MRM_CB_SessionAutoStart ||
             SHORT1FROMMP( mp1 ) == MRM_CB_AutoMinimize ||
             SHORT1FROMMP( mp1 ) == MRM_CB_TypingNotify) &&
            (SHORT2FROMMP( mp1 ) == BN_CLICKED ||
             SHORT2FROMMP( mp1 ) == BN_DBLCLICKED)) )
      {
        settingsDialogInit *init = (settingsDialogInit *)
         WinQueryWindowPtr( win, QWL_USER );;
        sessionThreadInfo **info = init->info;
        sessionThreadSettings *globalSettings = init->globalSettings;
        SHORT idx = -1;
        sessionThreadSettings *theSettings = NULL;
        int dupStrings = 0, isGlobal = 0;
        unsigned long myEntry;
        SHORT maxIdx;
        
        maxIdx = SHORT1FROMMR( WinSendDlgItemMsg( win,
         MRM_LB_SettingsScope, LM_QUERYITEMCOUNT, NULL, NULL ) );
        maxIdx--;
        
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win,
         MRM_LB_SettingsScope, LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ),
         NULL ) );
        
        if ( idx == LIT_NONE )
        {
          WinAlarm( HWND_DESKTOP, WA_ERROR );
          break;
        }
        
        myEntry = (unsigned long) LONGFROMMR(
         WinSendDlgItemMsg( win, MRM_LB_SettingsScope, LM_QUERYITEMHANDLE,
         MPFROMSHORT( idx ), NULL ) );
        
        isGlobal = (idx == maxIdx);
        if ( !isGlobal )
        {
          theSettings = (*info)[myEntry].settings;
          if ( !theSettings )
          {
            theSettings = globalSettings;
            isGlobal = 1;
          }
        } else {
          theSettings = globalSettings;
        }
        
        WinEnableWindow( WinWindowFromID( win, MRM_PB_UseGlobal ),
         !isGlobal && !theSettings->inheritSettings );
        
        if ( theSettings->inheritSettings )
        {
          // We should inherit our settings from the global settings.
          // Copy in the current global settings.
          memcpy( theSettings, globalSettings,
           sizeof( sessionThreadSettings ) );
          theSettings->inheritSettings = 1;
        }
        
        dupStrings = theSettings->inheritSettings;
        
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win,
         MRM_LB_EventType, LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ),
         NULL ) );
        
        if ( SHORT1FROMMP( mp1 ) == MRM_LB_SettingsScope &&
              SHORT2FROMMP( mp1 ) == LN_SELECT )
        {
          WinCheckButton( win, MRM_CB_EnableTimestamps,
           (theSettings->sessionFlags & SET_TIMESTAMPS_ENABLED) != 0 );
          WinCheckButton( win, MRM_CB_SessionAutoStart,
           (theSettings->sessionFlags & SET_AUTOSTART) != 0 );
          WinCheckButton( win, MRM_CB_AutoMinimize,
           (theSettings->sessionFlags & SET_AUTOMINIMIZE) != 0 );
          WinCheckButton( win, MRM_CB_TypingNotify,
           (theSettings->sessionFlags & SET_TYPING_NOTIFICATIONS) != 0 );
        }
        
        if ( SHORT2FROMMP( mp1 ) == BN_CLICKED ||
              SHORT2FROMMP( mp1 ) == BN_DBLCLICKED )
        {
          if ( SHORT1FROMMP( mp1 ) == MRM_CB_EventPlaySound )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_EventPlaySound ) )
            {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventPlaySoundFile ),
               TRUE );
              theSettings->settingFlags[idx] |= SET_SOUND_ENABLED;
            } else {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventPlaySoundFile ),
               FALSE );
              theSettings->settingFlags[idx] &= ~SET_SOUND_ENABLED;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_EventRunREXX )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_EventRunREXX ) )
            {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunREXXScript ),
               TRUE );
              theSettings->settingFlags[idx] |= SET_REXX_ENABLED;
            } else {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunREXXScript ),
               FALSE );
              theSettings->settingFlags[idx] &= ~SET_REXX_ENABLED;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_EventRun )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_EventRun ) )
            {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunCommand ),
               TRUE );
              theSettings->settingFlags[idx] |= SET_SHELL_ENABLED;
            } else {
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunCommand ),
               FALSE );
              theSettings->settingFlags[idx] &= ~SET_SHELL_ENABLED;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_EnableTimestamps )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_EnableTimestamps ) )
            {
              theSettings->sessionFlags |= SET_TIMESTAMPS_ENABLED;
            } else {
              theSettings->sessionFlags &= ~SET_TIMESTAMPS_ENABLED;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_SessionAutoStart )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_SessionAutoStart ) )
            {
              theSettings->sessionFlags |= SET_AUTOSTART;
            } else {
              theSettings->sessionFlags &= ~SET_AUTOSTART;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_AutoMinimize )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_AutoMinimize ) )
            {
              theSettings->sessionFlags |= SET_AUTOMINIMIZE;
            } else {
              theSettings->sessionFlags &= ~SET_AUTOMINIMIZE;
            }
          } else if ( SHORT1FROMMP( mp1 ) == MRM_CB_TypingNotify )
          {
            dupStrings++;
            if ( WinQueryButtonCheckstate( win, MRM_CB_TypingNotify ) )
            {
              theSettings->sessionFlags |= SET_TYPING_NOTIFICATIONS;
            } else {
              theSettings->sessionFlags &= ~SET_TYPING_NOTIFICATIONS;
            }
          }
          
          if ( dupStrings > 1 )
          {
            // Have to have inherit flag set and changed a checkbox
            if ( !isGlobal )
            {
              uniquifySettingsStrings( theSettings );
              WinEnableWindow( WinWindowFromID( win, MRM_PB_UseGlobal ), TRUE );
            }
          }
        }
        
        if ( SHORT1FROMMP( mp1 ) == MRM_LB_SettingsScope ||
              SHORT1FROMMP( mp1 ) == MRM_LB_EventType )
        {
          if ( SHORT2FROMMP( mp1 ) == LN_SELECT )
          {
            if ( idx == LIT_NONE )
            {
              WinEnableWindow( WinWindowFromID( win, MRM_CB_EventPlaySound ),
               FALSE );
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventPlaySoundFile ),
               FALSE );
              WinEnableWindow( WinWindowFromID( win, MRM_CB_EventRunREXX ),
               FALSE );
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunREXXScript ),
               FALSE );
              WinEnableWindow( WinWindowFromID( win, MRM_CB_EventRun ),
               FALSE );
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunCommand ),
               FALSE );
            } else {
              WinCheckButton( win, MRM_CB_EventPlaySound,
               theSettings->settingFlags[idx] & SET_SOUND_ENABLED );
              WinSetDlgItemText( win, MRM_EN_EventPlaySoundFile,
               theSettings->sounds[idx] );
              WinSendDlgItemMsg( win, MRM_EN_EventPlaySoundFile,
               EM_QUERYCHANGED, NULL, NULL );
              // Reset the changed status
              
              WinCheckButton( win, MRM_CB_EventRunREXX,
               theSettings->settingFlags[idx] & SET_REXX_ENABLED );
              WinSetDlgItemText( win, MRM_EN_EventRunREXXScript,
               theSettings->rexxScripts[idx] );
              WinSendDlgItemMsg( win, MRM_EN_EventPlaySoundFile,
               EM_QUERYCHANGED, NULL, NULL );
              // Reset the changed status
              
              WinCheckButton( win, MRM_CB_EventRun,
               theSettings->settingFlags[idx] & SET_SHELL_ENABLED );
              WinSetDlgItemText( win, MRM_EN_EventRunCommand,
               theSettings->shellCmds[idx] );
              WinSendDlgItemMsg( win, MRM_EN_EventPlaySoundFile,
               EM_QUERYCHANGED, NULL, NULL );
              // Reset the changed status
              
              if ( isMultiMediaCapable() )
              {
                WinEnableWindow( WinWindowFromID( win, MRM_CB_EventPlaySound ),
                 TRUE );
                WinEnableWindow( WinWindowFromID( win, MRM_EN_EventPlaySoundFile ),
                 theSettings->settingFlags[idx] & SET_SOUND_ENABLED );
              } else {
                WinEnableWindow( WinWindowFromID( win, MRM_CB_EventPlaySound ),
                 FALSE );
                WinEnableWindow( WinWindowFromID( win, MRM_EN_EventPlaySoundFile ),
                 FALSE );
              }
              
              if ( isRexxCapable() )
              {
                WinEnableWindow( WinWindowFromID( win, MRM_CB_EventRunREXX ),
                 TRUE );
                WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunREXXScript ),
                 theSettings->settingFlags[idx] & SET_REXX_ENABLED );
              }
              
/*            WinEnableWindow( WinWindowFromID( win, MRM_CB_EventRun ),
               TRUE );
              WinEnableWindow( WinWindowFromID( win, MRM_EN_EventRunCommand ),
               theSettings->settingFlags[idx] & SET_SHELL_ENABLED );
*/          
              // Don't enable because there's no functionality yet
            }
          }
        }
        
        if ( SHORT1FROMMP( mp1 ) == MRM_EN_EventPlaySoundFile &&
              SHORT2FROMMP( mp1 ) == EN_KILLFOCUS )
        {
          if ( WinSendDlgItemMsg( win, MRM_EN_EventPlaySoundFile,
                EM_QUERYCHANGED, NULL, NULL ) )
          {
            int textLen = WinQueryDlgItemTextLength( win,
                           MRM_EN_EventPlaySoundFile );
            
            if ( theSettings->inheritSettings && !isGlobal )
              uniquifySettingsStrings( theSettings );
            
            if ( theSettings->sounds[idx] )
              CHECKED_FREE( theSettings->sounds[idx] );
            
            theSettings->sounds[idx] = NULL;
            theSettings->inheritSettings = 0;
            WinEnableWindow( WinWindowFromID( win, MRM_PB_UseGlobal ), TRUE );
            
            if ( textLen )
            {
              CHECKED_MALLOC( textLen + 1, theSettings->sounds[idx] );
              WinQueryDlgItemText( win, MRM_EN_EventPlaySoundFile, textLen + 1,
               theSettings->sounds[idx] );
            }
          }
        }
        if ( SHORT1FROMMP( mp1 ) == MRM_EN_EventRunREXXScript &&
              SHORT2FROMMP( mp1 ) == EN_KILLFOCUS )
        {
          if ( WinSendDlgItemMsg( win, MRM_EN_EventRunREXXScript,
                EM_QUERYCHANGED, NULL, NULL ) )
          {
            int textLen = WinQueryDlgItemTextLength( win,
             MRM_EN_EventRunREXXScript );
            
            if ( theSettings->inheritSettings && !isGlobal )
              uniquifySettingsStrings( theSettings );
            
            if ( theSettings->rexxScripts[idx] )
              CHECKED_FREE( theSettings->rexxScripts[idx] );
            
            theSettings->rexxScripts[idx] = NULL;
            theSettings->inheritSettings = 0;
            WinEnableWindow( WinWindowFromID( win, MRM_PB_UseGlobal ), TRUE );
            
            if ( textLen )
            {
              CHECKED_MALLOC( textLen + 1, theSettings->rexxScripts[idx] );
              WinQueryDlgItemText( win, MRM_EN_EventRunREXXScript, textLen + 1,
               theSettings->rexxScripts[idx] );
            }
          }
        }
        if ( SHORT1FROMMP( mp1 ) == MRM_EN_EventRunREXXScript &&
              SHORT2FROMMP( mp1 ) == EN_KILLFOCUS )
        {
          if ( WinSendDlgItemMsg( win, MRM_EN_EventRunCommand,
                EM_QUERYCHANGED, NULL, NULL ) )
          {
            int textLen = WinQueryDlgItemTextLength( win,
             MRM_EN_EventRunCommand );
            
            if ( theSettings->inheritSettings && !isGlobal )
              uniquifySettingsStrings( theSettings );
            
            if ( theSettings->shellCmds[idx] )
              CHECKED_FREE( theSettings->shellCmds[idx] );
            
            theSettings->shellCmds[idx] = NULL;
            theSettings->inheritSettings = 0;
            WinEnableWindow( WinWindowFromID( win, MRM_PB_UseGlobal ), TRUE );
            
            if ( textLen )
            {
              CHECKED_MALLOC( textLen + 1, theSettings->shellCmds[idx] );
              WinQueryDlgItemText( win, MRM_EN_EventRunCommand, textLen + 1,
               theSettings->shellCmds[idx] );
            }
          }
        }
      }
    }
    break;
    case WM_COMMAND:
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case MRM_PB_UseGlobal:
        {
          settingsDialogInit *init = (settingsDialogInit *)
           WinQueryWindowPtr( win, QWL_USER );;
          sessionThreadInfo **info = init->info;
          sessionThreadSettings *globalSettings = init->globalSettings;
          SHORT idx = -1;
          sessionThreadSettings *theSettings = NULL;
          unsigned long myEntry;
          int i;
          
          idx = SHORT1FROMMR( WinSendDlgItemMsg( win,
           MRM_LB_SettingsScope, LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ),
           NULL ) );
          
          if ( idx == LIT_NONE )
          {
            WinAlarm( HWND_DESKTOP, WA_ERROR );
            break;
          }
          
          myEntry = (unsigned long) LONGFROMMR(
           WinSendDlgItemMsg( win, MRM_LB_SettingsScope, LM_QUERYITEMHANDLE,
           MPFROMSHORT( idx ), NULL ) );
          theSettings = (*info)[myEntry].settings;
          
          if ( theSettings->inheritSettings ) return 0;
          
          if ( theSettings->profile )
          {
            CHECKED_FREE( theSettings->profile );
            theSettings->profile = NULL;
          }
          
          for ( i=0; i<EVENT_MAXEVENTS; ++i )
          {
            if ( theSettings->sounds[i] )
            {
              CHECKED_FREE( theSettings->sounds[i] );
              theSettings->sounds[i] = NULL;
            }
            if ( theSettings->rexxScripts[i] )
            {
              CHECKED_FREE( theSettings->rexxScripts[i] );
              theSettings->rexxScripts[i] = NULL;
            }
            if ( theSettings->shellCmds[i] )
            {
              CHECKED_FREE( theSettings->shellCmds[i] );
              theSettings->shellCmds[i] = NULL;
            }
          }
          theSettings->inheritSettings = 1;
          WinSendMsg( win, WM_CONTROL,
           MPFROM2SHORT( MRM_LB_SettingsScope, LN_SELECT ), NULL );
          // Force contents update and button disabling
        }
        return 0;
        // Don't want to dismiss dialog
      }
    break;
  }
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY pmSessionManager( HWND win, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_USERBOOTED:
    {
      ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
      sessionThreadInfo **info =
       (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
      int i;
      
      for ( i=0; i<(*numItems); ++i )
      {
        if ( (*info)[i].theSession == PVOIDFROMMP( mp1 ) )
        {
          WinSendMsg( (*info)[i].theSession->getBuddyListWindow(),
           WM_CLOSE, NULL, NULL );
          // I'll do something classier later on, but for now, this will do.
          break;
        }
      }
    }
    break;
    case WM_CREATEBUDDYLIST:
    {
      ULONG fcf = FCF_TITLEBAR | FCF_MINMAX | FCF_SYSMENU | FCF_TASKLIST |
                   FCF_SIZEBORDER | FCF_ICON | FCF_NOMOVEWITHOWNER;
      HMODULE dll = getModuleHandle();
      HWND buddyFrame, buddyClient;
      RECTL desktop;
      buddyListCreateData *buddyCreateData = (buddyListCreateData *)
       PVOIDFROMMP( mp2 );
      UserInformation *userInfo = buddyCreateData->userInfo;
      MINIRECORDCORE *myRecord = (MINIRECORDCORE *) PVOIDFROMMP( mp1 );
      char *buffer;
      HINI iniFile = WinQueryWindowULong( win, 20 );
      SWP swp;
      ULONG tmpLong;
      
      CHECKED_MALLOC( 14 + strlen( userInfo->screenName ), buffer );
      
      sprintf( buffer, "Buddy List [%s]", userInfo->screenName );
      buddyFrame = WinCreateStdWindow( HWND_DESKTOP, WS_ANIMATE, &fcf,
       "MrMessage Buddy List", buffer, 0, dll, 1, &buddyClient );
       
      buddyCreateData->buddyListWin = buddyFrame;
      
      WinQueryWindowRect( HWND_DESKTOP, &desktop );
      
      swp.x = desktop.xLeft +
       ((desktop.xRight - desktop.xLeft - BUDDY_LIST_WIDTH) / 2);
      swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom -
        BUDDY_LIST_HEIGHT) / 2);
      swp.cx = BUDDY_LIST_WIDTH;
      swp.cy = BUDDY_LIST_HEIGHT;
      // Establish defaults
      
      tmpLong = sizeof( SWP );
      PrfQueryProfileData( iniFile, buffer, "Window position data", &swp,
       &tmpLong );
      // Read INI settings if there are any
      
      CHECKED_FREE( buffer );
      
      WinSetWindowPos( buddyFrame, 0, swp.x, swp.y, swp.cx, swp.cy,
       SWP_SIZE | SWP_MOVE );
       
      WinSetWindowPtr( buddyClient, 24, buddyCreateData->theSession );
      WinPostMsg( buddyClient, WM_INITULONGS, mp1, MPFROMLONG( win ) );
      WinSetFocus( HWND_DESKTOP, buddyFrame );
      
      DosPostEventSem( buddyCreateData->wakeupSem );
    }
    break;
    case WM_ERRORMESSAGE:
    {
      char titleString[ 1024 ];
      MINIRECORDCORE *theRecord = (MINIRECORDCORE *)PVOIDFROMMP( mp2 );
      char *loginName = theRecord->pszIcon;
      ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
      sessionThreadInfo **info =
       (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
      sessionThreadSettings *globalSettings = (sessionThreadSettings *)
       WinQueryWindowPtr( win, 60 );
      OscarSession *theSession = NULL;
      eventData theEventData;
      int i;
      
      for ( i=0; i<(*numItems); ++i )
      {
        if ( (*info)[i].record == theRecord )
        {
          theSession = (*info)[i].theSession;
          break;
        }
      }
      
      if ( strlen( loginName ) > 998 )
      {
        strcpy( titleString, "Error with AIM session (" );
        strncat( titleString, loginName, 998 );
        strcat( titleString, ")" );
      } else {
        sprintf( titleString, "Error with AIM session (%s)", loginName );
      }
      
      theEventData.currentUser = loginName;
      theEventData.otherUser = NULL;
      theEventData.message = errorMessageStrings[ SHORT1FROMMP(mp1) ];
      
      if ( theSession )
      {
        handleMrMessageEvent( EVENT_ERRORBOX, (*info)[i].settings,
         globalSettings, &theEventData, 0 );
      }
      
      WinMessageBox( HWND_DESKTOP, win,
       errorMessageStrings[ SHORT1FROMMP(mp1) ], titleString, 999,
       MB_OK | MB_APPLMODAL | MB_MOVEABLE );
      
      if ( SHORT1FROMMP( mp1 ) == MRM_AIM_BAD_PASSWORD ||
            SHORT1FROMMP( mp1 ) == MRM_AIM_BAD_IDPASS )
      {
        sessionThreadInfo **info =
         (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
        
        if ( !theSession )
        {
          debugf( "A session reported a password error, but I couldn't find it.\n" );
          WinAlarm( HWND_DESKTOP, WA_ERROR );
        } else {
          MINIRECORDCORE *record;
          
          record = (MINIRECORDCORE *)CMA_FIRST;
          do {
            record = (MINIRECORDCORE *) WinSendMsg(
             WinWindowFromID( win, MRM_CN_SessionContainer ),
             CM_QUERYRECORDEMPHASIS, MPFROMP( record ),
             MPFROMSHORT( CRA_SELECTED ) );
            if ( !record || (LONG)record == -1 ) break;
          
            WinSendMsg( WinWindowFromID( win, MRM_CN_SessionContainer ),
             CM_SETRECORDEMPHASIS, MPFROMP( record ),
             MPFROM2SHORT( FALSE, CRA_SELECTED ) );
          } while ( 1 );
          // De-select everything
          
          WinSendMsg( WinWindowFromID( win, MRM_CN_SessionContainer ),
           CM_SETRECORDEMPHASIS, mp2, MPFROM2SHORT( TRUE, CRA_SELECTED ) );
          // Select the record with the problem
          
          WinSetWindowULong( win, 56, 1 );
          WinSendMsg( win, WM_COMMAND, MPFROMSHORT( MRM_EditSession ), NULL );
          WinSetWindowULong( win, 56, 0 );
          // And edit it
        }
      }
      
      WinPostMsg( win, WM_SESSIONCLOSED, mp2, NULL );
    }
    break;
    case WM_SESSIONCLOSED:
    {
      ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
      sessionThreadInfo **info =
       (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
      sessionThreadSettings *globalSettings =
       (sessionThreadSettings *) WinQueryWindowPtr( win, 60 );
      ULONG i;
      
      WinSendMsg( WinWindowFromID( win, MRM_CN_SessionContainer ),
       CM_SETRECORDEMPHASIS, mp1, MPFROM2SHORT( FALSE, CRA_INUSE ) );
      
      for ( i=0; i<(*numItems); ++i )
      {
        if ( (*info)[i].userName && 
              (*info)[i].record == (MINIRECORDCORE *) PVOIDFROMMP( mp1 ) )
        {
          if ( (*info)[i].theSession )
            (*info)[i].theSession->windowClosed();
          
          if ( testSettingsFlag( SET_AUTOMINIMIZE, (*info)[i].settings,
                globalSettings ) )
          {
            WinSetWindowPos( WinQueryWindow( win, QW_PARENT ), NULLHANDLE,
             0, 0, 0, 0, SWP_RESTORE );
          }
          // Restore if auto-minimized
          break;
        }
      }
    }
    break;
    case WM_INITULONGS:
    {
      sessionManagerULONGs *initData =
       (sessionManagerULONGs *) PVOIDFROMMP( mp1 );
      ULONG numSessions, i, j, dataLen, myEntry, cursoredItem;
      USHORT txtLen;
      char keyString[80];
      char *text, *decodedPass, *ppKeys, *ppCurKey, *ppData;
      unsigned char *sessionData;
      MINIRECORDCORE *record, *cursoredRecord;
      RECORDINSERT insert;
      sessionThreadInfo **info;
      ULONG *numItems;
      HPOINTER icon;
      HMTX infoMux;
      const char *ppApp;
      sessionThreadSettings *mySettings;
      char *appName;

      WinSetWindowPtr( win, 4, initData->numItemsPtr );
      WinSetWindowPtr( win, 8, initData->threadInfoPtr );
      WinSetWindowULong( win, 12, initData->mutexSem );
      WinSetWindowULong( win, 20, initData->iniFile );
      WinSetWindowPtr( win, 60, initData->globalSettings );

      info = initData->threadInfoPtr;
      numItems = initData->numItemsPtr;
      infoMux = initData->mutexSem;
      
      // Now that we have the INI file handle, check if we can restore any
      //  AIM session data.
      
      for ( i=0; i<3; ++i )
      {
        switch ( i )
        {
          case 2:  ppApp = "Session Manager Presentation Parameters"; break;
          case 1:  ppApp = "Session Manager Presentation Parameters (Buttons)"; break;
          default: ppApp = "Session Manager Presentation Parameters (Container)";
        }
        if ( PrfQueryProfileSize( initData->iniFile, ppApp, NULL, &dataLen ) &&
              dataLen )
        {
          CHECKED_MALLOC( dataLen, ppKeys );
          
          if ( PrfQueryProfileData( initData->iniFile, ppApp, NULL, ppKeys,
                &dataLen ) )
          {
            ppCurKey = ppKeys;
            while ( *ppCurKey )
            {
              if ( PrfQueryProfileSize( initData->iniFile, ppApp, ppCurKey,
                    &dataLen ) )
              {
                CHECKED_MALLOC( dataLen, ppData );
                
                if ( PrfQueryProfileData( initData->iniFile, ppApp, ppCurKey,
                      ppData, &dataLen ) )
                {
                  switch ( i )
                  {
                    case 2:
                      WinSetPresParam( win, atol( ppCurKey ), dataLen, ppData );
                    break;
                    case 1:
                      WinSetPresParam( WinWindowFromID( win, MRM_StartSession ),
                       atol( ppCurKey ), dataLen, ppData );
                      WinSetPresParam( WinWindowFromID( win, MRM_CreateSession ),
                       atol( ppCurKey ), dataLen, ppData );
                      WinSetPresParam( WinWindowFromID( win, MRM_EditSession ),
                       atol( ppCurKey ), dataLen, ppData );
                      WinSetPresParam( WinWindowFromID( win, MRM_DeleteSession ),
                       atol( ppCurKey ), dataLen, ppData );
                    break;
                    default:
                      WinSetPresParam( WinWindowFromID( win, MRM_CN_SessionContainer ),
                       atol( ppCurKey ), dataLen, ppData );
                  }
                }
                CHECKED_FREE( ppData );
              }
              ppCurKey += strlen( ppCurKey ) + 1;
            }
          }
          CHECKED_FREE( ppKeys );
        }
      }
        
      dataLen = 4;
      if ( !PrfQueryProfileData( initData->iniFile, "Session Manager",
            "Number of Sessions", &numSessions, &dataLen ) )
      {
        debugf( "Missing or corrupted session manager data.\n" );
        WinShowWindow( WinQueryWindow( win, QW_PARENT ), TRUE );
        WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_CN_SessionContainer ) );
        return NULL;
      }
      
      cursoredRecord = NULL;
      cursoredItem = 0;
      dataLen = 4;
      PrfQueryProfileData( initData->iniFile, "Session Manager",
       "Cursored Record", &cursoredItem, &dataLen );
      // If it's there, great.  If not... don't care.
      
      debugf( "Profile contains %d session entries.\n", numSessions );
      
      for ( i=1; i<=numSessions; ++i )
      {
        sprintf( keyString, "Session %d data", i );
        if ( PrfQueryProfileSize( initData->iniFile, "Session Manager",
              keyString, &dataLen ) )
        {
          char isExtendedInfo;
          
          CHECKED_MALLOC( dataLen, sessionData );
          
          PrfQueryProfileData( initData->iniFile, "Session Manager",
           keyString, sessionData, &dataLen );
          
          j = 0;
          isExtendedInfo = 0;
          
          if ( 4 > dataLen )
          {
            debugf( "Profile entry for session %d is corrupt or incomplete.\n",
             i );
            CHECKED_FREE( sessionData );
            continue;
          }
          
          txtLen = *((USHORT *)(sessionData));
          // Length of username
          j += 2 + txtLen;
          // Points to the password string length
          
          if ( j > dataLen ||
               (j + 2 + *((USHORT *)(sessionData + j))) > dataLen )
          {
            debugf( "Profile entry for session %d is corrupt or incomplete.\n",
             i );
            CHECKED_FREE( sessionData );
            continue;
          }
          
          txtLen = *((USHORT *)(sessionData + j));
          // Length of password
          j += 2 + txtLen;
          // Points to the end or length of the server name
          
          if ( dataLen > j )
          {
            isExtendedInfo = 1;
            txtLen = *((USHORT *)(sessionData + j));
            // Length of the server name
            
            j += 2 + txtLen;
            // Points to the port number
            
            if ( j + 2 > dataLen )
            {
              debugf( "Profile entry for session %d is corrupt or incomplete (extended info).\n",
               i );
              CHECKED_FREE( sessionData );
              continue;
            }
          }
          
          txtLen = *((USHORT *)(sessionData));
          CHECKED_MALLOC( txtLen + 1, text );
          
          strncpy( text, (char *)(sessionData + 2), txtLen );
          text[ txtLen ] = 0;

          record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
           MRM_CN_SessionContainer, CM_ALLOCRECORD, MPFROMLONG( 0 ),
           MPFROMSHORT( 1 ) );
          if ( !record )
          {
            debugf( "Error allocating record in container.\n" );
            CHECKED_FREE( text );
            CHECKED_FREE( sessionData );
            return 0;
          }
          
          icon = WinQueryWindowULong( win, 16 );
           
          if ( i == cursoredItem )
          {
            cursoredRecord = record;
          }
          
          record->flRecordAttr = 0;
          record->ptlIcon.x = 0;
          record->ptlIcon.y = 0;
          record->pszIcon = text;
          record->hptrIcon = icon;
          insert.cb = sizeof( RECORDINSERT );
          insert.pRecordOrder = (RECORDCORE *)CMA_END;
          insert.pRecordParent = NULL;
          insert.fInvalidateRecord = TRUE;
          insert.zOrder = CMA_TOP;
          insert.cRecordsInsert = 1;
          
          WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_INSERTRECORD,
           MPFROMP( record ), MPFROMP( &insert ) );
          
          DosRequestMutexSem( infoMux, SEM_INDEFINITE_WAIT );
          // Paranoia, but so what...
          if ( !(*numItems) )
          {
            CHECKED_MALLOC( sizeof( sessionThreadInfo ), *info );
            myEntry = 0;
            (*numItems)++;
          } else {
            ULONG k;
            myEntry = *numItems;
            for ( k=0; k<*numItems; ++k )
            {
              if ( !(*info)[k].userName ) myEntry = k;
            }
            if ( *numItems == myEntry )
            {
              DosEnterCritSec();
              
              CHECKED_REALLOC( *info, ((*numItems) + 1) *
               sizeof( sessionThreadInfo ), *info );
              (*numItems)++;
              
              DosExitCritSec();
            }
          }
          
          (*info)[ myEntry ].userName = text;
          CHECKED_MALLOC( sizeof( sessionThreadSettings ), mySettings );
          (*info)[ myEntry ].settings = mySettings;
          
          memcpy( mySettings, initData->globalSettings,
           sizeof( sessionThreadSettings ) );
          mySettings->inheritSettings = 1;
          
          CHECKED_MALLOC( strlen( (*info)[myEntry].userName ) + 10, appName );
          sprintf( appName, "%s Settings", (*info)[myEntry].userName );
          querySessionSettings( initData->iniFile, appName, mySettings );
          CHECKED_FREE( appName );
          appName = NULL;
          
          DosReleaseMutexSem( infoMux );
          
          j = *((USHORT *)(sessionData)) + 2;
          txtLen = *((USHORT *)(sessionData + j));
          
          if ( txtLen > 16 )
          {
            debugf( "Session %s has a password that is %d characters long.\n",
             (*info)[myEntry].userName, txtLen );
            debugf( "This is too long to conform to AOL standards.  Truncating to 16.\n" );
          }
          
          CHECKED_MALLOC( txtLen + 1, text );
          memcpy( text, (char *)(sessionData + j + 2), txtLen );
          decodedPass = (char *)aim_encode_password( text, txtLen );
          strncpy( text, decodedPass, txtLen );
          text[ txtLen ] = 0;
          j += txtLen + 2;
          (*info)[ myEntry ].password = text;
          
          if ( isExtendedInfo )
          {
            txtLen = *((USHORT *)(sessionData + j));
            CHECKED_MALLOC( txtLen + 1, text );
            strncpy( text, (char *)(sessionData + j + 2), txtLen );
            j += txtLen + 2;
            text[ txtLen ] = 0;
            (*info)[ myEntry ].server = text;
            (*info)[ myEntry ].port = *((USHORT *)(sessionData + j));
          } else {
            CHECKED_STRDUP( "login.oscar.aol.com", (*info)[ myEntry ].server );
            (*info)[ myEntry ].port = 5190;
          }
          
          (*info)[ myEntry ].managerWin = win;
          (*info)[ myEntry ].theSession = NULL;
          (*info)[ myEntry ].record = record;
          (*info)[ myEntry ].threadID = 0;
          (*info)[ myEntry ].entryFromIniFile = i;
          
          CHECKED_FREE( sessionData );
        }
      }
      if ( cursoredRecord )
      {
        record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
         MRM_CN_SessionContainer, CM_QUERYRECORD, NULL,
         MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
        WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_SETRECORDEMPHASIS,
         MPFROMP( record ), MPFROM2SHORT( FALSE,
          CRA_SELECTED | CRA_CURSORED ) );
        WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_SETRECORDEMPHASIS,
         MPFROMP( cursoredRecord ), MPFROM2SHORT( TRUE,
          CRA_SELECTED | CRA_CURSORED ) );
      }
      
      for ( myEntry = 0; myEntry < (*numItems); ++myEntry )
      {
        mySettings = (*info)[ myEntry ].settings;
        
        if ( testSettingsFlag( SET_AUTOSTART, mySettings,
              initData->globalSettings ) )
        {
          sessionThreadInitData *init;
          HWND dlg;
          HMODULE dll = getModuleHandle();
          eventData theEventData;
          
          dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmProgressDialog,
           dll, MRM_Progress, NULL );
          // This will be destroyed in its own window proc (I hope!)
          
          CHECKED_MALLOC( sizeof( sessionThreadInitData ), init );
          init->info = info;
          init->myEntry = myEntry;
          init->progressDlg = dlg;
          
          WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
           CM_SETRECORDEMPHASIS, MPFROMP( (*info)[myEntry].record ),
           MPFROM2SHORT( TRUE, CRA_INUSE ) );
          (*info)[ myEntry ].threadID = _beginthread(
           (void (*) (void *))sessionThread, NULL, 65536, init );
          debugf( "Session automatically started in thread %d.\n",
           (*info)[ myEntry ].threadID );
           
          theEventData.currentUser = (*info)[myEntry].userName;
          theEventData.otherUser = NULL;
          theEventData.message = NULL;
          
          handleMrMessageEvent( EVENT_STARTSESSION,
           (*info)[ myEntry ].settings, initData->globalSettings,
           &theEventData, 0 );
          
          if ( testSettingsFlag( SET_AUTOMINIMIZE, (*info)[ myEntry ].settings,
                initData->globalSettings ) )
          {
            WinSetWindowPos( WinQueryWindow( win, QW_PARENT ), NULLHANDLE,
             0, 0, 0, 0, SWP_MINIMIZE );
          }
        }
      }
      // This was put in a separate loop because the info structure can be
      //  reallocated (and hence moved) in the first loop, and the session
      //  startup requires it to remain in place.
      
      WinShowWindow( WinQueryWindow( win, QW_PARENT ), TRUE );
      WinSetFocus( HWND_DESKTOP, WinWindowFromID( win, MRM_CN_SessionContainer ) );
    }
    break;
    case WM_CREATE:
    {
      ULONG tmpUl;
      HWND theWin;
      HMODULE dll = getModuleHandle();
      HPOINTER icon;
      HWND popupMenu;

      WinSetWindowULong( win, 0, 0 );
      WinSetWindowPtr( win, 4, NULL );
      WinSetWindowPtr( win, 8, NULL );
      WinSetWindowULong( win, 12, 0 );
      WinSetWindowULong( win, 52, 1 );
      WinSetWindowULong( win, 56, 0 );
      WinSetWindowULong( win, 64, 0 );
      
      icon = WinLoadPointer( HWND_DESKTOP, dll, 1 );
      WinSetWindowULong( win, 16, icon );
      popupMenu = WinLoadMenu( HWND_DESKTOP, dll, MRM_SessionManagerPopup );
      WinSetWindowULong( win, 24, popupMenu );
      popupMenu = WinLoadMenu( HWND_DESKTOP, dll, MRM_SessionManagerBackPopup );
      WinSetWindowULong( win, 28, popupMenu );
      
      WinSendMsg( WinQueryWindow( win, QW_PARENT ), WM_SETICON,
       MPFROMLONG( icon ), NULL );
      
      // Since we're not taking a shell position, the window dimensions will
      //  be bogus, so don't worry about sizing the child windows yet.
      
      WinSetPresParam( win, PP_FONTNAMESIZE, 13, "12.Helv Bold" );
      // Default font for this window
      
      tmpUl = CLR_PALEGRAY;
      WinSetPresParam( win, PP_BACKGROUNDCOLORINDEX, 4, &tmpUl );
      // Default background color
      
      theWin = WinCreateWindow( win, WC_CONTAINER, "Mr. Message Sessions",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | CCS_AUTOPOSITION |
        CCS_MINIRECORDCORE | CCS_EXTENDSEL,
       0, 0, 0, 0, win, HWND_TOP, MRM_CN_SessionContainer, NULL, NULL );
      
      WinSetWindowPtr( win, 32, WinSubclassWindow( theWin,
       pmPresParamNotifier ) );
       
      theWin = WinCreateWindow( win, WC_BUTTON, "Start",
       WS_DISABLED | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_DEFAULT |
        BS_PUSHBUTTON,
       0, 0, 0, 0, win, HWND_TOP, MRM_StartSession, NULL, NULL );
      
      WinSetWindowPtr( win, 36, WinSubclassWindow( theWin,
       pmPresParamNotifier ) );
       
      theWin = WinCreateWindow( win, WC_BUTTON, "Create...",
       WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON,
       0, 0, 0, 0, win, HWND_TOP, MRM_CreateSession, NULL, NULL );
      
      WinSetWindowPtr( win, 40, WinSubclassWindow( theWin,
       pmPresParamNotifier ) );
       
      theWin = WinCreateWindow( win, WC_BUTTON, "Edit...",
       WS_DISABLED | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON,
       0, 0, 0, 0, win, HWND_TOP, MRM_EditSession, NULL, NULL );
      
      WinSetWindowPtr( win, 44, WinSubclassWindow( theWin,
       pmPresParamNotifier ) );
       
      theWin = WinCreateWindow( win, WC_BUTTON, "Delete",
       WS_DISABLED | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON,
       0, 0, 0, 0, win, HWND_TOP, MRM_DeleteSession, NULL, NULL );
      
      WinSetWindowPtr( win, 48, WinSubclassWindow( theWin,
       pmPresParamNotifier ) );
       
      theWin = WinQueryWindow( win, QW_PARENT );
      theWin = WinWindowFromID( theWin, FID_MENU );
      WinEnableMenuItem( theWin, MRM_StartSession, FALSE );
      WinEnableMenuItem( theWin, MRM_EditSession, FALSE );
      WinEnableMenuItem( theWin, MRM_DeleteSession, FALSE );
    }
    break;
    case WM_SIZE:
    {
      USHORT newCx, newCy, widgetX, widgetY, widgetCx, widgetCy, buttonHeight;
      HWND theWin;
      HPS hps;
      POINTL points[3];
      
      newCx = SHORT1FROMMP( mp2 );
      newCy = SHORT2FROMMP( mp2 );
      
      hps = WinGetPS( WinWindowFromID( win, MRM_StartSession ) );
      // Make sure we pick up presentation params appropriate to the buttons
      //  when sizing the text.
      GpiQueryTextBox( hps, 24, "Create Start Edit Delete", 3,
       (PPOINTL) points );
      buttonHeight = (USHORT) (points[0].y - points[1].y + 8);
      
      WinReleasePS( hps );
      
      theWin = WinWindowFromID( win, MRM_CN_SessionContainer );
      widgetX = 8;
      widgetCx = newCx - 16;
      widgetY = buttonHeight + 8;
      widgetCy = newCy - widgetY - 8;
      WinSetWindowPos( theWin, 0, widgetX, widgetY, widgetCx, widgetCy,
       SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, MRM_StartSession );
      widgetX = 8;
      widgetCx = (newCx / 4) - 16;
      widgetY = 0;
      widgetCy = buttonHeight;
      WinSetWindowPos( theWin, 0, widgetX, widgetY, widgetCx, widgetCy,
       SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, MRM_CreateSession );
      widgetX += widgetCx + 16;
      WinSetWindowPos( theWin, 0, widgetX, widgetY, widgetCx, widgetCy,
       SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, MRM_EditSession );
      widgetX += widgetCx + 16;
      WinSetWindowPos( theWin, 0, widgetX, widgetY, widgetCx, widgetCy,
       SWP_SIZE | SWP_MOVE );
      
      theWin = WinWindowFromID( win, MRM_DeleteSession );
      widgetX += widgetCx + 16;
      widgetCx = newCx - (widgetX + 8);
      WinSetWindowPos( theWin, 0, widgetX, widgetY, widgetCx, widgetCy,
       SWP_SIZE | SWP_MOVE );
    }
    break;
    case WM_PAINT:
    {
      HPS hps;
      RECTL rect;
      POINTL pt;
      ULONG colorIdx, ret;
      
      ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
       &colorIdx, QPF_ID1COLORINDEX | QPF_NOINHERIT );
      if ( !ret )
      {
        ret = WinQueryPresParam( win, PP_BACKGROUNDCOLOR, 0, NULL, 4,
         &colorIdx, QPF_NOINHERIT );
        if ( !ret )
        {
          ret = WinQueryPresParam( win, PP_BACKGROUNDCOLORINDEX, 0, NULL, 4,
           &colorIdx, QPF_ID1COLORINDEX );
        }
      }
      
      hps = WinBeginPaint( win, NULLHANDLE, &rect );
      if ( ret < 4 )
        GpiSetColor( hps, CLR_PALEGRAY );
      else {
        GpiCreateLogColorTable( hps, 0, LCOLF_RGB, 0, 0, NULL );
        GpiSetColor( hps, colorIdx );
      }
      pt.x = rect.xLeft;
      pt.y = rect.yBottom;
      GpiMove( hps, &pt );
      pt.x = rect.xRight;
      pt.y = rect.yTop;
      GpiBox( hps, DRO_FILL, &pt, 0, 0 );
      WinEndPaint( hps );
    }
    break;
    case WM_COMMAND:
    {
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case MRM_About:
        {
          HMODULE dll = getModuleHandle();
          HWND dlgWin;
          SWP swp;
          RECTL desktop, dlgSize;
          
          dlgWin = WinLoadDlg( HWND_DESKTOP, win, WinDefDlgProc, dll,
           MRM_AboutBox, NULL );
          
          WinQueryWindowRect( HWND_DESKTOP, &desktop );
          WinQueryWindowRect( dlgWin, &dlgSize );
          
          swp.x = desktop.xLeft + ((desktop.xRight - desktop.xLeft) / 2) -
           ((dlgSize.xRight - dlgSize.xLeft) / 2);
          swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom) / 2) -
           ((dlgSize.yTop - dlgSize.yBottom) / 2);
          swp.cx = dlgSize.xRight - dlgSize.xLeft;
          swp.cy = dlgSize.yTop - dlgSize.yBottom;
          // Establish defaults
            
          WinSetWindowPos( dlgWin, 0, swp.x, swp.y, swp.cx, swp.cy,
           SWP_SIZE | SWP_MOVE | SWP_SHOW );
          
          playMediaFile( "goodisdum.wav" );
          
          WinProcessDlg( dlgWin );
          WinDestroyWindow( dlgWin );
        }
        break;
        case MRM_SelectAll:
        case MRM_DeselectAll:
        {
          MINIRECORDCORE *record;
          HWND theWin = WinWindowFromID( win, MRM_CN_SessionContainer );
          record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD,
           MPFROMSHORT( CMA_FIRST ), MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
          while ( record && (LONG) record != -1 )
          {
            if ( (!(record->flRecordAttr & CRA_SELECTED) &&
                  SHORT1FROMMP( mp1 ) == MRM_SelectAll) ||
                 ((record->flRecordAttr & CRA_SELECTED) &&
                  SHORT1FROMMP( mp1 ) == MRM_DeselectAll) )
            {
              WinSendMsg( theWin, CM_SETRECORDEMPHASIS, MPFROMP( record ),
               MPFROM2SHORT( (SHORT1FROMMP( mp1 ) == MRM_SelectAll),
               CRA_SELECTED ) );
            }
            record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD,
             MPFROMP( record ), MPFROM2SHORT( CMA_NEXT, CMA_ITEMORDER ) );
          }
        }
        break;
        case MRM_ExitAll:
          WinPostMsg( win, WM_CLOSE, NULL, NULL );
        break;
        case MRM_SessionSettings:
        case MRM_GlobalSettings:
        {
          ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
          sessionThreadInfo **info =
           (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
          HINI iniFile = WinQueryWindowULong( win, 20 );
          HMODULE dll = getModuleHandle();
          HWND dlgWin;
          RECTL desktop, dlgSize;
          SWP swp;
          ULONG tmpLong, i;
          SHORT idx, listLen, j;
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( win, 60 );
          sessionThreadSettings *mySettings;
          settingsDialogInit init;
          
          init.info = info;
          init.globalSettings = globalSettings;

          dlgWin = WinLoadDlg( HWND_DESKTOP, WinQueryWindow( win, QW_PARENT ),
           pmSettings, dll, MRM_SettingsWindow, &init );
          
          WinSendDlgItemMsg( dlgWin, MRM_EN_EventPlaySoundFile,
           EM_SETTEXTLIMIT, MPFROMSHORT( CCHMAXPATH ), NULL );
          WinSendDlgItemMsg( dlgWin, MRM_EN_EventRunREXXScript,
           EM_SETTEXTLIMIT, MPFROMSHORT( CCHMAXPATH ), NULL );
          WinSendDlgItemMsg( dlgWin, MRM_EN_EventRunCommand,
           EM_SETTEXTLIMIT, MPFROMSHORT( CCHMAXPATH ), NULL );
          
          for ( i=0; i<EVENT_MAXEVENTS; ++i )
          {
            WinSendDlgItemMsg( dlgWin, MRM_LB_EventType, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( UI_EventTypeStrings[i] ) );
          }
          
          for ( i=0; i<*numItems; ++i )
          {
            idx = SHORT1FROMMR( WinSendDlgItemMsg( dlgWin,
             MRM_LB_SettingsScope, LM_INSERTITEM,
             MPFROMSHORT( LIT_SORTASCENDING ),
             MPFROMP( (*info)[i].userName ) ) );
            WinSendDlgItemMsg( dlgWin, MRM_LB_SettingsScope, LM_SETITEMHANDLE,
             MPFROMSHORT( idx ), MPFROMLONG( i ) );
          }
          idx = SHORT1FROMMR( WinSendDlgItemMsg( dlgWin, MRM_LB_SettingsScope,
           LM_INSERTITEM, MPFROMSHORT( LIT_END ), MPFROMP( "All Sessions" ) ) );
          WinSendDlgItemMsg( dlgWin, MRM_LB_SettingsScope, LM_SETITEMHANDLE,
           MPFROMSHORT( idx ), MPFROMLONG( 0xffffffff ) );

          if ( SHORT1FROMMP( mp1 ) == MRM_GlobalSettings )
          {
            WinSendDlgItemMsg( dlgWin, MRM_LB_SettingsScope, LM_SELECTITEM,
             MPFROMSHORT( idx ), MPFROMLONG( TRUE ) );
            mySettings = globalSettings;
          } else {
            MINIRECORDCORE *record = NULL;
            
            if ( SHORT1FROMMP( mp2 ) & CMDSRC_MENU )
            {
              // Sent from a menu.  Check if it's a popup menu.
              record = (MINIRECORDCORE *) WinQueryWindowPtr(
               WinWindowFromID( win, MRM_CN_SessionContainer ), QWL_USER );
              WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
               QWL_USER, NULL );
            } else {
              // Allow an override from the buddy list window
              record = (MINIRECORDCORE *) WinQueryWindowPtr( win, 64 );
            }
            
            if ( !record )
            {
              record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
               MRM_CN_SessionContainer, CM_QUERYRECORDEMPHASIS,
               MPFROMLONG( CMA_FIRST ), MPFROMSHORT( CRA_SELECTED ) );
              
              if ( !record || (LONG)record == -1 )
              {
                WinAlarm( HWND_DESKTOP, WA_ERROR );
                debugf( "Attempted to pull up specific settings with nothing selected.\n" );
                WinDestroyWindow( dlgWin );
                return NULL;
              }
            }
            
            mySettings = NULL;
            for ( i=0; i<(*numItems); ++i )
            {
              if ( (*info)[i].userName && (*info)[i].record == record )
              {
                mySettings = (*info)[i].settings;
                listLen = idx;
                for ( j=0; j<listLen; ++j )
                {
                  if ( i == (unsigned long) WinSendDlgItemMsg(
                             dlgWin, MRM_LB_SettingsScope, LM_QUERYITEMHANDLE,
                             MPFROMSHORT( j ), NULL ) )
                  {
                    WinSendDlgItemMsg( dlgWin, MRM_LB_SettingsScope,
                     LM_SELECTITEM, MPFROMSHORT( j ), MPFROMLONG( TRUE ) );
                    break;
                  }
                }
                break;
              }
            }
          }
          
          WinQueryWindowRect( HWND_DESKTOP, &desktop );
          WinQueryWindowRect( dlgWin, &dlgSize );
          
          swp.x = desktop.xLeft + ((desktop.xRight - desktop.xLeft) / 2) -
           ((dlgSize.xRight - dlgSize.xLeft) / 2);
          swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom) / 2) -
           ((dlgSize.yTop - dlgSize.yBottom) / 2);
          swp.cx = dlgSize.xRight - dlgSize.xLeft;
          swp.cy = dlgSize.yTop - dlgSize.yBottom;
          // Establish defaults
            
          tmpLong = sizeof( SWP );
          PrfQueryProfileData( iniFile, "Settings Dialog",
           "Window position data", &swp, &tmpLong );
          // Read INI settings if there are any
          
          WinSetWindowPos( dlgWin, 0, swp.x, swp.y, swp.cx, swp.cy,
           SWP_MOVE | SWP_SHOW );
          
          WinProcessDlg( dlgWin );
          // Settings values are updated in the dialog window procedure
          
          WinQueryWindowPos( dlgWin, &swp );
          if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
          {
            swp.x = WinQueryWindowUShort( dlgWin, QWS_XRESTORE );
            swp.y = WinQueryWindowUShort( dlgWin, QWS_YRESTORE );
            swp.cx = WinQueryWindowUShort( dlgWin, QWS_CXRESTORE );
            swp.cy = WinQueryWindowUShort( dlgWin, QWS_CYRESTORE );
          }
          
          PrfWriteProfileData( iniFile, "Settings Dialog",
           "Window position data", &swp, sizeof( SWP ) );
          
          saveSessionSettings( iniFile, "Global Settings", globalSettings );
          for ( i=0; i<(*numItems); ++i )
          {
            if ( (*info)[i].userName )
            {
              char *appName;
              CHECKED_MALLOC( strlen( (*info)[i].userName ) + 10, appName );
              sprintf( appName, "%s Settings", (*info)[i].userName );
              saveSessionSettings( iniFile, appName, (*info)[i].settings );
              CHECKED_FREE( appName );
            }
          }
          
          WinDestroyWindow( dlgWin );
        }
        break;
        case MRM_CreateSession:
        {
          HWND dlgWin;
          RECTL desktopSize, dlgSize;
          HPOINTER icon;
          HMODULE dll = getModuleHandle();
          ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
          sessionThreadInfo **info =
           (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
          HMTX infoMux = WinQueryWindowULong( win, 12 );
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( win, 60 );
          
          dlgWin = WinLoadDlg( HWND_DESKTOP, win, pmSessionEditor,
           dll, 1, NULL );
          WinQueryWindowRect( HWND_DESKTOP, &desktopSize );
          WinQueryWindowRect( dlgWin, &dlgSize );
          
          WinSetWindowPos( dlgWin, HWND_TOP,
           desktopSize.xLeft + (
            ((desktopSize.xRight - desktopSize.xLeft) -
             (dlgSize.xRight - dlgSize.xLeft)) / 2),
           desktopSize.yBottom + (
            ((desktopSize.yTop - desktopSize.yBottom) -
             (dlgSize.yTop - dlgSize.yBottom)) / 2),
           0, 0, SWP_MOVE );
          // Center the dialog window
          
          if ( WinProcessDlg( dlgWin ) == DID_OK )
          {
            char *text;
            ULONG txtLen, myEntry;
            MINIRECORDCORE *record;
            RECORDINSERT insert;
            
            debugf( "Adding a session record.\n" );
            
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ScreenName ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ScreenName, txtLen, text );
            record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
             MRM_CN_SessionContainer, CM_ALLOCRECORD, MPFROMLONG( 0 ),
             MPFROMSHORT( 1 ) );
            if ( !record )
            {
              debugf( "Error allocating record in container.\n" );
              CHECKED_FREE( text );
              WinDestroyWindow( dlgWin );
              return 0;
            }
            
            icon = WinQueryWindowULong( win, 16 );
            
            record->flRecordAttr = CRA_CURSORED;
            record->ptlIcon.x = 0;
            record->ptlIcon.y = 0;
            record->pszIcon = text;
            record->hptrIcon = icon;
            insert.cb = sizeof( RECORDINSERT );
            insert.pRecordOrder = (RECORDCORE *)CMA_END;
            insert.pRecordParent = NULL;
            insert.fInvalidateRecord = TRUE;
            insert.zOrder = CMA_TOP;
            insert.cRecordsInsert = 1;
            
            WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_INSERTRECORD,
             MPFROMP( record ), MPFROMP( &insert ) );
            
            DosRequestMutexSem( infoMux, SEM_INDEFINITE_WAIT );
            if ( !(*numItems) )
            {
              CHECKED_MALLOC( sizeof( sessionThreadInfo ), *info );
              myEntry = 0;
              (*numItems)++;
            } else {
              ULONG i;
              myEntry = *numItems;
              for ( i=0; i<*numItems; ++i )
              {
                if ( !(*info)[i].userName ) myEntry = i;
              }
              if ( *numItems == myEntry )
              {
                CHECKED_REALLOC( *info, ((*numItems) + 1) *
                 sizeof( sessionThreadInfo ), *info );
                (*numItems)++;
              }
            }
            
            (*info)[ myEntry ].userName = text;
            DosReleaseMutexSem( infoMux );
            
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_Password ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_Password, txtLen, text );
            (*info)[ myEntry ].password = text;
            
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ServerName ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ServerName, txtLen, text );
            (*info)[ myEntry ].server = text;
            
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ServerPort ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ServerPort, txtLen, text );
            (*info)[ myEntry ].port = atol( text );
            CHECKED_FREE( text );
            
            (*info)[ myEntry ].managerWin = win;
            (*info)[ myEntry ].theSession = NULL;
            (*info)[ myEntry ].record = record;
            (*info)[ myEntry ].threadID = 0;
            (*info)[ myEntry ].entryFromIniFile = 0;
            
            CHECKED_MALLOC( sizeof( sessionThreadSettings ),
             (*info)[ myEntry ].settings );
            memcpy( (*info)[ myEntry ].settings, globalSettings,
             sizeof( sessionThreadSettings ) );
            (*info)[ myEntry ].settings->inheritSettings = 1;
          }
          
          WinDestroyWindow( dlgWin );
        }
        break;
        case MRM_DeleteSession:
        {
          HWND theWin;
          MINIRECORDCORE *record, *prevRecord;
          ULONG i, onlyOneRecord;
          ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
          sessionThreadInfo **info =
           (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( win, 60 );
         
          theWin = WinWindowFromID( win, MRM_CN_SessionContainer );
          debugf( "User requested to delete record(s).\n" );
          
          record = NULL;
          
          if ( SHORT1FROMMP( mp2 ) & CMDSRC_MENU )
          {
            // Sent from a menu.  Check if it's a popup menu.
            record = (MINIRECORDCORE *) WinQueryWindowPtr(
             WinWindowFromID( win, MRM_CN_SessionContainer ), QWL_USER );
            WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
             QWL_USER, NULL );
            if ( record )
              debugf( "Popup menu acted on %s.\n", record->pszIcon );
          }
          
          if ( record && !(record->flRecordAttr & CRA_SELECTED) )
          {
            // Source emphasis, but not selected.  Act only on this object.
            onlyOneRecord = 1;
            prevRecord = (MINIRECORDCORE *)CMA_FIRST;
            debugf( "Only one record to operate on.\n" );
          } else {
            onlyOneRecord = 0;
            record = (MINIRECORDCORE *)CMA_FIRST;
            debugf( "Operating on all selected records.\n" );
          }
          
          do
          {
            if ( !onlyOneRecord )
            {
              prevRecord = record;
              record = (MINIRECORDCORE *) WinSendMsg( theWin,
               CM_QUERYRECORDEMPHASIS, MPFROMP( record ),
               MPFROMSHORT( CRA_SELECTED ) );
              if ( !record || (LONG)record == -1 ) break;
            }
            
            for ( i=0; i<(*numItems); ++i )
            {
              if ( (*info)[i].userName && (*info)[i].record == record )
              {
                if ( (*info)[i].theSession || (*info)[i].threadID )
                {
                  // Session is running... why are we deleting it?!
                  char *titleString;
                  sessionThreadSettings *mySettings = (*info)[i].settings;
                  eventData theEventData;
                  char *errMsg = "Session is currently opened and running.  You cannot delete it.  Please close the session if you wish to delete it."; 
                  
                  CHECKED_MALLOC( 24 + strlen( (*info)[i].userName ),
                   titleString );
                  
                  sprintf( titleString, "Cannot remove session: %s",
                   (*info)[i].userName );
                  
                  theEventData.currentUser = (*info)[i].userName;
                  theEventData.otherUser = NULL;
                  theEventData.message = "CANNOT DELETE SESSION";
                  handleMrMessageEvent( EVENT_ERRORBOX, (*info)[i].settings,
                   globalSettings, &theEventData, 0 );
                  
                  WinMessageBox( HWND_DESKTOP, win, errMsg, titleString, 502,
                   MB_CANCEL | MB_APPLMODAL | MB_MOVEABLE );
                  CHECKED_FREE( titleString );
                } else {
                  HMTX infoMux = WinQueryWindowULong( win, 12 );
                  
                  if ( (*info)[i].entryFromIniFile )
                  {
                    HINI iniFile = WinQueryWindowULong( win, 20 );
                    char keyString[80];
                    
                    sprintf( (char *)keyString, "Session %d data",
                     (*info)[i].entryFromIniFile );
                    
                    PrfWriteProfileData( iniFile, "Session Manager", keyString,
                     NULL, 0 );
                    // Clear the old information out of the profile so that
                    //  the password can't be gotten if a guest uses the
                    //  messenger temporarily.
                  }
                  
                  DosRequestMutexSem( infoMux, SEM_INDEFINITE_WAIT );
                  (*info)[i].record = NULL;
                  if ( (*info)[i].userName )
                    CHECKED_FREE( (*info)[i].userName );
                  if ( (*info)[i].password )
                    CHECKED_FREE( (*info)[i].password );
                  (*info)[i].userName = NULL;
                  destroySessionSettings( (*info)[i].settings );
                  (*info)[i].settings = NULL;
                  DosReleaseMutexSem( infoMux );
                  
                  WinSendMsg( theWin, CM_SETRECORDEMPHASIS, MPFROMP( record ),
                   MPFROM2SHORT( FALSE, CRA_SELECTED ) );
                  WinSendMsg( theWin, CM_REMOVERECORD, MPFROMP( &record ),
                   MPFROM2SHORT( 1, CMA_FREE | CMA_INVALIDATE ) );
                  
                  record = prevRecord;
                  // Need to back up one step so we don't miss something
                  debugf( "Record was removed.\n" );
                }
              }
            }
            
            if ( onlyOneRecord ) break;
          } while ( 1 );
        }
        break;
        case MRM_StartSession:
        {
          HWND theWin;
          MINIRECORDCORE *record = NULL;
          ULONG i, onlyOneRecord;
          ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
          sessionThreadInfo **info =
           (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( win, 60 );
          eventData theEventData;
          
          theWin = WinWindowFromID( win, MRM_CN_SessionContainer );
          debugf( "User requested to start session(s).\n" );
          
          if ( SHORT1FROMMP( mp2 ) & CMDSRC_MENU )
          {
            // Sent from a menu.  Check if it's a popup menu.
            record = (MINIRECORDCORE *) WinQueryWindowPtr(
             WinWindowFromID( win, MRM_CN_SessionContainer ), QWL_USER );
            WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
             QWL_USER, NULL );
          }
          
          if ( record && !(record->flRecordAttr & CRA_SELECTED) )
          {
            // Source emphasis, but not selected, act only on this object
            onlyOneRecord = 1;
          } else {
            onlyOneRecord = 0;
            record = (MINIRECORDCORE *)CMA_FIRST;
          }
          
          do {
            if ( !onlyOneRecord )
            {
              record = (MINIRECORDCORE *) WinSendMsg( theWin,
               CM_QUERYRECORDEMPHASIS, MPFROMP( record ),
               MPFROMSHORT( CRA_SELECTED ) );
              if ( !record || (LONG)record == -1 ) break;
            }
            
            for ( i=0; i<(*numItems); ++i )
            {
              if ( (*info)[i].userName && (*info)[i].record == record )
              {
                if ( !(record->flRecordAttr & CRA_INUSE) )
                {
                  sessionThreadInitData *init;
                  HWND dlg;
                  HMODULE dll = getModuleHandle();
                  
                  dlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP,
                   pmProgressDialog, dll, MRM_Progress, NULL );
                  // This will be destroyed in its own window proc (I hope!)
                  
                  CHECKED_MALLOC( sizeof( sessionThreadInitData ), init );
                  init->info = info;
                  init->myEntry = i;
                  init->progressDlg = dlg;
                  
                  WinSendMsg( theWin, CM_SETRECORDEMPHASIS, MPFROMP( record ),
                   MPFROM2SHORT( TRUE, CRA_INUSE ) );
                  (*info)[i].threadID = _beginthread(
                   (void (*) (void *))sessionThread, NULL, 65536, init );
                  debugf( "Session started in thread %d.\n",
                   (*info)[i].threadID );
                  
                  theEventData.currentUser = (*info)[i].userName;
                  theEventData.otherUser = NULL;
                  theEventData.message = NULL;
                  handleMrMessageEvent( EVENT_STARTSESSION,
                   (*info)[i].settings, globalSettings, &theEventData, 0 );
                  if ( testSettingsFlag( SET_AUTOMINIMIZE, (*info)[i].settings,
                        globalSettings ) )
                  {
                    WinSetWindowPos( WinQueryWindow( win, QW_PARENT ),
                     NULLHANDLE, 0, 0, 0, 0, SWP_MINIMIZE );
                  }
                } else {
                  debugf( "Attempted to launch an already active session for %s.\n",
                   (*info)[i].userName );
                  if ( (*info)[i].theSession )
                  {
                    WinSetFocus( HWND_DESKTOP,
                     (*info)[i].theSession->getBuddyListWindow() );
                  }
                  // Bring the existing session to the front
                }
                break;
              }
            }
            
            if ( onlyOneRecord ) break;
          } while ( 1 );
        }
        break;
        case MRM_EditSession:
        {
          HMODULE dll = getModuleHandle();
          HWND dlgWin;
          RECTL desktopSize, dlgSize;
          sessionEditorInit initData;
          ULONG i, myEntry;
          MINIRECORDCORE *record = NULL;
          ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
          sessionThreadInfo **info =
           (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
          sessionThreadSettings *globalSettings = (sessionThreadSettings *)
           WinQueryWindowPtr( win, 60 );
          
          if ( SHORT1FROMMP( mp2 ) & CMDSRC_MENU )
          {
            // Sent from a menu.  Check if it's a popup menu.
            record = (MINIRECORDCORE *) WinQueryWindowPtr(
             WinWindowFromID( win, MRM_CN_SessionContainer ), QWL_USER );
            WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
             QWL_USER, NULL );
          }
          
          if ( !record )
          {
            record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
             MRM_CN_SessionContainer, CM_QUERYRECORDEMPHASIS,
             MPFROMLONG( CMA_FIRST ), MPFROMSHORT( CRA_SELECTED ) );
            
            if ( !record || (LONG)record == -1 )
            {
              debugf( "Couldn't find the selected record!\n" );
              WinAlarm( HWND_DESKTOP, WA_ERROR );
              return NULL;
            }
          }
          
          myEntry = *numItems;
          for ( i=0; i<(*numItems); ++i )
          {
            if ( (*info)[i].userName && (*info)[i].record == record )
            {
              myEntry = i;
              break;
            }
          }
          
          if ( myEntry == *numItems )
          {
            debugf( "Couldn't figure out the selected record!\n" );
            WinAlarm( HWND_DESKTOP, WA_ERROR );
            return NULL;
          }
          
          if ( ((*info)[myEntry].theSession || (*info)[myEntry].threadID) &&
                !WinQueryWindowULong( win, 56 ) )
          {
            // Session is running... why are we editting it?!
            char *titleString;
            eventData theEventData;
            
            CHECKED_MALLOC( 24 + strlen( (*info)[myEntry].userName ), titleString );
            
            sprintf( titleString, "Cannot modify session: %s",
             (*info)[myEntry].userName );
            
            theEventData.currentUser = (*info)[myEntry].userName;
            theEventData.otherUser = NULL;
            theEventData.message = "CANNOT MODIFY SESSION";
            handleMrMessageEvent( EVENT_ERRORBOX, (*info)[myEntry].settings,
             globalSettings, &theEventData, 0 );
            
            WinMessageBox( HWND_DESKTOP, win,
             "Session is currently opened and running.  You cannot modify it.  Please close the session and then proceed with your edits.",
             titleString, 503,
             MB_CANCEL | MB_APPLMODAL | MB_MOVEABLE );
            CHECKED_FREE( titleString );
            return NULL;
          }
                  
          initData.userName = (*info)[myEntry].userName;
          initData.password = (*info)[myEntry].password;
          initData.server = (*info)[myEntry].server;
          initData.port = (*info)[myEntry].port;
          
          dlgWin = WinLoadDlg( HWND_DESKTOP, win, pmSessionEditor,
           dll, 1, &initData );
          WinQueryWindowRect( HWND_DESKTOP, &desktopSize );
          WinQueryWindowRect( dlgWin, &dlgSize );
          
          WinSetWindowPos( dlgWin, HWND_TOP,
           desktopSize.xLeft + (
            ((desktopSize.xRight - desktopSize.xLeft) -
             (dlgSize.xRight - dlgSize.xLeft)) / 2),
           desktopSize.yBottom + (
            ((desktopSize.yTop - desktopSize.yBottom) -
             (dlgSize.yTop - dlgSize.yBottom)) / 2),
           0, 0, SWP_MOVE );
          // Center the dialog window
          
          if ( WinProcessDlg( dlgWin ) == DID_OK )
          {
            char *text, *tmpString;
            ULONG txtLen;
            HINI iniFile = WinQueryWindowULong( win, 20 );
            
            debugf( "Modifying a session record.\n" );
            
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ScreenName ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ScreenName, txtLen, text );
            tmpString = (*info)[myEntry].userName;
            (*info)[myEntry].userName = text;
            record->pszIcon = text;
            
            renameSessionData( iniFile, tmpString, text );
            
            CHECKED_FREE( tmpString );
            WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
             CM_INVALIDATERECORD, MPFROMP( &record ),
             MPFROM2SHORT( 1, CMA_TEXTCHANGED ) );
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_Password ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_Password, txtLen, text );
            tmpString = (*info)[myEntry].password;
            (*info)[myEntry].password = text;
            CHECKED_FREE( tmpString );
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ServerName ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ServerName, txtLen, text );
            tmpString = (*info)[myEntry].server;
            (*info)[myEntry].server = text;
            CHECKED_FREE( tmpString );
            txtLen = WinQueryDlgItemTextLength( dlgWin, MRM_EN_ServerPort ) + 1;
            CHECKED_MALLOC( txtLen, text );
            WinQueryDlgItemText( dlgWin, MRM_EN_ServerPort, txtLen, text );
            (*info)[myEntry].port = atol( text );
            CHECKED_FREE( text );
          }
          WinDestroyWindow( dlgWin );
        }
        break;
        case MRM_AudioVolume:
        {
          HWND volumeDlg, popupWin;
          HMODULE dll = getModuleHandle();
          HINI iniFile = WinQueryWindowULong( win, 20 );
          SWP swp;
          ULONG len;
          RECTL desktop, dlgSize;
          
          popupWin = WinQueryWindowULong( win, 28 );
          
          WinEnableMenuItem( WinWindowFromID( WinQueryWindow( win, QW_PARENT ),
           FID_MENU ), MRM_AudioVolume, FALSE );
          WinEnableMenuItem( popupWin, MRM_AudioVolume, FALSE );
          
          volumeDlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, pmVolumeDialog,
           dll, MRM_AudioVolumeControl, NULL );
          
          WinQueryWindowRect( HWND_DESKTOP, &desktop );
          WinQueryWindowRect( volumeDlg, &dlgSize );
          
          swp.x = desktop.xLeft + ((desktop.xRight - desktop.xLeft) / 2) -
           ((dlgSize.xRight - dlgSize.xLeft) / 2);
          swp.y = desktop.yBottom + ((desktop.yTop - desktop.yBottom) / 2) -
           ((dlgSize.yTop - dlgSize.yBottom) / 2);
          swp.cx = dlgSize.xRight - dlgSize.xLeft;
          swp.cy = dlgSize.yTop - dlgSize.yBottom;
          // Establish defaults
          
          len = sizeof( SWP );
          PrfQueryProfileData( iniFile, "Volume control",
           "Window position data", &swp, &len );
          
          WinSetWindowPos( volumeDlg, 0, swp.x, swp.y, swp.cx, swp.cy,
           SWP_MOVE | SWP_SHOW );
          
          WinProcessDlg( volumeDlg );
          
          WinQueryWindowPos( volumeDlg, &swp );
          if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
          {
            swp.x = WinQueryWindowUShort( volumeDlg, QWS_XRESTORE );
            swp.y = WinQueryWindowUShort( volumeDlg, QWS_YRESTORE );
            swp.cx = WinQueryWindowUShort( volumeDlg, QWS_CXRESTORE );
            swp.cy = WinQueryWindowUShort( volumeDlg, QWS_CYRESTORE );
          }
          
          PrfWriteProfileData( iniFile, "Volume control",
           "Window position data", &swp, sizeof( SWP ) );
          
          WinDestroyWindow( volumeDlg );
          
          WinEnableMenuItem( WinWindowFromID( WinQueryWindow( win, QW_PARENT ),
           FID_MENU ), MRM_AudioVolume, TRUE );
          WinEnableMenuItem( popupWin, MRM_AudioVolume, TRUE );
        }
        break;
      }
    }
    break;
    case WM_GIMMEDROPCOPYDATA:
    {
      MINIRECORDCORE *theRecord;
      char *sharedMem;
      ULONG i;
      ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
      sessionThreadInfo **info =
       (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
      HMTX infoMux = WinQueryWindowULong( win, 12 );
      
      debugf( "Another window requested dragged data for copy operation.\n" );
      
      sharedMem = (char *) PVOIDFROMMP( mp1 );
      theRecord = (MINIRECORDCORE *) PVOIDFROMMP( mp2 );
      
      DosGetSharedMem( sharedMem, PAG_WRITE );
      DosRequestMutexSem( infoMux, SEM_INDEFINITE_WAIT );
      for ( i=0; i<(*numItems); ++i )
      {
        if ( (*info)[i].userName && (*info)[i].record == theRecord )
        {
          if ( strlen( (*info)[i].userName ) + strlen( (*info)[i].password ) +
                strlen( (*info)[i].server ) + 5 > 4096 )
          {
            debugf( "Dragged data strings are way bigger than expected.  Aborting.\n" );
            memset( sharedMem, 0, 5 );
          } else {
            int spot;
            
            strcpy( sharedMem, (*info)[i].userName );
            spot = strlen( (*info)[i].userName );
            strcpy( sharedMem + spot + 1, (*info)[i].password );
            spot += 1 + strlen( (*info)[i].password );
            strcpy( sharedMem + spot + 1, (*info)[i].server );
            spot += 1 + strlen( (*info)[i].server );
            *((USHORT *)(sharedMem + spot)) = (*info)[i].port;
          }
          break;
        }
      }
      DosReleaseMutexSem( infoMux );
      DosFreeMem( sharedMem );
    }
    break;
    case WM_WINPPCHANGED:
    {
      HINI iniFile = WinQueryWindowULong( win, 20 );
      ULONG ppType = LONGFROMMP( mp2 );
      ULONG lenRet;
      char keyName[10];
      char buffer[1024];
      HWND theWin;
      // Presentation parameter changed on a subclassed child window
      
      if ( !WinQueryWindowULong( win, 52 ) ) break;
      // Already handling button presentation parameter changes.  Just fall
      //  through to the default window procedure.
      
      if ( !iniFile ) break;
      // INI file hasn't been opened yet - PresParams changed during WM_CREATE
      
      if ( SHORT1FROMMP( mp1 ) )
      {
        theWin = WinWindowFromID( win, SHORT1FROMMP( mp1 ) );
      } else {
        theWin = win;
      }
      
      if ( ppType == PP_FONTNAMESIZE && SHORT1FROMMP( mp1 ) > 100 &&
            SHORT1FROMMP( mp1 ) < 105 )
      {
        // May need to resize buttons
        RECTL winRect;
        WinQueryWindowRect( win, &winRect );
        WinPostMsg( win, WM_SIZE,
         MPFROM2SHORT( winRect.xRight - winRect.xLeft,
                       winRect.yTop - winRect.yBottom ),
         MPFROM2SHORT( winRect.xRight - winRect.xLeft,
                       winRect.yTop - winRect.yBottom ) );
      }
      
      lenRet = WinQueryPresParam( theWin, ppType, 0, NULL, 1024, buffer, 0 );
      // No way to query PP size that I know of.  Goofy.
      
      if ( (ppType == PP_BACKGROUNDCOLORINDEX ||
            ppType == PP_BACKGROUNDCOLOR) && SHORT1FROMMP( mp1 ) == 0 )
      {
        WinInvalidateRect( win, NULL, TRUE );
        // Make sure background is repainted
      }
      
      if ( lenRet )
      {
        sprintf( keyName, "%d", ppType );
        
        if ( theWin == win )
        {
          PrfWriteProfileData( iniFile, "Session Manager Presentation Parameters",
           keyName, buffer, lenRet );
        } else {
          if ( SHORT1FROMMP( mp1 ) > 100 && SHORT1FROMMP( mp1 ) < 105 )
          {
            // One of the buttons.  Clone the presentation parameters across
            //  all buttons.
            WinSetWindowULong( win, 52, 0 );
            // Prevent recursion.
            
            PrfWriteProfileData( iniFile, "Session Manager Presentation Parameters (Buttons)",
             keyName, buffer, lenRet );
            WinSetPresParam( WinWindowFromID( win, MRM_StartSession ),
             ppType, lenRet, buffer );
            WinSetPresParam( WinWindowFromID( win, MRM_CreateSession ),
             ppType, lenRet, buffer );
            WinSetPresParam( WinWindowFromID( win, MRM_EditSession ),
             ppType, lenRet, buffer );
            WinSetPresParam( WinWindowFromID( win, MRM_DeleteSession ),
             ppType, lenRet, buffer );
            
            WinSetWindowULong( win, 52, 1 );
          } else {
            PrfWriteProfileData( iniFile, "Session Manager Presentation Parameters (Container)",
             keyName, buffer, lenRet );
          }
        }
      }
    }
    break;
    case WM_PRESPARAMCHANGED:
      WinPostMsg( win, WM_WINPPCHANGED, 0, mp1 );
    break;
    case WM_CLOSE:
    {
      HWND theWin;
      MINIRECORDCORE *record;
      HPOINTER icon;
      ULONG dataLen, bigDataLen, i, txtLen, numRealSessions;
      unsigned char *prfData;
      HINI iniFile = WinQueryWindowULong( win, 20 );
      ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
      ULONG cursoredItem;
      sessionThreadInfo **info =
       (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
      HWND menuWin, frameWin;
      char keyName[80];
      SWP swp;
      
      for ( i=0; i<(*numItems); ++i )
      {
        if ( (*info)[i].userName && (*info)[i].theSession )
        {
          debugf( "Telling session %d to close due to Session Manager exit.\n",
           i );
          (*info)[i].theSession->closeSession();
        }
      }
      
      debugf( "Saving changes to profile.\n" );
      
      frameWin = WinQueryWindow( win, QW_PARENT );
      theWin = WinWindowFromID( win, MRM_CN_SessionContainer );
      WinQueryWindowPos( frameWin, &swp );
      if ( (swp.fl & SWP_MINIMIZE) || (swp.fl & SWP_MAXIMIZE) )
      {
        swp.x = WinQueryWindowUShort( frameWin, QWS_XRESTORE );
        swp.y = WinQueryWindowUShort( frameWin, QWS_YRESTORE );
        swp.cx = WinQueryWindowUShort( frameWin, QWS_CXRESTORE );
        swp.cy = WinQueryWindowUShort( frameWin, QWS_CYRESTORE );
      }
      PrfWriteProfileData( iniFile, "Session Manager", "Window position data",
       &swp, sizeof( SWP ) );
      
      bigDataLen = 0;
      for ( i=0; i<*numItems; ++i )
      {
        if ( (*info)[i].userName )
        {
          dataLen = 6 + strlen( (*info)[i].userName ) +
           strlen( (*info)[i].password ) + strlen( (*info)[i].server ) + 2;
          if ( dataLen > bigDataLen ) bigDataLen = dataLen;
        }
      }
      
      CHECKED_MALLOC( bigDataLen, prfData );
      numRealSessions = 0;
      
      record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD,
       MPFROMSHORT( CMA_FIRST ), MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
      while ( record && (LONG) record != -1 )
      {
        for ( i=0; i<(*numItems); ++i )
        {
          if ( (*info)[i].record == record )
          {
            if ( (*info)[i].userName )
            {
              dataLen = 0;
              txtLen = strlen( (*info)[i].userName );
              *((USHORT *)(prfData + dataLen)) = (USHORT) txtLen;
              dataLen += 2;
              strncpy( (char *)(prfData + dataLen), (*info)[i].userName,
               txtLen );
              dataLen += txtLen;
              txtLen = strlen( (*info)[i].password );
              *((USHORT *)(prfData + dataLen)) = (USHORT) txtLen;
              dataLen += 2;
              memcpy( (char *)(prfData + dataLen),
               (char *)aim_encode_password( (*info)[i].password ), txtLen );
              dataLen += txtLen;
              txtLen = strlen( (*info)[i].server );
              *((USHORT *)(prfData + dataLen)) = (USHORT) txtLen;
              dataLen += 2;
              strncpy( (char *)(prfData + dataLen),
               (*info)[i].server, txtLen );
              dataLen += txtLen;
              *((USHORT *)(prfData + dataLen)) = (USHORT) (*info)[i].port;
              dataLen += 2;
              numRealSessions++;
              debugf( "Saving record: %s.\n", (*info)[i].userName );
              sprintf( keyName, "Session %d data", numRealSessions );
              PrfWriteProfileData( iniFile, "Session Manager", keyName,
               prfData, dataLen );
            }
            break;
          }
        }
        record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD,
         MPFROMP( record ), MPFROM2SHORT( CMA_NEXT, CMA_ITEMORDER ) );
      }
      
      CHECKED_FREE( prfData );

      PrfWriteProfileData( iniFile, "Session Manager", "Number of Sessions",
       &numRealSessions, 4 );
      
      cursoredItem = 0;
      i = 0;
      
      record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD, 
       NULL, MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
      
      while ( record && (LONG)record != -1 )
      {
        ++i;
        if ( record->flRecordAttr & CRA_CURSORED )
        {
          cursoredItem = i;
          // 1-based
        }
        
        record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD, 
         MPFROMP( record ), MPFROM2SHORT( CMA_NEXT, CMA_ITEMORDER ) );
      }
      
      debugf( "Deleting all container objects.\n" );
      do {
        record = (MINIRECORDCORE *) WinSendMsg( theWin, CM_QUERYRECORD, NULL,
         MPFROM2SHORT( CMA_LAST, CMA_ITEMORDER ) );
        if ( !record || (LONG)record == -1 ) break;
        
        WinSendMsg( theWin, CM_REMOVERECORD, MPFROMP( &record ),
         MPFROM2SHORT( 1, CMA_FREE ) );
      } while ( 1 );
      // Need a separate loop here because if we remove the cursored item,
      //  then the next item *becomes* cursored.  So do the remove in a
      //  separate step.
      
      PrfWriteProfileData( iniFile, "Session Manager", "Cursored Record",
       &cursoredItem, 4 );
      
      WinDestroyWindow( theWin );
      WinDestroyWindow( WinWindowFromID( win, MRM_StartSession ) );
      WinDestroyWindow( WinWindowFromID( win, MRM_CreateSession ) );
      WinDestroyWindow( WinWindowFromID( win, MRM_EditSession ) );
      WinDestroyWindow( WinWindowFromID( win, MRM_DeleteSession ) );
      
      menuWin = WinQueryWindowULong( win, 24 );
      WinDestroyWindow( menuWin );
      menuWin = WinQueryWindowULong( win, 28 );
      WinDestroyWindow( menuWin );
      
      icon = WinQueryWindowULong( win, 16 );
      if ( icon )
      {
        WinDestroyPointer( icon );
      }
      
      debugf( "Session manager window is closing.\n" );
    }
    break;
    case WM_MENUEND:
    {
      MINIRECORDCORE *record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
       MRM_CN_SessionContainer, CM_QUERYRECORDEMPHASIS, MPFROMP( CMA_FIRST ),
       MPFROMSHORT( CRA_SOURCE ) );
      RECTL rectl;
      POINTL pts[2];
      
      // Remove the source emphasis if any records have it and invalidate
      //  the appropriate area of the container to make sure that this
      //  emphasis is correctly reflected.
      
      if ( WinQueryWindowULong( win, 24 ) != LONGFROMMP( mp2 ) &&
            WinQueryWindowULong( win, 28 ) != LONGFROMMP( mp2 ) )
        break;
      // Only want to operate on popup windows
      
      WinShowWindow( LONGFROMMP( mp2 ), FALSE );
      
      if ( record && (LONG)record != -1 )
      {
        WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
         CM_SETRECORDEMPHASIS, MPFROMP( record ),
         MPFROM2SHORT( FALSE, CRA_SOURCE ) );
      } else {
        WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
         CM_SETRECORDEMPHASIS, MPFROMP( NULL ),
         MPFROM2SHORT( FALSE, CRA_SOURCE ) );
      }
      
      WinQueryWindowRect( LONGFROMMP( mp2 ), &rectl );
      pts[0].x = rectl.xLeft;
      pts[0].y = rectl.yBottom;
      pts[1].x = rectl.xRight;
      pts[1].y = rectl.yTop;
      WinMapWindowPoints( LONGFROMMP( mp2 ), WinWindowFromID( win,
       MRM_CN_SessionContainer ), pts, 2 );
      rectl.xLeft = pts[0].x;
      rectl.yBottom = pts[0].y;
      rectl.xRight = pts[1].x;
      rectl.yTop = pts[1].y;
      WinInvalidateRect( WinWindowFromID( win, MRM_CN_SessionContainer ),
       &rectl, TRUE );
    }
    break;
    case WM_CONTROL:
    {
      if ( SHORT1FROMMP( mp1 ) == MRM_CN_SessionContainer )
      {
        switch ( SHORT2FROMMP( mp1 ) )
        {
          case CN_CONTEXTMENU:
          {
            MINIRECORDCORE *record = (MINIRECORDCORE *) PVOIDFROMMP( mp2 );
            HWND menuWin;
            POINTL pt;
            ULONG numSelected = WinQueryWindowULong( win, 0 );
            
            if ( !record )
            {
              // Pop-up menu for background window
              
              WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
               QWL_USER, NULL );
              // QWL_USER is for my use and I'll use it to record the object
              //  on which the popup menu was invoked.
              
              menuWin = WinQueryWindowULong( win, 28 );
            } else {
              // Pop-up on an object
              
              WinSetWindowPtr( WinWindowFromID( win, MRM_CN_SessionContainer ),
               QWL_USER, record );
              // QWL_USER is for my use and I'll use it to record the object
              //  on which the popup menu was invoked.
              
              menuWin = WinQueryWindowULong( win, 24 );
              if ( numSelected > 1 && (record->flRecordAttr & CRA_SELECTED) )
              {
                // Attempted to select more than one.  Disable edit.
                WinEnableMenuItem( menuWin, MRM_EditSession, FALSE );
              } else {
                WinEnableMenuItem( menuWin, MRM_EditSession, TRUE );
              }
            }
            
            WinQueryPointerPos( HWND_DESKTOP, &pt );
            
            WinSendMsg( WinWindowFromID( win, MRM_CN_SessionContainer ),
             CM_SETRECORDEMPHASIS, MPFROMP( record ),
             MPFROM2SHORT( TRUE, CRA_SOURCE ) );
            
            WinPopupMenu( HWND_DESKTOP, win, menuWin, pt.x, pt.y, 0,
             PU_KEYBOARD | PU_MOUSEBUTTON1 | PU_MOUSEBUTTON2 );
          }
          break;
          case CN_REALLOCPSZ:
          {
            CNREDITDATA *editData = (CNREDITDATA *) PVOIDFROMMP( mp2 );
            MINIRECORDCORE *record = (MINIRECORDCORE *) editData->pRecord;
            ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
            sessionThreadInfo **info =
             (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
            sessionThreadSettings *mySettings = NULL;
            sessionThreadSettings *globalSettings = (sessionThreadSettings *)
             WinQueryWindowPtr( win, 60 );
            ULONG i;
            
            for ( i=0; i<(*numItems); ++i )
            {
              if ( (*info)[i].userName && (*info)[i].record == record )
              {
                mySettings = (*info)[i].settings;
              }
            }
            
            if ( record->flRecordAttr & CRA_INUSE )
            {
              // Disallow editting the title if it's in use
              char *titleString;
              eventData theEventData;
              
              CHECKED_MALLOC( 24 + strlen( *(editData->ppszText) ), titleString );
              sprintf( titleString, "Cannot modify session: %s",
               *(editData->ppszText) );
              theEventData.currentUser = *(editData->ppszText);
              theEventData.otherUser = NULL;
              theEventData.message = "CANNOT MODIFY SESSION";
              handleMrMessageEvent( EVENT_ERRORBOX, mySettings,
               globalSettings, &theEventData, 0 );
              WinMessageBox( HWND_DESKTOP, win,
               "Session is currently opened and running.  You cannot modify it.  Please close the session and then proceed with your edits.",
               titleString, 503,
               MB_CANCEL | MB_APPLMODAL | MB_MOVEABLE );
              CHECKED_FREE( titleString );
              return MRFROMLONG( FALSE );
            }
            CHECKED_MALLOC( editData->cbText, *(editData->ppszText) );
            return MRFROMLONG( TRUE );
          }
          case CN_ENDEDIT:
          {
            CNREDITDATA *editData = (CNREDITDATA *) PVOIDFROMMP( mp2 );
            MINIRECORDCORE *record = (MINIRECORDCORE *) editData->pRecord;
            ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
            sessionThreadInfo **info =
             (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
            HINI iniFile = WinQueryWindowULong( win, 20 );
            char *tmpString, *newString;
            ULONG i;
            
            if ( !editData->pRecord ) break;
            
            for ( i=0; i<(*numItems); ++i )
            {
              if ( (*info)[i].userName && (*info)[i].record == record )
              {
                tmpString = (*info)[i].userName; 
                newString = *(editData->ppszText);
                (*info)[i].userName = newString;
                renameSessionData( iniFile, tmpString, newString );
                CHECKED_FREE( tmpString );
                break;
              }
            }
          }          
          case CN_EMPHASIS:
          {
            NOTIFYRECORDEMPHASIS *emphasis;
            emphasis = (NOTIFYRECORDEMPHASIS *) PVOIDFROMMP( mp2 );
            if ( emphasis->fEmphasisMask & CRA_SELECTED )
            {
              // Something was selected or de-selected
              MINIRECORDCORE *record = (MINIRECORDCORE *) emphasis->pRecord;
              ULONG selectedCount = WinQueryWindowULong( win, 0 );
              HWND theWin;
              
              if ( record->flRecordAttr & CRA_SELECTED )
              {
                if ( !selectedCount )
                {
                  // Enable options that require a selection
                  WinEnableWindow( WinWindowFromID( win, MRM_StartSession ),
                   TRUE );
                  WinEnableWindow( WinWindowFromID( win, MRM_EditSession ),
                   TRUE );
                  WinEnableWindow( WinWindowFromID( win, MRM_DeleteSession ),
                   TRUE );
                  theWin = WinQueryWindow( win, QW_PARENT );
                  theWin = WinWindowFromID( theWin, FID_MENU );
                  WinEnableMenuItem( theWin, MRM_StartSession, TRUE );
                  WinEnableMenuItem( theWin, MRM_EditSession, TRUE );
                  WinEnableMenuItem( theWin, MRM_DeleteSession, TRUE );
                  WinEnableMenuItem( theWin, MRM_SessionSettings, TRUE );
                } else {
                  theWin = WinQueryWindow( win, QW_PARENT );
                  theWin = WinWindowFromID( theWin, FID_MENU );
                  WinEnableWindow( WinWindowFromID( win, MRM_EditSession ),
                   FALSE );
                  WinEnableMenuItem( theWin, MRM_EditSession, FALSE );
                  // Can only edit one entry
                }
                selectedCount++;
              } else {
                selectedCount--;
                if ( !selectedCount )
                {
                  // Disable options that require a selection
                  WinEnableWindow( WinWindowFromID( win, MRM_StartSession ),
                   FALSE );
                  WinEnableWindow( WinWindowFromID( win, MRM_EditSession ),
                   FALSE );
                  WinEnableWindow( WinWindowFromID( win, MRM_DeleteSession ),
                   FALSE );
                  theWin = WinQueryWindow( win, QW_PARENT );
                  theWin = WinWindowFromID( theWin, FID_MENU );
                  WinEnableMenuItem( theWin, MRM_StartSession, FALSE );
                  WinEnableMenuItem( theWin, MRM_EditSession, FALSE );
                  WinEnableMenuItem( theWin, MRM_DeleteSession, FALSE );
                  WinEnableMenuItem( theWin, MRM_SessionSettings, FALSE );
                } else if ( selectedCount == 1 )
                {
                  WinEnableWindow( WinWindowFromID( win, MRM_EditSession ),
                   TRUE );
                  theWin = WinQueryWindow( win, QW_PARENT );
                  theWin = WinWindowFromID( theWin, FID_MENU );
                  WinEnableMenuItem( theWin, MRM_EditSession, TRUE );
                  // Can only edit one entry
                }
              }
              WinSetWindowULong( win, 0, selectedCount );
            }
          }
          break;
          case CN_ENTER:
            if ( WinQueryWindowULong( win, 0 ) )
            {
              // If something is selected, pretend we hit the Start button
              WinPostMsg( win, WM_COMMAND, MPFROMSHORT( MRM_StartSession ),
               NULL );
            }
          break;
          case CN_INITDRAG:
          {
            DRAGINFO *dragInfo;
            DRAGIMAGE dragImage;
            DRAGITEM dragItem;
            CNRDRAGINIT *dragInitInfo = (CNRDRAGINIT *) PVOIDFROMMP( mp2 );
            
            if ( !dragInitInfo->pRecord ) break;
            debugf( "Dragging entry %s.\n", dragInitInfo->pRecord->pszIcon );
             
            dragItem.hwndItem = win;
            dragItem.hstrType = DrgAddStrHandle( DRT_UNKNOWN );
            dragItem.hstrRMF = DrgAddStrHandle( "<DRM_DDE,DRF_TEXT>" );
            dragItem.hstrContainerName =
             DrgAddStrHandle( "Mr. Message Session Manager" );
            dragItem.cxOffset = 0;
            dragItem.cyOffset = 0;
            dragItem.fsControl = 0;
            dragItem.fsSupportedOps = DO_MOVEABLE | DO_COPYABLE;
            dragImage.cb = sizeof(DRAGIMAGE);
            dragImage.cptl = 0;
            dragImage.hImage = WinQueryWindowULong( win, 16 );
            dragImage.sizlStretch.cx = 0;
            dragImage.sizlStretch.cy = 0;
            dragImage.fl = DRG_ICON;
            dragImage.cxOffset = 0;
            dragImage.cyOffset = 0;
            dragItem.ulItemID = (ULONG) dragInitInfo->pRecord;
            dragItem.hstrSourceName =
             DrgAddStrHandle( dragInitInfo->pRecord->pszIcon );
            dragItem.hstrTargetName =
             DrgAddStrHandle( dragInitInfo->pRecord->pszIcon );
            
            dragInfo = DrgAllocDraginfo( 1 );
            dragInfo->usOperation = DO_DEFAULT;
            
            DrgSetDragitem( dragInfo, &dragItem, sizeof( DRAGITEM ), 0 );
            
            debugf( "Initiating direct manipulation (drag-n-drop).\n" );
            DrgDrag( win, dragInfo, &dragImage, 1, VK_ENDDRAG, NULL );
            
            DrgDeleteDraginfoStrHandles( dragInfo );
            DrgFreeDraginfo( dragInfo );
            debugf( "Freed direct manipulation resources.\n" );
          }
          break;
          case CN_DRAGOVER:
          {
            CNRDRAGINFO *cnrDragInfo = (CNRDRAGINFO *) PVOIDFROMMP( mp2 );
            DRAGINFO *dragInfo = cnrDragInfo->pDragInfo;
            DRAGITEM *dragItem;
            USHORT defaultAction = DO_MOVE;
            ULONG containerNameLen;
            const char *acceptContainerString = "Mr. Message Session Manager";
            char buffer[28];
            
            if ( !DrgAccessDraginfo( dragInfo ) )
            {
              return MRFROM2SHORT( DOR_NEVERDROP, defaultAction );
            }
            
            dragItem = DrgQueryDragitemPtr( dragInfo, 0 );
            containerNameLen = DrgQueryStrName(
             dragItem->hstrContainerName, 28, buffer );
            
            if ( containerNameLen < strlen( acceptContainerString ) )
            {
              return MRFROM2SHORT( DOR_NODROPOP, 0 );
            } else {
              if ( strcmp( buffer, acceptContainerString ) != 0 )
              {
                return MRFROM2SHORT( DOR_NODROPOP, 0 );
              }
            }
            
            if ( dragInfo->hwndSource != win )
            {
              defaultAction = DO_COPY;
            }
            
            if ( cnrDragInfo->pRecord == NULL )
            {
              DrgFreeDraginfo( dragInfo );
              return MRFROM2SHORT( DOR_DROP, defaultAction );
            }
            
            DrgFreeDraginfo( dragInfo );
            return MRFROM2SHORT( DOR_NODROP, defaultAction );
          }
          case CN_DROP:
          {
            CNRDRAGINFO *cnrDragInfo = (CNRDRAGINFO *) PVOIDFROMMP( mp2 );
            DRAGINFO *dragInfo = cnrDragInfo->pDragInfo;
            DRAGITEM *dragItem;
            MINIRECORDCORE *theRecord, *record;
            RECORDINSERT insert;
            
            if ( !DrgAccessDraginfo( dragInfo ) )
            {
              return NULL;
            }
            
            if ( cnrDragInfo->pRecord != NULL )
            {
              // Can't drop on an existing entry.  No meaning to that here.
              return NULL;
            }
            
            if ( dragInfo->usOperation == DO_COPY )
            {
              // Wait for a subsequent sent message to create the copy
              char *sharedMem;
              HPOINTER icon;
              char *text;
              ULONG len, len2, myEntry;
              HMTX infoMux = WinQueryWindowULong( win, 12 );
              ULONG *numItems = (ULONG *) WinQueryWindowPtr( win, 4 );
              sessionThreadInfo **info =
               (sessionThreadInfo **) WinQueryWindowPtr( win, 8 );
              sessionThreadSettings *globalSettings = (sessionThreadSettings *)
               WinQueryWindowPtr( win, 60 );
              
              dragItem = DrgQueryDragitemPtr( dragInfo, 0 );
              theRecord = (MINIRECORDCORE *) dragItem->ulItemID;
              
              DosAllocSharedMem( (PVOID *)&sharedMem, NULL, 4096, OBJ_GETTABLE |
               PAG_COMMIT | PAG_READ | PAG_WRITE );
              
              debugf( "Drag source was a different window.  Copying instead of moving.\n" );
              DrgSendTransferMsg( dragInfo->hwndSource, WM_GIMMEDROPCOPYDATA,
               MPFROMP( sharedMem ), MPFROMP( theRecord ) );
              
              record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
               MRM_CN_SessionContainer, CM_ALLOCRECORD, MPFROMLONG( 0 ),
               MPFROMSHORT( 1 ) );
              if ( !record )
              {
                debugf( "Error allocating record in container.\n" );
                DosFreeMem( sharedMem );
                return 0;
              }
              
              icon = WinQueryWindowULong( win, 16 );
              
              len = strlen( sharedMem );
              if ( !len )
              {
                DosFreeMem( sharedMem );
                break;
              }
              
              CHECKED_MALLOC( len + 1, text );
              strcpy( text, sharedMem );
              
              debugf( "Entry %s is being copied.\n", text );
              
              record->flRecordAttr = CRA_CURSORED;
              record->ptlIcon.x = 0;
              record->ptlIcon.y = 0;
              record->pszIcon = text;
              record->hptrIcon = icon;
              insert.cb = sizeof( RECORDINSERT );
              insert.pRecordOrder = (RECORDCORE *)CMA_END;
              insert.pRecordParent = NULL;
              insert.fInvalidateRecord = TRUE;
              insert.zOrder = CMA_TOP;
              insert.cRecordsInsert = 1;
              
              WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_INSERTRECORD,
               MPFROMP( record ), MPFROMP( &insert ) );
              
              DosRequestMutexSem( infoMux, SEM_INDEFINITE_WAIT );
              if ( !(*numItems) )
              {
                CHECKED_MALLOC( sizeof( sessionThreadInfo ), *info );
                myEntry = 0;
                (*numItems)++;
              } else {
                ULONG i;
                myEntry = *numItems;
                for ( i=0; i<*numItems; ++i )
                {
                  if ( !(*info)[i].userName ) myEntry = i;
                }
                if ( *numItems == myEntry )
                {
                  CHECKED_REALLOC( *info, ((*numItems) + 1) *
                   sizeof( sessionThreadInfo ), *info );
                  (*numItems)++;
                }
              }
              
              (*info)[ myEntry ].userName = text;
              DosReleaseMutexSem( infoMux );
            
              len2 = strlen( sharedMem + len + 1 );
              CHECKED_MALLOC( len2 + 1, text );
              strcpy( text, sharedMem + len + 1 );
              (*info)[ myEntry ].password = text;
              
              len += len2;
              
              len2 = strlen( sharedMem + len + 1 );
              CHECKED_MALLOC( len2 + 1, text );
              strcpy( text, sharedMem + len + 1 );
              (*info)[ myEntry ].server = text;
              
              len += len2;
              
              (*info)[ myEntry ].port = *((USHORT *)( sharedMem + len + 1 ));
              
              DosFreeMem( sharedMem );
              
              (*info)[ myEntry ].managerWin = win;
              (*info)[ myEntry ].theSession = NULL;
              (*info)[ myEntry ].record = record;
              (*info)[ myEntry ].threadID = 0;
              (*info)[ myEntry ].entryFromIniFile = 0;
              
              CHECKED_MALLOC( sizeof( sessionThreadSettings ),
               (*info)[ myEntry ].settings );
              memcpy( (*info)[ myEntry ].settings, globalSettings,
               sizeof( sessionThreadSettings ) );
              (*info)[ myEntry ].settings->inheritSettings = 1;
            } else if ( dragInfo->usOperation == DO_MOVE )
            {
              // Move means that it originated in this very window and process.
              //  So our dragItem should give us the MINIRECORDCORE pointer
              //  which we can reposition as requested by the user.
              RECTL itemRect, theRect;
              QUERYRECORDRECT qrr;
              LONG tmpL;
              POINTL dropPt;
              MINIRECORDCORE *prevRecord = (MINIRECORDCORE *)CMA_FIRST;
              
              dragItem = DrgQueryDragitemPtr( dragInfo, 0 );
              theRecord = (MINIRECORDCORE *) dragItem->ulItemID;
              dropPt.x = dragInfo->xDrop;
              dropPt.y = dragInfo->yDrop;
              WinMapWindowPoints( HWND_DESKTOP,
               WinWindowFromID( win, MRM_CN_SessionContainer ), &dropPt, 1 );
              WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
               CM_QUERYVIEWPORTRECT, MPFROMP( &theRect ),
               MPFROM2SHORT( CMA_WORKSPACE, FALSE ) );
              dropPt.x += theRect.xLeft;
              dropPt.y += theRect.yBottom;
              
              debugf( "Dropped at %d, %d (container workspace coordinates).\n",
               dropPt.x, dropPt.y );
              
              record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
               MRM_CN_SessionContainer, CM_QUERYRECORD, MPFROMSHORT( CMA_FIRST ),
               MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
              qrr.cb = sizeof( QUERYRECORDRECT );
              qrr.fRightSplitWindow = FALSE;
              qrr.fsExtent = CMA_ICON | CMA_TEXT;
              while ( record && (LONG) record != -1 )
              {
                qrr.pRecord = (RECORDCORE *)record;
                WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
                 CM_QUERYRECORDRECT, MPFROMP( &itemRect ), MPFROMP( &qrr ) );
                 
                if ( dropPt.y > itemRect.yTop ) break;
                // Insert the record before this one here
                
                if ( dropPt.y <= itemRect.yTop &&
                      dropPt.y >= itemRect.yBottom )
                {
                  tmpL = itemRect.xRight - itemRect.xLeft;
                  if ( dropPt.x < itemRect.xLeft + (tmpL / 2) ) break;
                  // Insert the record before this one here
                }
                
                prevRecord = record;
                record = (MINIRECORDCORE *) WinSendDlgItemMsg( win,
                 MRM_CN_SessionContainer, CM_QUERYRECORD, MPFROMP( record ),
                 MPFROM2SHORT( CMA_NEXT, CMA_ITEMORDER ) );
              }
              
              if ( record && (LONG) record != -1 )
              {
                if ( prevRecord == theRecord ) break;
                
                WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_REMOVERECORD,
                 MPFROMP( &theRecord ), MPFROM2SHORT( 1, 0 ) );
                 
                insert.cb = sizeof( RECORDINSERT );
                insert.pRecordOrder = (RECORDCORE *) prevRecord;
                insert.pRecordParent = NULL;
                insert.fInvalidateRecord = TRUE;
                insert.zOrder = CMA_TOP;
                insert.cRecordsInsert = 1;
                WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_INSERTRECORD,
                 MPFROMP( theRecord ), MPFROMP( &insert ) );
              } else {
                // Move to end
                
                WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_REMOVERECORD,
                 MPFROMP( &theRecord ), MPFROM2SHORT( 1, 0 ) );
                 
                insert.cb = sizeof( RECORDINSERT );
                insert.pRecordOrder = (RECORDCORE *) CMA_END;
                insert.pRecordParent = NULL;
                insert.fInvalidateRecord = TRUE;
                insert.zOrder = CMA_TOP;
                insert.cRecordsInsert = 1;
                WinSendDlgItemMsg( win, MRM_CN_SessionContainer, CM_INSERTRECORD,
                 MPFROMP( theRecord ), MPFROMP( &insert ) );
              }
              
              WinSendDlgItemMsg( win, MRM_CN_SessionContainer,
               CM_INVALIDATERECORD, MPFROMP( &theRecord ),
               MPFROM2SHORT( 1, CMA_REPOSITION ) );
              DrgFreeDraginfo( dragInfo );
            }
          }
          break;
        }
      }
    }
    break;
  }
  return WinDefWindowProc( win, msg, mp1, mp2 );
}

void renameSessionData( HINI iniFile, char *oldName, char *newName )
{
  unsigned char *tmpString;
  unsigned long dataLen;
  char *strPtr, *strPtr2;
  
  extern int screenNamesEqual( const char *sn1, const char *sn2 );
  // from oscarsession.cpp
  
  if ( strcmp( oldName, newName ) )
  {
    debugf( "Renaming profile information headings for %s.\n", newName );
  }
  
  PrfQueryProfileSize( iniFile, NULL, NULL, &dataLen );
  
  if ( !dataLen )
  {
    return;
  }
  
  CHECKED_MALLOC( dataLen, tmpString );
  PrfQueryProfileData( iniFile, NULL, NULL, tmpString, &dataLen );
  
  strPtr = (char *)tmpString;
  
  if ( !strPtr )
  {
    CHECKED_FREE( tmpString );
    return;
  }
  
  while ( *strPtr )
  {
    if ( strncmp( "Buddy List [", strPtr, 12 ) == 0 )
    {
      strPtr2 = strrchr( strPtr + 12, ']' );
      if ( !strPtr2 )
      {
        PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
        debugf( "Corrupted Buddy List entry was removed from profile: %s\n",
         strPtr );
        strPtr += strlen( strPtr ) + 1;
        continue;
      }
      *strPtr2 = ' ';
      if ( screenNamesEqual( strPtr + 12, oldName ) )
      {
        if ( strncmp( newName, strPtr + 12, strlen( newName ) ) )
        {
          char *buddyListSettings, *newAppName;
          unsigned char *keyData;
          unsigned long settingsLen, dataLen;
          
          debugf( "Updating Buddy List profile information for %s.\n", newName );
          
          CHECKED_MALLOC( 14 + strlen( newName ), newAppName );
          sprintf( newAppName, "Buddy List [%s]", newName );
          
          *strPtr2 = ']';
          if ( !PrfQueryProfileSize( iniFile, strPtr, NULL, &settingsLen ) )
          {
            PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
            strPtr += strlen( strPtr ) + 1;
            CHECKED_FREE( newAppName );
            continue;
          }
          CHECKED_MALLOC( settingsLen, buddyListSettings );
          if ( !PrfQueryProfileData( iniFile, strPtr, NULL,
                buddyListSettings, &settingsLen ) )
          {
            PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
            strPtr += strlen( strPtr ) + 1;
            CHECKED_FREE( buddyListSettings );
            CHECKED_FREE( newAppName );
            continue;
          }
          
          strPtr2 = buddyListSettings;
          while ( *strPtr2 )
          {
            if ( !PrfQueryProfileSize( iniFile, strPtr, strPtr2, &dataLen ) )
            {
              strPtr2 += strlen( strPtr2 ) + 1;
              continue;
            }
            CHECKED_MALLOC( dataLen, keyData );
            PrfQueryProfileData( iniFile, strPtr, strPtr2, keyData,
             &dataLen );
            PrfWriteProfileData( iniFile, newAppName, strPtr2, keyData,
             dataLen );
            CHECKED_FREE( keyData );
            strPtr2 += strlen( strPtr2 ) + 1;
          }
          
          PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
          // Clear out the old entry
          
          CHECKED_FREE( buddyListSettings );
          CHECKED_FREE( newAppName );
        }
      } else {
        *strPtr2 = ']';
      }
    } else if ( strlen( strPtr ) > 8 &&
                 strcmp( strPtr + strlen( strPtr ) - 8, " Aliases" ) == 0 )
    {
      strPtr2 = strrchr( strPtr, ' ' );
      *strPtr2 = 0;
      if ( screenNamesEqual( strPtr, oldName ) && strcmp( strPtr, newName ) )
      {
        char *aliasSettings, *newAppName;
        unsigned char *keyData;
        unsigned long settingsLen, dataLen;
        
        debugf( "Updating alias profile information for %s.\n", newName );
        
        CHECKED_MALLOC( 9 + strlen( newName ), newAppName );
        sprintf( newAppName, "%s Aliases", newName );
        
        *strPtr2 = ' ';
        if ( !PrfQueryProfileSize( iniFile, strPtr, NULL, &settingsLen ) )
        {
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        CHECKED_MALLOC( settingsLen, aliasSettings );
        if ( !PrfQueryProfileData( iniFile, strPtr, NULL, aliasSettings,
              &settingsLen ) )
        {
          CHECKED_FREE( aliasSettings );
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        strPtr2 = aliasSettings;
        
        while ( *strPtr2 )
        {
          if ( !PrfQueryProfileSize( iniFile, strPtr, strPtr2, &dataLen ) )
          {
            strPtr2 += strlen( strPtr2 ) + 1;
            continue;
          }
          CHECKED_MALLOC( dataLen, keyData );
          PrfQueryProfileData( iniFile, strPtr, strPtr2, keyData,
           &dataLen );
          PrfWriteProfileData( iniFile, newAppName, strPtr2, keyData,
           dataLen );
          CHECKED_FREE( keyData );
          strPtr2 += strlen( strPtr2 ) + 1;
        }
        
        PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
        CHECKED_FREE( aliasSettings );
        CHECKED_FREE( newAppName );
      } else {
        *strPtr2 = ' ';
      }
    } else if ( strlen( strPtr ) > 30 &&
                 strcmp( strPtr + strlen( strPtr ) - 24,
                  "] Instant Message Window" ) == 0 )
    {
      int j;
      int isJunk = 0;
      strPtr2 = strstr( strPtr, " <-> " );
      
      if ( !strPtr2 || *strPtr != '[' ) isJunk = 1;
      if ( !isJunk )
      {
        for ( j=0; j < strlen(strPtr); ++j )
        {
          if ( !isprint( strPtr[j] ) )
          {
            // Somehow, I've seen an INI file with control characters
            //  in these entries.  I'll clean them out.
            isJunk = 1;
            break;
          }
        }
      }
      
      if ( isJunk )
      {
        debugf( "Garbage instant message window settings entry found and deleted.\n" );
        PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
        strPtr += strlen( strPtr ) + 1;
        continue;
      }
      *strPtr2 = 0;
      if ( screenNamesEqual( strPtr + 1, oldName ) &&
            strcmp( strPtr + 1, newName ) )
      {
        char *windowSettings, *newAppName;
        unsigned char *keyData;
        unsigned long settingsLen, dataLen;
        
        *strPtr2 = ' ';
        debugf( "Updating [%s%s Settings.\n", newName, strPtr2 );
        CHECKED_MALLOC( 2 + strlen( newName ) + strlen( strPtr2 ),
         newAppName );
        sprintf( newAppName, "[%s%s", newName, strPtr2 );
        
        if ( !PrfQueryProfileSize( iniFile, strPtr, NULL, &settingsLen ) )
        {
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        CHECKED_MALLOC( settingsLen, windowSettings );
        if ( !PrfQueryProfileData( iniFile, strPtr, NULL, windowSettings,
              &settingsLen ) )
        {
          CHECKED_FREE( windowSettings );
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        strPtr2 = windowSettings;
        
        while ( *strPtr2 )
        {
          if ( !PrfQueryProfileSize( iniFile, strPtr, strPtr2, &dataLen ) )
          {
            strPtr2 += strlen( strPtr2 ) + 1;
            continue;
          }
          CHECKED_MALLOC( dataLen, keyData );
          PrfQueryProfileData( iniFile, strPtr, strPtr2, keyData,
           &dataLen );
          PrfWriteProfileData( iniFile, newAppName, strPtr2, keyData,
           dataLen );
          CHECKED_FREE( keyData );
          strPtr2 += strlen( strPtr2 ) + 1;
        }
        
        PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
        CHECKED_FREE( windowSettings );
        CHECKED_FREE( newAppName );
      } else {
        *strPtr2 = ' ';
      }
    } else if ( strlen( strPtr ) > 9 &&
                 strcmp( strPtr + strlen( strPtr ) - 9,
                  " Settings" ) == 0 )
    {
      strPtr2 = strrchr( strPtr, ' ' );
      *strPtr2 = 0;
      if ( screenNamesEqual( strPtr, oldName ) && strcmp( strPtr, newName ) )
      {
        char *sessionSettings, *newAppName;
        unsigned char *keyData;
        unsigned long settingsLen, dataLen;
        
        debugf( "Updating session profile information for %s.\n", newName );
        
        CHECKED_MALLOC( 10 + strlen( newName ), newAppName );
        sprintf( newAppName, "%s Settings", newName );
        
        *strPtr2 = ' ';
        if ( !PrfQueryProfileSize( iniFile, strPtr, NULL, &settingsLen ) )
        {
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        CHECKED_MALLOC( settingsLen, sessionSettings );
        if ( !PrfQueryProfileData( iniFile, strPtr, NULL, sessionSettings,
              &settingsLen ) )
        {
          CHECKED_FREE( sessionSettings );
          CHECKED_FREE( newAppName );
          strPtr += strlen( strPtr ) + 1;
          continue;
        }
        
        strPtr2 = sessionSettings;
        
        while ( *strPtr2 )
        {
          if ( !PrfQueryProfileSize( iniFile, strPtr, strPtr2, &dataLen ) )
          {
            strPtr2 += strlen( strPtr2 ) + 1;
            continue;
          }
          CHECKED_MALLOC( dataLen, keyData );
          PrfQueryProfileData( iniFile, strPtr, strPtr2, keyData,
           &dataLen );
          PrfWriteProfileData( iniFile, newAppName, strPtr2, keyData,
           dataLen );
          CHECKED_FREE( keyData );
          strPtr2 += strlen( strPtr2 ) + 1;
        }
        
        PrfWriteProfileData( iniFile, strPtr, NULL, NULL, 0 );
        CHECKED_FREE( sessionSettings );
        CHECKED_FREE( newAppName );
      } else {
        *strPtr2 = ' ';
      }
    }
    strPtr += strlen( strPtr ) + 1;
  }
  
  CHECKED_FREE( tmpString );
}

void fixProfiles( HINI iniFile )
{
  unsigned short patchLevel;
  unsigned long len;
  unsigned char *tmpString;
  int updated = 0;
  
  patchLevel = 0;
  
  PrfQueryProfileData( iniFile, "Profile Data", "Patch Level",
   &patchLevel, &len );
  
  debugf( "Profile data is at patch level %d.\n", patchLevel );
  
  switch ( patchLevel )
  {
    case 0:
    {
      unsigned short nameLen;
      int numProfs, i;
      char *screenName;
      char buffer[1024];
      
      extern int screenNamesEqual( const char *sn1, const char *sn2 );
      // from oscarsession.cpp
      
      debugf( "Updating profile data to latest patch level.\n" );
      
      len = sizeof( int );
      
      if ( !PrfQueryProfileData( iniFile, "Session Manager",
            "Number of Sessions", &numProfs, &len ) )
        break;
      
      for ( i=1; i<=numProfs; ++i )
      {
        sprintf( buffer, "Session %d data", i );
        if ( !PrfQueryProfileSize( iniFile, "Session Manager", buffer,
              &len ) )
          continue;
        
        CHECKED_MALLOC( len, tmpString );
        
        if ( !PrfQueryProfileData( iniFile, "Session Manager", buffer,
              tmpString, &len ) )
        {
          CHECKED_FREE( tmpString );
          continue;
        }
        
        nameLen = *((unsigned short *)tmpString);
        CHECKED_MALLOC( nameLen + 1, screenName );
        memcpy( screenName, tmpString + 2, nameLen );
        screenName[nameLen] = 0;
        CHECKED_FREE( tmpString );
        
        debugf( "Checking profile: %s.\n", screenName );
        renameSessionData( iniFile, screenName, screenName );
        CHECKED_FREE( screenName );
      }
    }
    
    patchLevel = 1;
    updated = 1;

    break;
  }
  
  if ( updated )
  {
    PrfWriteProfileData( iniFile, "Profile Data", "Patch Level",
     &patchLevel, sizeof( unsigned short ) );
    
    debugf( "Profile data has been updated to patch level %d.\n", patchLevel );
  }
}

SessionManager :: SessionManager()
{
  QMSG qmsg;
  ULONG frameflgs = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER | 
   FCF_MINMAX | FCF_TASKLIST | FCF_MENU;
  ULONG i;
  RECTL desktopSize;
  HMODULE dllModule = getModuleHandle();
  HWND mgrWin, clientWin;
  HAB ab;
  HMQ messq;
  SWP swp;
  sessionManagerULONGs initData;
  sessionThreadSettings globalSettings = { 0 };
  
  debugf( "Session manager started.\n" );
  registerSessionManager( this );
  
  infoMutex = NULLHANDLE;
  if ( DosCreateMutexSem( NULL, &infoMutex, 0, 0 ) )
  {
    debugf( "Could not create mutex semaphore for session manager.\n" );
    return;
  }
  
  debugf( "Session manager mutex semaphore was created.\n" );
  
  ab = WinInitialize( 0 );
  messq = WinCreateMsgQueue( ab, 0 );

  initData.iniFile = PrfOpenProfile( ab, "MRMESSAGE.INI" );
  if ( !initData.iniFile )
  {
    debugf( "Error opening profile!\n" );
    return;
  }
  
  if ( isMultiMediaCapable() )
  {
    ULONG tmpVolume, len;
    
    tmpVolume = 100; // default volume level
    len = sizeof( ULONG );
    PrfQueryProfileData( initData.iniFile, "Session Manager", "Audio Volume",
     &tmpVolume, &len );
    setAudioVolume( tmpVolume );
  }
  
  WinRegisterClass( ab, "MrMessage Splash Screen", pmSplashScreen,
   0, 4 );
  // 4 bytes extra storage for HBITMAP
  
  globalSettings.settingFlags[EVENT_SPLASHSCREEN] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "coffee.wav", globalSettings.sounds[EVENT_SPLASHSCREEN] );
  globalSettings.settingFlags[EVENT_STARTSESSION] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "vacusuck.wav", globalSettings.sounds[EVENT_STARTSESSION] );
  globalSettings.settingFlags[EVENT_ERRORBOX] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "lookinat.wav", globalSettings.sounds[EVENT_ERRORBOX] );
  globalSettings.settingFlags[EVENT_FIRSTCOMM] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "firsttime.wav", globalSettings.sounds[EVENT_FIRSTCOMM] );
  globalSettings.settingFlags[EVENT_RECEIVED] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "boing.wav", globalSettings.sounds[EVENT_RECEIVED] );
  globalSettings.settingFlags[EVENT_SENT] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "twip.wav", globalSettings.sounds[EVENT_SENT] );
  globalSettings.settingFlags[EVENT_ARRIVE] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "drwropen.wav", globalSettings.sounds[EVENT_ARRIVE] );
  globalSettings.settingFlags[EVENT_LEAVE] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "drwclose.wav", globalSettings.sounds[EVENT_LEAVE] );
  globalSettings.settingFlags[EVENT_ENDSESSION] = SET_SOUND_ENABLED;
  CHECKED_STRDUP( "stopped.wav", globalSettings.sounds[EVENT_ENDSESSION] );
  CHECKED_STRDUP( "This IM user is using Mr. Message for OS/2.",
   globalSettings.profile );
  CHECKED_STRDUP( "I'm away from my computer right now.",
   globalSettings.awayMessage );
  globalSettings.inheritSettings = 0;
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    globalSettings.rexxScripts[i] = NULL;
    globalSettings.shellCmds[i] = NULL;
  }
  globalSettings.sessionFlags = 0;
  
  querySessionSettings( initData.iniFile, "Global Settings", &globalSettings );
  
  fixProfiles( initData.iniFile );
  // Patch any bad data in INI files created by bugs in previous releases
  
  mgrWin = WinCreateWindow( HWND_DESKTOP, "MrMessage Splash Screen",
   "Mr. Message for OS/2", 0, 0, 0, 0, 0, HWND_DESKTOP, HWND_TOP,
   100, &globalSettings, NULL );
  
  while ( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
    WinDispatchMsg( hab, &qmsg );
  
  WinDestroyWindow( mgrWin );
  
  WinRegisterClass( ab, "MrMessage Session Manager", pmSessionManager,
   0, 68 );
  // 68 bytes extra storage
  // Offset 0  = number of selected sessions in GUI
  //        4  = pointer to number of sessions
  //        8  = pointer to session thread information
  //        12 = mutex semaphore handle for session thread information
  //        16 = AIM icon pointer handle
  //        20 = INI file handle
  //        24 = session manager popup menu HWND
  //        28 = session manager background popup menu HWND
  //        32 = System-defined window procedure for window ID 100
  //        36 = System-defined window procedure for window ID 101
  //     ...48 = System-defined window procedure for window ID 104
  //        52 = Allow distribution of button pres params (prevent recursion)
  //        56 = Allow edit session when session running
  //        60 = Pointer to global settings
  //        64 = Selected session override (for session settings)
  
  WinRegisterClass( ab, "MrMessage Buddy List", pmBuddyList, 0, 112 );
  // 112 bytes extra storage
  // Offset 0   = Pointer to my MINIRECORDCORE entry in the session manager
  // Offset 4   = Semaphore to wake up the session thread (when closing)
  // Offset 8   = Icon handle for away buddy icon
  // Offset 12  = Icon handle for folder for group icon
  // Offset 16  = Window handle of session manager
  // Offset 20  = Icon handle for online buddy icon
  // Offset 24  = Pointer to OscarSession object
  // Offset 28  = Icon handle for unplugged icon
  // Offset 32  = Container window original window proc
  // Offset 36  = Bubble window handle
  // Offset 40  = Icon handle for "hot" online buddy icon
  // Offset 44  = Record used to create bubble help window
  // Offset 48  = Filtration in effect on this list
  // Offset 52  = Icon handle for the big red X
  // Offset 56  = UNUSED FOR NOW
  // Offset 60  = Idle-ness was reported already
  // Offset 64  = Buddy popup menu handle
  // Offset 68  = Buddy list popup menu handle
  // Offset 72  = Record on which popup menu was invoked
  // Offset 76  = Original container window procedure (needed by PP notifier)
  // Offset 80  = Container mutex semaphore (can't send msgs during edit)
  // Offset 84  = Buddy pointer for locally added buddies group
  // Offset 88  = Buddy pointer for buddies added because of external IMs
  // Offset 92  = Icon handle for the "I'm Here" icon
  // Offset 96  = Icon handle for the "I'm Away" icon
  // Offset 100 = Icon handle for the filtered online icon
  // Offset 104 = Icon handle for the filtered away icon
  // Offset 108 = Icon handle for the filtered offline icon
  
  WinRegisterClass( ab, "MrMessage Bubble Window", pmBubbleWindow,
   CS_MOVENOTIFY, 24 );
  // 24 bytes extra storage
  // Offset  0 = Pointer to the text contained in the bubble
  // Offset  4 = Non-cached presentation space handle
  // Offset  8 = Memory device context
  // Offset 12 = Memory presentation space
  // Offset 16 = Background bitmap handle
  // Offset 20 = Character line height
  
  WinRegisterClass( ab, "MrMessage Instant Message Window", pmIMwindow, 0, 44 );
  // 44 bytes extra storage
  // Offset  0 = Pointer to Buddy data
  // Offset  4 = Buddy list window handle
  // Offset  8 = Allow send (yes/no depending on recipient online status)
  // Offset 12 = Window procedure for MLE (needed for subclassing)
  // Offset 16 = Rich Text Viewer handle
  // Offset 20 = Previous length of typed text
  // Offset 24 = Previous typing notify state
  // Offset 28 = Writing icon handle
  // Offset 32 = Paper icon handle
  // Offset 36 = Status icon handle
  // Offset 40 = Typing notification icons active, or report buddy status (1/0)
  
  WinRegisterClass( ab, "MrMessage Percent Bar", pmPercentProc, 0, 12 );
  // 12 bytes extra storage
  // Offset  0 = Maximum value
  // Offset  4 = Minimum value
  // Offset  8 = Current value

  i = ACLInitCtl();
  if ( !i )
  {
    debugf( "Aaron's Rich Text Viewer failed to initialize.\n", i );
    return;
  }
  
  WinQueryWindowRect( HWND_DESKTOP, &desktopSize );
  
  mgrWin = WinCreateStdWindow( HWND_DESKTOP, WS_ANIMATE, &frameflgs,
   "MrMessage Session Manager",
   "Mr. Message for OS/2 - Session Manager",
   0, dllModule, MRM_SessionManagerMenu, &clientWin );

  numItems = 0;
  sessions = NULL;
  
  initData.numItemsPtr = &numItems;
  initData.threadInfoPtr = &sessions;
  initData.mutexSem = infoMutex;
  initData.globalSettings = &globalSettings;
  
  swp.x = desktopSize.xLeft + (((desktopSize.xRight - desktopSize.xLeft) -
   SESSION_MGR_WIDTH) / 2);
  swp.y = desktopSize.yBottom + (((desktopSize.yTop - desktopSize.yBottom) -
   SESSION_MGR_HEIGHT) / 2);
  swp.cx = SESSION_MGR_WIDTH;
  swp.cy = SESSION_MGR_HEIGHT;
  // Set up the default information if it's not in the profile

  i = sizeof( SWP );
  PrfQueryProfileData( initData.iniFile, "Session Manager",
   "Window position data", &swp, &i );
  
  WinSetWindowPos( mgrWin, NULLHANDLE, swp.x, swp.y, swp.cx, swp.cy,
   SWP_SIZE | SWP_MOVE );
  
  WinPostMsg( mgrWin, WM_INITULONGS, MPFROMP( &initData ), NULL );

  WinSetFocus( HWND_DESKTOP, mgrWin );

  while ( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
    WinDispatchMsg( hab, &qmsg );
  
  WinDestroyWindow( mgrWin );
  WinDestroyMsgQueue( messq );
  
  debugf( "Cleaning up and waiting for active sessions to end.\n" );
  
  DosRequestMutexSem( infoMutex, SEM_INDEFINITE_WAIT );
  
  if ( sessions && numItems )
  {
    int activeEntries = 0, entriesCleaned = 0;
    
    for ( i=0; i<numItems; ++i )
    {
      if ( sessions[i].userName )
      {
        entriesCleaned++;
        if ( sessions[i].theSession )
        {
          ULONG theThread = sessions[i].threadID;
          
          // Shut down and clean up the session nicely.  We do this outside of
          //  the message queue loop so that we don't hang up the GUI.
          activeEntries++;
          if ( theThread )
          {
            int rc, timePolled = 0;

            debugf( "Session in thread %d is still active.  Waiting for it to end.\n",
             theThread );
            
            rc = DosWaitThread( &theThread, DCWW_NOWAIT );
            while ( rc == ERROR_THREAD_NOT_TERMINATED &&
                     sessions[i].threadID && timePolled < 5 )
            {
              DosSleep( 1000 );
              timePolled++;
              rc = DosWaitThread( &theThread, DCWW_NOWAIT );
            }
            
            if ( rc == ERROR_THREAD_NOT_TERMINATED && sessions[i].threadID )
            {
              debugf( "Timed out waiting for thread to end.  Killing thread %d.\n",
               theThread );
              DosKillThread( theThread );
              // The session might have been hung up in its destructor, so
              //  don't try to delete it again here.  Just let the memory
              //  potentially leak so we can get out of here safely.
            }
          }
          // This will delete the session for us.
        }
        destroySessionSettings( sessions[i].settings );
        CHECKED_FREE( sessions[i].userName );
        CHECKED_FREE( sessions[i].server );
        if ( sessions[i].password ) CHECKED_FREE( sessions[i].password );
      }
    }
    
    DosReleaseMutexSem( infoMutex );
    
    CHECKED_FREE( sessions );
    
    debugf( "Cleaned up %d session entries (%d of which were active).\n",
     entriesCleaned, activeEntries );
  }
  
  if ( isMultiMediaCapable() )
  {
    ULONG tmpVolume = getAudioVolume();
    
    PrfWriteProfileData( initData.iniFile, "Session Manager", "Audio Volume",
     &tmpVolume, sizeof( ULONG ) );
  }
  
  PrfCloseProfile( initData.iniFile );
  WinTerminate( ab );
  
  if ( globalSettings.profile )
  {
    CHECKED_FREE( globalSettings.profile );
  }
  
  for ( i=0; i<EVENT_MAXEVENTS; ++i )
  {
    if ( globalSettings.sounds[i] )
      CHECKED_FREE( globalSettings.sounds[i] );
    if ( globalSettings.rexxScripts[i] )
      CHECKED_FREE( globalSettings.rexxScripts[i] );
    if ( globalSettings.shellCmds[i] )
      CHECKED_FREE( globalSettings.shellCmds[i] );
  }
}

SessionManager :: ~SessionManager()
{
  debugf( "Session manager exiting.\n" );
  if ( infoMutex )
  {
    debugf( "Closing session manager mutex semaphore.\n" );
    DosCloseMutexSem( infoMutex );
  }
}

OscarSession *SessionManager :: getOpenedSession( const char *screenName )
{
  int i;
  
  DosRequestMutexSem( infoMutex, SEM_INDEFINITE_WAIT );
  
  if ( sessions && numItems )
  {
    for ( i=0; i<numItems; ++i )
    {
      if ( sessions[i].userName )
      {
        if ( stricmp( screenName, sessions[i].userName ) == 0 )
        {
          DosReleaseMutexSem( infoMutex );
          return sessions[i].theSession;
        }
      }
    }
  }
  
  DosReleaseMutexSem( infoMutex );
  return NULL;
}

