#define INCL_DOS
#define INCL_WIN
#define INCL_DOSERRORS
#define INCL_MCIOS2
#define INCL_MACHDR
#define INCL_MMIOOS2
#define INCL_REXXSAA

#include <os2.h>
#include <os2me.h>
#include <string.h>
#include <process.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <io.h>
#include <rexxsaa.h>

#include "oscardata.h"
#include "oscarsession.h"
#include "sessionmanager.h"
#include "compatibility.h"
#include "aclctl10.h"

#ifdef MRM_DEBUG_PRINTF
int debugShowRawIncoming = 0;

int debugf( char *fmt, ... )
{
  if ( debugOutputStream )
  {
    int retVal;
    va_list argptr;
    
    fprintf( debugOutputStream, "[TID%02d]: ", (*_threadid) );
    va_start( argptr, fmt );
    retVal = vfprintf( debugOutputStream, fmt, argptr );
    va_end( argptr );
    fflush( debugOutputStream );
    
    return retVal;
  }
  return 0;
}
#endif

#ifdef MRM_DEBUG_MEMORY
blockHistory *memTracer = NULL;
int numMemTracers = 0;
int allocMemTracers = 0;
#endif

static ULONG (EXPENTRY *pMciSendCommand) ( USHORT usDeviceID,
 USHORT usMessage, ULONG ulParam1, PVOID pParam2, USHORT usUserParm ) = NULL;

static ULONG (EXPENTRY *pMmioIdentifyFile) ( PSZ pszFileName,
 PMMIOINFO pmmioinfo, PMMFORMATINFO pmmformatinfo, PFOURCC pfccStorageSystem,
 ULONG ulReserved, ULONG ulFlags ) = NULL;
 
static LONG (EXPENTRY *pRexxStart) ( LONG nArgs, PRXSTRING args,
 PCSZ fileName, PRXSTRING inCoreProc, PCSZ initialEnv, LONG cmdType,
 PRXSYSEXIT sysExitEnv, PSHORT retCode, PRXSTRING retVal ) = NULL;
static APIRET (EXPENTRY *pRexxRegisterSubcomExe) ( PCSZ subcomHandlerEnvName,
 PFN subcomHandler, PUCHAR userArea ) = NULL;
static APIRET (EXPENTRY *pRexxDeregisterSubcom) ( PCSZ subcomHandlerEnvName,
 PCSZ moduleName ) = NULL;
static APIRET (EXPENTRY *pRexxVariablePool) ( SHVBLOCK *var ) = NULL;

static HMODULE mmpmDLL = 0;
static HMODULE mdmDLL = 0;

static HMODULE rexxApiDLL = 0;
static HMODULE rexxDLL = 0;

static ULONG numWaveOuts = 0;
static ULONG numMIDIouts = 0;
static ULONG *eventThreads = NULL;
static USHORT *eventDeviceIDs = NULL;
static ULONG numEventThreads = 0;
static ULONG eventThreadsSize = 0;
static HEV eventMutex = 0;
static ULONG audioVolume = 100;

static int REXXavailable = 0;

static SessionManager *sessionMgr = NULL;

char **waveOutNames, **midiOutNames;

#ifdef MRM_DEBUG_MEMORY

void CHECKED_FREE_TRACER( void *ptr, const char *file, int line, int flags )
{
  int i;
  
  if ( !ptr )
  {
    debugf( "MEMORY MANAGEMENT FAULT!!: FREE NULL POINTER\n" );
    debugf( "ATTEMPTED TO FREE NULL POINTER IN %s LINE %d.\n",
     file, line );
    return;
  }
  
  for ( i=0; i<numMemTracers; ++i )
  {
    if ( !memTracer[i].blockAddr ) continue;
    
    if ( memTracer[i].blockAddr == ptr )
    {
      if ( memTracer[i].freeModule )
      {
        debugf( "MEMORY MANAGEMENT FAULT!!: MULTIPLE FREES (0x%08lx)\n",
         ptr );
        debugf( "BLOCK ALLOCATED IN %s LINE %d.\n", memTracer[i].allocModule,
         memTracer[i].allocLine );
        debugf( "BLOCK DEALLOCATED IN %s LINE %d.\n", memTracer[i].freeModule,
         memTracer[i].freeLine );
        debugf( "ADDITIONAL FREE IN %s LINE %d.\n", file, line );
        return;
      } else {
        if ( !(flags & MRM_DEBUG_MEMCHECK_ISREALLOC) )
          free( ptr );
        
        memTracer[i].freeModule = file;
        memTracer[i].freeLine = line;
        DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT,
         &memTracer[i].timestamp, 4 );
        return;
      }
    }
  }
  
  debugf( "MEMORY MANAGEMENT FAULT!!: FREE A NON-ALLOCATED BLOCK (0x%08lx)\n",
   ptr );
  debugf( "BAD FREE IN %s LINE %d.\n", file, line );
}

void *CHECKED_MALLOC_TRACER( int size, const char *file, int line, int flags,
 void *reallocPtr )
{
  void *retPtr;
  int i, firstFree = numMemTracers, oldest = numMemTracers;
  
  if ( !size )
  {
    debugf( "MEMORY MANAGEMENT FAULT!!: ALLOC OF SIZE 0\n" );
    debugf( "ALLOC IN %s LINE %d.\n", file, line );
    return NULL;
  }
  
  if ( flags & MRM_DEBUG_MEMCHECK_ISCALLOC )
  {
    retPtr = calloc( size, 1 );
  } else {
    if ( flags & MRM_DEBUG_MEMCHECK_ISREALLOC )
    {
      retPtr = realloc( reallocPtr, size );
    } else {
      retPtr = malloc( size );
    }
  }
  
  for ( i=0; i<numMemTracers; ++i )
  {
    if ( !memTracer[i].blockAddr ) continue;
    
    if ( memTracer[i].blockAddr == retPtr )
    {
      memTracer[i].allocModule = file;
      memTracer[i].allocLine = line;
      memTracer[i].size = size;
      memTracer[i].freeModule = NULL;
      memTracer[i].freeLine = 0;
      DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT,
       &memTracer[i].timestamp, 4 );
      return retPtr;
    } else if ( firstFree == numMemTracers && !memTracer[i].blockAddr )
    {
      firstFree = i;
    }
    if ( (oldest == numMemTracers ||
          memTracer[i].timestamp < memTracer[oldest].timestamp) &&
          memTracer[i].allocModule && memTracer[i].freeModule )
    {
      oldest = i;
    }
  }
  
  if ( firstFree == numMemTracers )
  {
    if ( numMemTracers == allocMemTracers )
    {
      // Take the least recently accessed block that has been cleaned up
      //  and remove it from our tracer list.  This could cause some double
      //  frees to be seen as bad frees, but it will keep our tracer size
      //  from growing to ludicrous proportions.
      
      if ( oldest == numMemTracers )
      {
        debugf( "WARNING: Memory tracer history is full.  This may produce false hits\n" );
        debugf( "         pertaining to the block allocated at 0x%08lx.\n",
         retPtr );
        return retPtr;
      } else {
        firstFree = oldest;
      }
    } else {
      numMemTracers++;
    }
  }
  
  memTracer[firstFree].blockAddr = retPtr;
  memTracer[firstFree].allocModule = file;
  memTracer[firstFree].allocLine = line;
  memTracer[firstFree].size = size;
  memTracer[firstFree].freeModule = NULL;
  memTracer[firstFree].freeLine = 0;
  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT,
   &memTracer[firstFree].timestamp, 4 );
  
  return retPtr;
}

#endif

static void soundErrorCleanup( void )
{
  if ( mmpmDLL )
    DosFreeModule( mmpmDLL );
  if ( mdmDLL )
    DosFreeModule( mdmDLL );
  
  mmpmDLL = 0;
  mdmDLL = 0;
  pMciSendCommand = NULL;
}

char *getDeviceDescription( USHORT devType, USHORT devOrdinal )
{
  MCI_SYSINFO_PARMS siParms = { 0 };
  MCI_OPEN_PARMS openParms = { 0 };
  MCI_INFO_PARMS infoParms = { 0 };
  MCI_GENERIC_PARMS genParms = { 0 };
  ULONG rc, bufSize;
  char *outbuf;
  
  CHECKED_CALLOC( 128, outbuf );
  bufSize = 128;
  
  openParms.pszDeviceType = (PSZ) (MAKEULONG( devType, devOrdinal ));
  
  rc = (*pMciSendCommand) ( 0, MCI_OPEN,
   MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE | MCI_WAIT, &openParms, 0 );
  
  if ( rc )
  {
    if ( rc == MCIERR_DEVICE_LOCKED )
    {
      debugf( "Device type %u, ordinal %u is locked by another process.\n",
       devType, devOrdinal );
    } else {
      debugf( "MCI_OPEN failed for device type %u, ordinal %u.  RC = %lu\n",
       devType, devOrdinal, rc );
    }
    CHECKED_FREE( outbuf );
    return NULL;
  }
  
  infoParms.pszReturn = outbuf;
  infoParms.ulRetSize = bufSize;
  
  rc = (*pMciSendCommand) ( openParms.usDeviceID, MCI_INFO,
   MCI_INFO_PRODUCT | MCI_WAIT, &infoParms, 0 );
  
  if ( rc == MCIERR_INVALID_BUFFER )
  {
    CHECKED_REALLOC( outbuf, infoParms.ulRetSize, outbuf );
    bufSize = infoParms.ulRetSize;
    infoParms.pszReturn = outbuf;
    infoParms.ulRetSize = bufSize;
    
    rc = (*pMciSendCommand) ( openParms.usDeviceID, MCI_INFO,
     MCI_INFO_PRODUCT | MCI_WAIT, &infoParms, 0 );
  }
  
  rc = (*pMciSendCommand) ( openParms.usDeviceID, MCI_CLOSE,
   MCI_WAIT, &genParms, 0 );
  
  if ( outbuf && *outbuf )
  {
    return outbuf;
  }
  
  siParms.usDeviceType = devType;
  siParms.ulNumber = devOrdinal;
  siParms.pszReturn = outbuf;
  siParms.ulRetSize = bufSize;
  
  rc = (*pMciSendCommand) ( 0, MCI_SYSINFO,
   MCI_SYSINFO_INSTALLNAME | MCI_WAIT, &siParms, 0 );
  
  if ( rc == MCIERR_INVALID_BUFFER )
  {
    CHECKED_REALLOC( outbuf, siParms.ulRetSize, outbuf );
    bufSize = siParms.ulRetSize;
    siParms.pszReturn = outbuf;
    siParms.ulRetSize = bufSize;
    
    rc = (*pMciSendCommand) ( 0, MCI_SYSINFO,
     MCI_SYSINFO_INSTALLNAME | MCI_WAIT, &siParms, 0 );
  }
  
  if ( outbuf && *outbuf )
  {
    return outbuf;
  }
  
  debugf( "The Media Control Interface could not give me a name for device type %u, ordinal %u (rc=%lu).\n",
   devType, devOrdinal, rc );
  
  CHECKED_FREE( outbuf );
  
  return NULL;
}

int setRexxVariable( char *name, char *value )
{
  SHVBLOCK block;
  
  block.shvcode = RXSHV_SYSET;
  block.shvret = 0;
  block.shvnext = NULL;
  MAKERXSTRING( block.shvname, name, strlen( name ) );
  MAKERXSTRING( block.shvvalue, value, strlen( value ) );
  block.shvvaluelen = strlen( value );
  return pRexxVariablePool( &block );
}

ULONG APIENTRY REXXsubComHandler( PRXSTRING Command, PUSHORT Flags,
 PRXSTRING Retstr )
{
  OscarSession *theSession;
  OscarData *oscarData;
  char *cmdCopy, *tmpPtr, *tmpString, *parm1, *parm2;
  int processed = 0;
  
  if ( Flags ) *Flags = RXSUBCOM_OK;
  if ( !Command ) return 0;
  
  CHECKED_MALLOC( Command->strlength + 1, cmdCopy );
  memcpy( cmdCopy, Command->strptr, Command->strlength );
  cmdCopy[Command->strlength] = 0;
  // Force a NULL terminator
  
  // Now it's safe to tokenize
  
  tmpPtr = cmdCopy;
  
  if ( Flags ) *Flags = RXSUBCOM_FAILURE;
  // Assume this is not our keyword until proven otherwise
  
  while ( *tmpPtr )
  {
    if ( *tmpPtr == ' ' || *tmpPtr == '\t' )
    {
      ++tmpPtr;
      continue;
    }
    tmpString = strchr( tmpPtr, ' ' );
    if ( tmpString )
    {
      *tmpString = 0;
      if ( stricmp( tmpPtr, "SEND_IM" ) == 0 )
      {
        processed = 1;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  SEND_IM - parameter 1 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm1 = tmpPtr;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  SEND_IM - parameter 2 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm2 = tmpPtr;
        tmpPtr = tmpString + 1;
        
        if ( !*tmpPtr )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  SEND_IM - parameter 3 is missing.\n" );
          return 0;
        }
        theSession = sessionMgr->getOpenedSession( parm1 );
        if ( !theSession )
        {
          debugf( "REXX:  SEND_IM - Session (%s) was not running.\n", parm1 );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        oscarData = new OscarData( theSession->getSeqNumPointer() );
        oscarData->sendInstantMessage( parm2, tmpPtr );
        theSession->queueForSend( oscarData, 14 );
        debugf( "REXX:  SEND_IM - Message sent from %s to %s: %s\n", parm1,
         parm2, tmpPtr );
        CHECKED_FREE( cmdCopy );
        return 0;
      } else if ( stricmp( tmpPtr, "POST_TO_CHAT_WINDOW" ) == 0 )
      {
        Buddy *theBuddy;

        processed = 1;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  POST_TO_CHAT_WINDOW - parameter 1 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm1 = tmpPtr;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  POST_TO_CHAT_WINDOW - parameter 2 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm2 = tmpPtr;
        tmpPtr = tmpString + 1;
        
        if ( !*tmpPtr )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  POST_TO_CHAT_WINDOW - parameter 3 is missing.\n" );
          return 0;
        }
        theSession = sessionMgr->getOpenedSession( parm1 );
        if ( !theSession )
        {
          debugf( "REXX:  POST_TO_CHAT_WINDOW - Session (%s) was not running.\n", parm1 );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        
        theBuddy = theSession->getBuddyFromName( parm2 );
        if ( !theBuddy )
        {
          debugf( "REXX:  POST_TO_CHAT_WINDOW - Buddy %s is not in the buddy list of %s.\n",
           parm2, parm1 );
          setRexxVariable( "IMSTATS", "NOSUCHBUDDY" );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        
        if ( theBuddy->imChatWin )
        {
          char *messageTmp;
          int len = strlen( tmpPtr );
          
          CHECKED_STRDUP( tmpPtr, messageTmp );
          WinPostMsg( theBuddy->imChatClientWin, WM_POSTMESSAGE,
           MPFROMP( messageTmp ), NULL );
        }
        
        CHECKED_FREE( cmdCopy );
        return 0;
      } else if ( stricmp( tmpPtr, "USER_STATS" ) == 0 )
      {
        Buddy *theBuddy;
        char buffer[1024];
        char *stat, *prof;
        
        processed = 1;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  USER_STATS - parameter 1 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm1 = tmpPtr;
        tmpPtr = tmpString + 1;
        if ( !*tmpPtr )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  USER_STATS - parameter 2 is missing.\n" );
          return 0;
        }
        parm2 = tmpPtr;
        *tmpString = 0;
        tmpPtr = tmpString + 1;
        
        theSession = sessionMgr->getOpenedSession( parm1 );
        if ( !theSession )
        {
          debugf( "REXX:  USER_STATS - Session (%s) was not running.\n", parm1 );
          setRexxVariable( "IMSTATS", "NOSUCHSESSION" );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        theBuddy = theSession->getBuddyFromName( parm2 );
        if ( !theBuddy )
        {
          debugf( "REXX:  USER_STATS - Buddy %s is not in the buddy list of %s.\n",
           parm2, parm1 );
          setRexxVariable( "IMSTATS", "NOSUCHBUDDY" );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        
        if ( theBuddy->userData.isOnline )
        {
          if ( (theBuddy->userData.userClass & 32) || 
                (theBuddy->userData.userStatus & 1) )
          {
            stat = "AWAY";
            prof = theBuddy->userData.awayMessage;
          } else {
            stat = "ONLINE";
            prof = theBuddy->userData.clientProfile;
          }
        } else {
          stat = "OFFLINE";
        }
        
        sprintf( (char *)buffer, "%s %ld %ld ",
         stat, theBuddy->userData.sessionLen, theBuddy->userData.idleMinutes );
        if ( prof )
        {
          int l = strlen( buffer );
          l += strlen( prof );
          if ( l > 1024 )
          {
            strncat( (char *)buffer, prof, l - strlen( buffer ) - 1 );
          } else {
            strcat( (char *)buffer, prof );
          }
        }
        setRexxVariable( "IMSTATS", (char *)buffer );
      } else if ( stricmp( tmpPtr, "USER_ALIAS" ) == 0 )
      {
        Buddy *theBuddy;
        
        processed = 1;
        tmpPtr = tmpString + 1;
        tmpString = strchr( tmpPtr, ' ' );
        if ( !tmpString )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  USER_ALIAS - parameter 1 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm1 = tmpPtr;
        tmpPtr = tmpString + 1;
        if ( !*tmpPtr )
        {
          if ( Flags ) *Flags = RXSUBCOM_ERROR;
          CHECKED_FREE( cmdCopy );
          debugf( "REXX:  USER_ALIAS - parameter 2 is missing.\n" );
          return 0;
        }
        *tmpString = 0;
        parm2 = tmpPtr;
        tmpPtr = tmpString + 1;
        
        theSession = sessionMgr->getOpenedSession( parm1 );
        if ( !theSession )
        {
          debugf( "REXX:  USER_ALIAS - Session (%s) was not running.\n", parm1 );
          setRexxVariable( "IMALIAS", "NOSUCHSESSION" );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        theBuddy = theSession->getBuddyFromName( parm2 );
        if ( !theBuddy )
        {
          debugf( "REXX:  USER_ALIAS - Buddy %s is not in the buddy list of %s.\n",
           parm2, parm1 );
          setRexxVariable( "IMALIAS", "NOSUCHBUDDY" );
          CHECKED_FREE( cmdCopy );
          return 0;
        }
        if ( theBuddy->alias )
        {
          setRexxVariable( "IMALIAS", theBuddy->alias );
        } else {
          // If no alias is set, return the original name
          setRexxVariable( "IMALIAS", parm2 );
        }
      } else if ( stricmp( tmpPtr, "DEBUG_IM" ) == 0 )
      {
        processed = 1;
        debugf( "REXX message: %s\n", tmpPtr + 9 );
      }
      tmpPtr = tmpString + 1;
    } else {
      // Any keywords that take no parameters handled here...
      //  if I had any.
      CHECKED_FREE( cmdCopy );
      return 0;
    }
  }
  
  if ( !processed )
  {
    debugf( "Unknown command received from REXX: %s\n", cmdCopy ); 
  }
  
  CHECKED_FREE( cmdCopy );
    
  return 0;
}

int initializeCompatibility( int initMMPM )
{
  ULONG rc, i;

  #ifdef MRM_DEBUG_MEMORY
  
  memTracer = (blockHistory *) malloc( sizeof( blockHistory ) * 1024 );
  numMemTracers = 0;
  allocMemTracers = MRM_DEBUG_MAX_TRACERS;
  
  #endif

  rc = DosLoadModule( NULL, 0, "REXX.DLL", &rexxDLL );
  if ( rc == 0 )
  {
    DosLoadModule( NULL, 0, "REXXAPI.DLL", &rexxApiDLL );
    if ( rc == 0 )
    {
      rc = DosQueryProcAddr( rexxDLL, 1, NULL, (PFN *) &pRexxStart );
      rc |= DosQueryProcAddr( rexxApiDLL, 7, NULL,
       (PFN *) &pRexxRegisterSubcomExe );
      rc |= DosQueryProcAddr( rexxApiDLL, 9, NULL,
       (PFN *) &pRexxDeregisterSubcom );
      rc |= DosQueryProcAddr( rexxDLL, 2, NULL, (PFN *) &pRexxVariablePool );
      
      if ( rc == 0 )
      {
        rc = (*pRexxRegisterSubcomExe) ( "MrMessage",
         (PFN)REXXsubComHandler, NULL );
        
        if ( rc == RXSUBCOM_OK )
        {
          debugf( "REXX subcommand handler was registered.\n" );
          REXXavailable = 1;
        }
      }
    }
  }
  if ( rc )
  {
    debugf( "Error initializing REXX interface.  REXX plugins unavailable.\n" );
    REXXavailable = 0;
  }

  if ( !initMMPM )
  {
    debugf( "Sound was disabled by a user command line option.\n" );
  } else {
    rc = DosLoadModule( NULL, 0, "MMPM.DLL", &mmpmDLL );
    
    if ( rc == 0 )
      rc = DosLoadModule( NULL, 0, "MDM.DLL", &mdmDLL );
    
    if ( rc == 0 )
    {
      rc = DosQueryProcAddr( mdmDLL, 1, NULL, (PFN *) &pMciSendCommand );
      rc |= DosQueryProcAddr( mmpmDLL, 92, NULL, (PFN *) &pMmioIdentifyFile );
    }
    
    if ( rc )
    {
      debugf( "Unable to load MultiMedia Presentation Manager (MMPM.DLL).\n" );
      debugf( "Sound will be disabled as a result.\n" );
      
      soundErrorCleanup();
    } else {
      MCI_SYSINFO_PARMS mmInfo = { 0 };
      char outbuf[16];
      
      debugf( "MultiMedia Presentation Manager was initialized at runtime.\n" );
      
      mmInfo.usDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
      mmInfo.pszReturn = outbuf;
      mmInfo.ulRetSize = 16;
      
      rc = (*pMciSendCommand) ( 0, MCI_SYSINFO,
       MCI_SYSINFO_QUANTITY | MCI_WAIT, &mmInfo, 0 );
      
      if ( rc )
      {
        CHECKED_FREE( outbuf );
        debugf( "Error querying number of waveform audio devices (rc=%lu).\n",
         rc );
        debugf( "Waveform audio output is disabled.\n" );
        numWaveOuts = 0;
        waveOutNames = NULL;
      } else {
        numWaveOuts = atol( outbuf );
        if ( numWaveOuts )
        {
          CHECKED_CALLOC( sizeof( char * ) * numWaveOuts, waveOutNames );
        } else waveOutNames = NULL;
      }
      
      mmInfo.usDeviceType = MCI_DEVTYPE_SEQUENCER;
      mmInfo.pszReturn = outbuf;
      mmInfo.ulRetSize = 16;
      
      rc = (*pMciSendCommand) ( 0, MCI_SYSINFO,
       MCI_SYSINFO_QUANTITY | MCI_WAIT, &mmInfo, 0 );
      
      if ( rc )
      {
        CHECKED_FREE( outbuf );
        debugf( "Error querying number of MIDI devices (rc=%lu).\n", rc );
        debugf( "MIDI sequencer support is disabled.\n" );
        numMIDIouts = 0;
        midiOutNames = NULL;
      } else {
        numMIDIouts = atol( outbuf );
        if ( numMIDIouts )
        {
          CHECKED_CALLOC( sizeof( char * ) * numMIDIouts, midiOutNames );
        } else midiOutNames = NULL;
      }
      
      for ( i=1; i<=numWaveOuts; ++i )
      {
        waveOutNames[i-1] =
         getDeviceDescription( MCI_DEVTYPE_WAVEFORM_AUDIO, (USHORT)i );
         
        if ( waveOutNames[i-1] && *(waveOutNames[i-1]) )
        {
          debugf( "Waveform audio device #%lu: %s\n", i, waveOutNames[i-1] );
        }
      }
      
      for ( i=1; i<=numMIDIouts; ++i )
      {
        midiOutNames[i-1] =
         getDeviceDescription( MCI_DEVTYPE_SEQUENCER, (USHORT)i );
         
        if ( midiOutNames[i-1] && *(midiOutNames[i-1]) )
        {
          debugf( "MIDI sequencer device #%lu: %s\n", i, midiOutNames[i-1] );
        }
      }
    }
    debugf( "MultiMedia initialization is complete.\n" );
  }
  
  eventThreadsSize = 8;
  CHECKED_CALLOC( 8 * sizeof( ULONG ), eventThreads );
  CHECKED_CALLOC( 8 * sizeof( USHORT ), eventDeviceIDs );
  // Initially allow for 8 simultaneous event threads
  
  DosCreateMutexSem( NULL, &eventMutex, 0, FALSE );
  
  return 1;
}

void deinitializeCompatibility( void )
{
  ULONG i;
  
  if ( numEventThreads )
  {
    ULONG rc, timePolled;
    
    if ( numEventThreads )
      debugf( "%d event threads are still active.  Shutting them down.\n",
       numEventThreads );
    
    for ( i=0; i<numEventThreads; ++i )
    {
      if ( !eventThreads[i] ) continue;
      
      timePolled = 0;
      rc = DosWaitThread( eventThreads + i, DCWW_NOWAIT );
      while ( rc == ERROR_THREAD_NOT_TERMINATED && timePolled < 10 )
      {
        DosSleep( 1000 );
        if ( !eventThreads[i] ) break;
        timePolled++;
        rc = DosWaitThread( eventThreads + i, DCWW_NOWAIT );
      }
      // Give them 10 seconds to shut down nicely.
      
      if ( rc == ERROR_THREAD_NOT_TERMINATED && eventThreads[i] )
      {
        MCI_GENERIC_PARMS genParms;
        
        debugf( "Timed out waiting for thread to end.\n" );
        
        if ( eventDeviceIDs[i] )
        {
          debugf( "Forcefully closing multimedia device ID %u.\n",
           eventDeviceIDs[i] );
          (*pMciSendCommand) ( eventDeviceIDs[i], MCI_CLOSE, MCI_WAIT,
           &genParms, 0 );
        }
         
        debugf( "Killing thread %d.\n", eventThreads[i] );
        DosKillThread( eventThreads[i] );
      }
      // Then get nasty.
    }
  }
  
  if ( eventThreadsSize )
    CHECKED_FREE( eventThreads );
  
  if ( eventDeviceIDs )
    CHECKED_FREE( eventDeviceIDs );
  
  if ( eventMutex )
    DosCloseMutexSem( eventMutex );
  
  if ( mmpmDLL )
    DosFreeModule( mmpmDLL );
  
  if ( mdmDLL )
    DosFreeModule( mdmDLL );
  
  pMciSendCommand = NULL;
  mmpmDLL = 0;
  mdmDLL = 0;
  
  if ( numWaveOuts )
  {
    for ( i=0; i<numWaveOuts; ++i )
    {
      if ( waveOutNames[i] )
      {
        CHECKED_FREE( waveOutNames[i] );
        waveOutNames[i] = NULL;
      }
    }
    
    CHECKED_FREE( waveOutNames );
    waveOutNames = NULL;
    numWaveOuts = 0;
  }
  if ( numMIDIouts )
  {
    for ( i=0; i<numMIDIouts; ++i )
    {
      if ( midiOutNames[i] )
      {
        CHECKED_FREE( midiOutNames[i] );
        midiOutNames[i] = NULL;
      }
    }
    
    CHECKED_FREE( midiOutNames );
    midiOutNames = NULL;
    numMIDIouts = 0;
  }
  
  if ( REXXavailable )
  {
    (*pRexxDeregisterSubcom) ( "MrMessage", NULL );
    debugf( "REXX subcommand handler was deregistered.\n" );
    if ( rexxDLL )
      DosFreeModule( rexxDLL );
    if ( rexxApiDLL )
      DosFreeModule( rexxApiDLL );
    REXXavailable = 0;
  }
  
  #ifdef MRM_DEBUG_MEMORY
  {
    unsigned long bytesLeaked = 0;
    int leaksFound = 0;
    
    debugf( "LEAK-CHECKING THROUGH %d BLOCKS.\n", numMemTracers );
    
    for ( i=0; i<numMemTracers; ++i )
    {
      if ( !memTracer[i].blockAddr ) continue;
      if ( !memTracer[i].freeModule )
      {
        leaksFound++;
        debugf( "MEMORY LEAK DETECTED!!\n" );
        debugf( "BLOCK 0x%08lx (SIZE %ld) WAS ALLOCATED IN %s LINE %d.\n",
         memTracer[i].blockAddr, memTracer[i].size,
         memTracer[i].allocModule, memTracer[i].allocLine );
        bytesLeaked += memTracer[i].size;
        if ( ((char *)(memTracer[i].blockAddr))[memTracer[i].size - 1] == 0 )
        {
          // If it is NULL terminated, print it like a string.
          debugf( "BLOCK CONTENTS: %s\n", memTracer[i].blockAddr ); 
        }
      }
    }
    
    if ( leaksFound )
    {
      debugf( "FOUND %d MEMORY LEAKS (%ld BYTES).\n", leaksFound, bytesLeaked );
    } else {
      debugf( "NO MEMORY LEAKS WERE FOUND.\n" );
    }
  }
  
  free( memTracer );
  numMemTracers = 0;
  allocMemTracers = 0;
  
  #endif
}

int isMultiMediaCapable( void )
{
  return mmpmDLL != NULLHANDLE;
}

int isRexxCapable( void )
{
  return REXXavailable;
}

void playThread( const char *fileName )
{
  MCI_OPEN_PARMS openParms = { 0 };
  MCI_PLAY_PARMS playParms = { 0 };
  MCI_GENERIC_PARMS genParms = { 0 };
  MMFORMATINFO formatInfo = { 0 };
  MCI_SET_PARMS setParms = { 0 };
  USHORT devType;
  FOURCC fcc;
  ULONG rc, mySlot;
  char *nameCopy;
  
  CHECKED_STRDUP( fileName, nameCopy );
  
  debugf( "Playing media file: %s\n", nameCopy );
  
  DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
  
  for ( mySlot=0; mySlot<eventThreadsSize; ++mySlot )
  {
    if ( eventThreads[ mySlot ] == *_threadid )
    {
      break;
    }
  }
  
  if ( access( nameCopy, 4 ) != 0 )
  {
    debugf( "Unable to locate or read file: %s\n", nameCopy );
    eventThreads[ mySlot ] = 0;
    eventDeviceIDs[ mySlot ] = 0;
    numEventThreads--;
    DosReleaseMutexSem( eventMutex );
    CHECKED_FREE( nameCopy );
    _endthread();
    return;
  }
  
  formatInfo.ulStructLen = sizeof( MMFORMATINFO );
  rc = (*pMmioIdentifyFile) ( (PSZ) fileName, NULL, &formatInfo, &fcc, 0, 0 );
  
  if ( rc )
  {
    debugf( "The multimedia subsystem had an error (%lu) identifying the media format for file: %s\n",
     rc, fileName );
    eventThreads[ mySlot ] = 0;
    eventDeviceIDs[ mySlot ] = 0;
    numEventThreads--;
    DosReleaseMutexSem( eventMutex );
    CHECKED_FREE( nameCopy );
    _endthread();
    return;
  }
  
  if ( formatInfo.ulMediaType == MMIO_MEDIATYPE_AUDIO )
    devType = MCI_DEVTYPE_WAVEFORM_AUDIO;
  else if ( formatInfo.ulMediaType == MMIO_MEDIATYPE_MIDI )
    devType = MCI_DEVTYPE_SEQUENCER;
  else {
    debugf( "Media type could not be played by Mr. Message: %s (type %lu)\n",
     nameCopy, formatInfo.ulMediaType );
    
    eventThreads[ mySlot ] = 0;
    eventDeviceIDs[ mySlot ] = 0;
    numEventThreads--;
    DosReleaseMutexSem( eventMutex );
    CHECKED_FREE( nameCopy );
    _endthread();
    return;
  }

  openParms.pszElementName = (PSZ) (fileName);
  openParms.pszDeviceType = (PSZ) (MAKEULONG( devType, 0 ));
  
  rc = (*pMciSendCommand) ( 0, MCI_OPEN, MCI_OPEN_TYPE_ID |
   MCI_OPEN_ELEMENT | MCI_OPEN_SHAREABLE | MCI_WAIT, &openParms, 0 );  
  
  eventDeviceIDs[ mySlot ] = openParms.usDeviceID;
  
  if ( rc )
  {
    debugf( "Error (%lu) opening the media control interface for file: %s\n",
     rc, nameCopy );

    eventThreads[ mySlot ] = 0;
    eventDeviceIDs[ mySlot ] = 0;
    numEventThreads--;
    DosReleaseMutexSem( eventMutex );
    CHECKED_FREE( nameCopy );
    _endthread();
    return;
  }
  
  setParms.ulAudio = MCI_SET_AUDIO_ALL;
  setParms.ulLevel = audioVolume;
  
  rc = (*pMciSendCommand) ( eventDeviceIDs[ mySlot ], MCI_SET,
   MCI_WAIT | MCI_SET_AUDIO | MCI_SET_VOLUME, &setParms, 0 );
  
  DosReleaseMutexSem( eventMutex );
  
  if ( !rc )
    rc = (*pMciSendCommand) ( openParms.usDeviceID, MCI_PLAY, MCI_WAIT,
     &playParms, 0 );
  
  if ( rc )
  {
    debugf( "The media control interface had an error (%lu) playing: %s\n",
     rc, nameCopy );
    (*pMciSendCommand) ( openParms.usDeviceID, MCI_CLOSE, MCI_WAIT,
     &genParms, 0 );
    
    DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
    eventThreads[ mySlot ] = 0;
    eventDeviceIDs[ mySlot ] = 0;
    numEventThreads--;
    DosReleaseMutexSem( eventMutex );
    CHECKED_FREE( nameCopy );
    _endthread();
    return;
  }
  
  (*pMciSendCommand) ( openParms.usDeviceID, MCI_CLOSE, MCI_WAIT,
   &genParms, 0 );
  
  debugf( "Done playing media file: %s\n", nameCopy );
  
  DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
  
  eventThreads[ mySlot ] = 0;
  eventDeviceIDs[ mySlot ] = 0;
    
  numEventThreads--;
  
  DosReleaseMutexSem( eventMutex );
  CHECKED_FREE( nameCopy );
}

int getEventThreadSlot( void )
{
  int i, tmpSlot;
  
  numEventThreads++;
  
  for ( i=0; i<eventThreadsSize; ++i )
  {
    if ( eventThreads[i] == 0 )
    {
      return i;
    }
  }
  
  if ( numEventThreads > eventThreadsSize )
  {
    i = eventThreadsSize;
    tmpSlot = i;
    
    if ( !eventThreadsSize ) eventThreadsSize = 4;
      else eventThreadsSize *= 2;
    
    CHECKED_REALLOC( eventThreads, sizeof( ULONG ) * eventThreadsSize,
     eventThreads );
    CHECKED_REALLOC( eventDeviceIDs, sizeof( USHORT ) * eventThreadsSize,
     eventDeviceIDs );
    
    for ( ; i<eventThreadsSize; ++i )
    {
      eventThreads[i] = 0;
      eventDeviceIDs[i] = 0;
    }
  }
  // Expand the number of simultaneous threads if needed.
  
  return tmpSlot;
}

void playMediaFile( const char *fileName )
{
  ULONG slot;
  
  if ( !mmpmDLL ) return;
  
  DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
  slot = getEventThreadSlot();
  
  eventThreads[slot] = _beginthread( (void (*) (void *))playThread, NULL,
   65536, (void *)fileName );

  DosReleaseMutexSem( eventMutex );
  // Find an empty slot and launch the thread.
}

typedef struct
{
  char *scriptName;
  char *curUser, *otherUser, *message;
  int mySlot;
} rexxStartupParams;

void rexxThread( rexxStartupParams *params )
{
  ULONG rc;
  SHORT rexxRC;
  RXSTRING result = {0};
  RXSTRING rxParams[3] = {0};
  
  debugf( "REXX thread started.  Running script: %s\n", params->scriptName );
  
  if ( params->curUser )
  {
    rxParams[0].strlength = strlen( params->curUser );
    rxParams[0].strptr = params->curUser;
  }
  if ( params->otherUser )
  {
    rxParams[1].strlength = strlen( params->otherUser );
    rxParams[1].strptr = params->otherUser;
  }
  if ( params->message )
  {
    rxParams[2].strlength = strlen( params->message );
    rxParams[2].strptr = params->message;
  }
  rc = (*pRexxStart) ( 3, (RXSTRING *)rxParams, params->scriptName, NULL,
   "MrMessage", RXSUBROUTINE, NULL, &rexxRC, &result );
  
  if ( result.strlength )
    DosFreeMem( result.strptr );
  
  if ( params->curUser ) CHECKED_FREE( params->curUser );
  if ( params->otherUser ) CHECKED_FREE( params->otherUser );
  if ( params->message ) CHECKED_FREE( params->message );
  
  if ( rc != 0 )
  {
    debugf( "Error (%d) occured during RexxStart.  REXX thread ending.\n", rc );
  } else {
    debugf( "REXX script (%s) finished executing.  REXX thread ending.\n",
     params->scriptName );
  }
  
  DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
  eventThreads[ params->mySlot ] = 0;
  eventDeviceIDs[ params->mySlot ] = 0;
  numEventThreads--;
  DosReleaseMutexSem( eventMutex );
  
  CHECKED_FREE( params->scriptName );
  CHECKED_FREE( params );
}

int handleMrMessageEvent( UI_EventType eventType,
 sessionThreadSettings *specificSettings,
 sessionThreadSettings *globalSettings, eventData *theEventData, int noSound )
{
  sessionThreadSettings *theSettings;
  int ret = 0;
  
  if ( !specificSettings || specificSettings->inheritSettings )
    theSettings = globalSettings;
  else
    theSettings = specificSettings;
  
  if ( !noSound && (theSettings->settingFlags[ eventType ] & SET_SOUND_ENABLED) )
  {
    playMediaFile( theSettings->sounds[ eventType ] );
    ret |= EVENT_PROCESSED_SOUND;
  }
  
  if ( theSettings->settingFlags[ eventType ] & SET_REXX_ENABLED &&
        REXXavailable )
  {
    RXSTRING result;
    char *tmpStr;
    rexxStartupParams *threadInit;
    int i, j, slot, len;
    
    result.strlength = 0;
    result.strptr = NULL;
    
    debugf( "Launching REXX Script: %s\n",
     theSettings->rexxScripts[eventType] );
     
    CHECKED_MALLOC( sizeof( rexxStartupParams ), threadInit );
    
    if ( theEventData->currentUser )
    {
      len = strlen( theEventData->currentUser );
      CHECKED_MALLOC( len + 1, tmpStr );
      for ( i=0, j=0; i<len; i++ )
      {
        if ( theEventData->currentUser[i] != ' ' )
        {
          tmpStr[j] = theEventData->currentUser[i];
          ++j;
        }
      }
      tmpStr[j] = 0;
      threadInit->curUser = tmpStr;
    } else {
      threadInit->curUser = NULL;
    }
     
    if ( theEventData->otherUser )
    {
      len = strlen( theEventData->otherUser );
      CHECKED_MALLOC( len + 1, tmpStr );
      for ( i=0, j=0; i<len; i++ )
      {
        if ( theEventData->otherUser[i] != ' ' )
        {
          tmpStr[j] = theEventData->otherUser[i];
          ++j;
        }
      }
      tmpStr[j] = 0;
      threadInit->otherUser = tmpStr;
    } else {
      threadInit->otherUser = NULL;
    }
    
    // Note:  Screen names are passed with NO WHITESPACE so that they
    //        can used as separated parameters using whitespace.  Any message
    //        that is passed will be the last parameter passed, so separation
    //        is not necessary.
     
    if ( theEventData->message )
    {
      CHECKED_STRDUP( theEventData->message, threadInit->message );
    } else {
      threadInit->message = NULL;
    }
    
    CHECKED_STRDUP( theSettings->rexxScripts[ eventType ], 
     threadInit->scriptName );
    
    DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
    slot = getEventThreadSlot();
    threadInit->mySlot = slot;
    eventThreads[slot] = _beginthread( (void (*) (void *))rexxThread, NULL,
     65536, (void *)threadInit );
    DosReleaseMutexSem( eventMutex );
    
    ret |= EVENT_PROCESSED_SCRIPT;
  }
  
  // Handle shell script launching here too...
  
  return ret;
}

unsigned long testSettingsFlag( unsigned long theFlag,
 sessionThreadSettings *specificSettings,
 sessionThreadSettings *globalSettings )
{
  sessionThreadSettings *theSettings;
  
  if ( !specificSettings || specificSettings->inheritSettings )
    theSettings = globalSettings;
  else
    theSettings = specificSettings;
  
  return theSettings->sessionFlags & theFlag;
}

void setAudioVolume( ULONG volume )
{
  int i;
  
  if ( !mmpmDLL ) return;
  
  DosRequestMutexSem( eventMutex, SEM_INDEFINITE_WAIT );
  
  if ( volume > 100 ) audioVolume = 100;
  else audioVolume = volume;
  
  for ( i=0; i<eventThreadsSize; ++i )
  {
    if ( eventThreads[i] )
    {
      MCI_SET_PARMS setParms = { 0 };
      setParms.ulAudio = MCI_SET_AUDIO_ALL;
      setParms.ulLevel = audioVolume;
      (*pMciSendCommand) ( eventDeviceIDs[i], MCI_SET, MCI_WAIT |
       MCI_SET_AUDIO | MCI_SET_VOLUME, &setParms, 0 );
    }
  }
  // Set volume levels for all currently playing sounds/devices
  
  DosReleaseMutexSem( eventMutex );
}

ULONG getAudioVolume( void )
{
  return audioVolume;
}

void registerSessionManager( SessionManager *smgr )
{
  sessionMgr = smgr;
}
