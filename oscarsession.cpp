#define INCL_DOS
#define INCL_WIN
#define INCL_DOSERRORS
#define TCPV40HDRS

#include <os2.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include "oscarsession.h"
#include "sessionmanager.h"
#include "compatibility.h"

extern "C" {
#include <sys\socket.h>
#include <netinet\in.h>
#include <netdb.h>
#include <types.h>
}

#define RESPONSE_TIMEOUT 5000

#define SET_NEXT_STATE( nextState )                        \
  DosRequestMutexSem( waitStateMux, SEM_INDEFINITE_WAIT ); \
  DosResetEventSem( stateWakeup, (ULONG *)&rc );           \
  waitForState = nextState;                                \
  DosPostEventSem( continuousReceive );                    \
  DosReleaseMutexSem( waitStateMux );

#define ADVANCE_STATE_MACHINE \
  status = waitForState;      \
  waitForState = fatalError;

#define WAIT_CHECK_FOR_ERRORS( timeout, waitFailureCode )      \
  if ( DosWaitEventSem( stateWakeup, timeout ) )               \
  {                                                            \
    errorCode = waitFailureCode;                               \
    status = fatalError;                                       \
    debugf( "%s\n", errorMessageStrings[ errorCode ] );        \
    WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE,            \
     MPFROMSHORT( errorCode ), MPFROMP( myRecordInManager ) ); \
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );           \
    return;                                                    \
  }                                                            \
  if ( status == fatalError )                                  \
  {                                                            \
    debugf( "%s\n", errorMessageStrings[ errorCode ] );        \
    WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE,            \
     MPFROMSHORT( errorCode ), MPFROMP( myRecordInManager ) ); \
    receiveData.printData();                                   \
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );           \
    return;                                                    \
  }

int screenNamesEqual( const char *sn1, const char *sn2 )
{
  int i, j, len1, len2;
  
  len1 = strlen( sn1 );
  len2 = strlen( sn2 );
  for ( i=0, j=0; i<len1 && j<len2; i++, j++ )
  {
    while ( sn1[i] == ' ' && i < len1 ) ++i;
    while ( sn2[j] == ' ' && j < len2 ) ++j;
    if ( i > len1 || j > len2 ) break;
    if ( tolower( sn1[i] ) != tolower( sn2[j] ) ) return 0;
  }
  while ( sn1[i] == ' ' && i < len1 ) ++i;
  while ( sn2[j] == ' ' && j < len2 ) ++j;
  
  if ( i == len1 && j == len2 ) return 1;
  
  return 0;
}

void dataReceiverThread( OscarSession *sessionData )
{
  OscarData *dataPacket = &(sessionData->receiveData);
  UserInformation tmpUserInfo;
  unsigned short AIMerror;
  
  int theSocket, firstIn = 1;
  
  debugf( "Starting data receiver thread.\n" );
  
  while ( 1 )
  {
    ULONG rc;
    
    if ( !firstIn )
    {
      debugf( "Receiver thread waiting.\n" );
      DosWaitEventSem( sessionData->continuousReceive, SEM_INDEFINITE_WAIT );
      debugf( "Receiver thread was told to wake up.\n" );
    } else firstIn = 0;
    
    DosResetEventSem( sessionData->continuousReceive, &rc );
    
    if ( sessionData->status == fatalError ||
          sessionData->status == shutDown ) break;
    
    if ( sessionData->waitForState < BOSconnectionAck )
    {
      theSocket = sessionData->oscarSocket;
      debugf( "Listening to Oscar socket (wait for state %d)\n",
       sessionData->waitForState );
    } else {
      theSocket = sessionData->BOSsocket;
      debugf( "Listening to BOS socket (wait for state %d)\n",
       sessionData->waitForState );
    }
    
    debugf( "Waiting to receive on socket.\n" );
    dataPacket->receiveData( theSocket );
    
    if ( dataPacket->getStatus() != 0 )
    {
      debugf( "We're getting booted by AOL.  Bummer.\n" );
      
      if ( !sessionData->reconnect )
      {
        if ( dataPacket->getStatus() == 10054 || dataPacket->getStatus() == 10004 )
        {
          // Under these conditions, trying to close the BOS socket will hang.
          // So avoid that mess by not trying to close it.  The socket will
          //  get cleaned up by the system when we exit.
          sessionData->BOSsocket = 0;
        }
        sessionData->status = fatalError;
        DosPostEventSem( sessionData->pauseResume );
        
        sessionData->reconnect = 1;
        sessionData->reconnectIP = NULL;
        sessionData->reconnectCookie = NULL;
        sessionData->reconnectCookieLen = 0;
        DosPostEventSem( sessionData->sessionShutdown );
        // Reconnect automatically (trigger a warm boot)
        
        break;
      }
    }

    debugf( "Data received.  Previous state: %d, Wait for state: %d.\n",
     sessionData->status, sessionData->waitForState );
    
    #ifdef MRM_DEBUG_PRINTF
    {
      extern int debugShowRawIncoming;
      
      if ( debugShowRawIncoming )
      {
        dataPacket->printData();
      }
    }
    #endif
    
    if ( dataPacket->isMOTD() )
    {
      debugf( "AOL message of the day was received and promptly ignored.\n" );
      firstIn = 1;
      continue;
    }
    
    AIMerror = dataPacket->getAIMerror();
    
    if ( AIMerror != 0 )
    {
      // May want to report certain AIM errors back to the user eventually.
      // For now they'll just crop up in the debug on the console.
      firstIn = 1;
      continue;
    }
    
    switch ( sessionData->status )
    {
      case noSession:
        // Looking for a connection acknowledgement
        
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( dataPacket->isConnectionAck() &&
              sessionData->waitForState == oscarConnectionAck )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_OSC_BAD_ACK;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case oscarConnectionAck:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == oscarMD5authKey &&
              dataPacket->getMD5AuthKey( &sessionData->authData ) )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else if ( sessionData->waitForState == oscarMD5authKey )
        {
          dataPacket->getAuthResponseData( &sessionData->authData );
          // If we got a 0x17 sub 3 with an error, this will catch it.
          
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_OSC_BAD_AUTH;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case oscarMD5authKey:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == oscarLoginAuthorization &&
              !dataPacket->getAuthResponseData( &sessionData->authData ) )
        {
          ULONG junk;
          
          DosResetEventSem( sessionData->continuousReceive, &junk );
          // Stops us from receiving after this packet
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_OSC_BAD_AUTH;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case nextSocket:
        // This receive call is the result of closing the Oscar socket in
        //  order to change over to the BOS socket.  Eat it without any
        //  nasty-grams.
        debugf( "Switching sockets\n" );
        sessionData->status = oscarLoginAuthorization;
      break;
      case oscarLoginAuthorization:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( dataPacket->isConnectionAck() &&
              sessionData->waitForState == BOSconnectionAck )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_BAD_ACK;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSconnectionAck:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( dataPacket->isHostReadyMessage(
              &sessionData->supportedFamilies ) &&
              sessionData->waitForState == BOShostReady )
        {
          sessionData->reportAvailableServices();
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_BAD_LOGIN;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOShostReady:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( dataPacket->isFamilyVersions() &&
              sessionData->waitForState == BOSservicesVersion )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_BAD_SERV_VER;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSservicesVersion:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( !dataPacket->getUserInformation( &sessionData->userInfo ) &&
              sessionData->waitForState == BOScurrentUserInfo )
        {
          // There may be subtle differences in the screen name in this user
          //  info data from the way that the user entered it, so use the
          //  user's way.  This will help keep all the settings unified in
          //  the INI file (since they key off of the session name).
          CHECKED_FREE( sessionData->userInfo.screenName );
          CHECKED_STRDUP( sessionData->myUserName,
           sessionData->userInfo.screenName );
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_SELF_INFO;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOScurrentUserInfo:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSrateInformation &&
              dataPacket->isRateInformation( &sessionData->rateInformation ) )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_RATE_INFO;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSrateInformation:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSserverStoredLimits &&
              dataPacket->isSSIlimits() )
        {
          // Not doing anything with this information for now
          DosPostEventSem( sessionData->stateWakeup );
          debugf( "Received SSI limit information (ignored for now).\n" );
        } else {
          firstIn = 1;
          // Received something else (user info packet probably).  Cycle again.
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSserverStoredLimits:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSserverStoredInfo &&
              !dataPacket->getSSIData( &sessionData->ssiData ) )
        {
          DosPostEventSem( sessionData->stateWakeup );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_SSI;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSserverStoredInfo:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSlocationLimits )
        {
          if ( !dataPacket->getSSIData( &sessionData->ssiData ) )
          {
            // Actually, this isn't the location limits info, it's a
            //  continuation of the buddy list.  Append to the buddy list
            //  but stay in the same state.
            debugf( "Buddy list continuation was received.\n" );
            DosPostEventSem( sessionData->continuousReceive );
            // Allow us to receive another packet with the same state
          } else {
            // Location limits info
            // Not doing anything with this information for now
            DosPostEventSem( sessionData->stateWakeup );
            debugf( "Received location service limitations (ignored for now).\n" );
          }
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_LOCATION_LIM;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSlocationLimits:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSbuddyManagementLimits &&
              dataPacket->isBuddyManagementLimits() )
        {
          // Not doing anything with this information for now
          DosPostEventSem( sessionData->stateWakeup );
          debugf( "Received buddy list management limitations (ignored for now).\n" );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_BLM_LIM;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSbuddyManagementLimits:
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSprivacyLimits &&
              dataPacket->isPrivacyLimits() )
        {
          // Not doing anything with this information for now
          DosPostEventSem( sessionData->stateWakeup );
          debugf( "Received privacy limitations (ignored for now).\n" );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_PRIVACY_LIM;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      break;
      case BOSprivacyLimits:
      {
        DosRequestMutexSem( sessionData->waitStateMux, SEM_INDEFINITE_WAIT );
        // Make sure we post the wakeup for the appropriate waitFor state
        if ( sessionData->waitForState == BOSICBMparams &&
              dataPacket->isICBMparams() )
        {
          // Not doing anything with this information for now
          DosPostEventSem( sessionData->stateWakeup );
          debugf( "Received ICBM parameters (ignored for now).\n" );
        } else {
          sessionData->status = fatalError;
          sessionData->errorCode = MRM_BOS_NO_PRIVACY_LIM;
          dataPacket->printData();
          DosPostEventSem( sessionData->stateWakeup );
        }
        DosReleaseMutexSem( sessionData->waitStateMux );
      }
      break;
      case BOSserviceLoop:
      {
        rendevousInfo extraIMinfo;
        char *userName, *message, *ipAddr;
        unsigned char *cookie;
        unsigned short tmpShort;
        
        // Any number of things can fly across in this state.
        
        firstIn = 1; // Don't wait for any states before receiving
        
        if ( !dataPacket->getUserInformation( &tmpUserInfo ) )
        {
          UserInformation *userInfo = new UserInformation( tmpUserInfo );
          
          userInfo->printData();
          
          WinPostMsg( sessionData->sessionWindow, WM_BUDDYSTATUS,
           MPFROMP( userInfo ), NULL );
        } else if ( dataPacket->instantMessageReceived( &userName, &message,
                     &extraIMinfo ) )
        {
          // Got an instant message from someone!
          
          if ( userName != NULL && message != NULL &&
                extraIMinfo.rendevousType == RENDEVOUS_TYPE_VOID )
          {
            WinPostMsg( sessionData->sessionWindow, WM_IMRECEIVED,
             MPFROMP( userName ), MPFROMP( message ) );
          } else {
            if ( extraIMinfo.invitationText )
              CHECKED_FREE( extraIMinfo.invitationText );
            if ( extraIMinfo.chatroomName )
              CHECKED_FREE( extraIMinfo.chatroomName );
            if ( userName )
              CHECKED_FREE( userName );
          }
        } else if ( dataPacket->isServerPause() )
        {
          ULONG junk;
          DosResetEventSem( sessionData->pauseResume, &junk );
          debugf( "Server requested a pause.  Sending acknowledgement.\n" );
          sessionData->sendData.prepareServerPauseAck();
          sessionData->sendData.sendData( sessionData->BOSsocket );
        } else if ( dataPacket->isServerResume() )
        {
          debugf( "Server resume requested.  Resuming communications.\n" );
          DosPostEventSem( sessionData->pauseResume );
        } else if ( dataPacket->isServerMigrate( &ipAddr, &cookie, &tmpShort ) )
        {
          debugf( "Attempting to migrate over to a different server (%s).\n",
           ipAddr );
          sessionData->reconnect = 1;
          sessionData->reconnectIP = ipAddr;
          sessionData->reconnectCookie = cookie;
          sessionData->reconnectCookieLen = tmpShort;
          DosPostEventSem( sessionData->sessionShutdown );
          // Signal a warm boot, but don't tie up this thread doing it.
        } else if ( dataPacket->isServerRateWarning() )
        {
          int i;
          debugf( "We're getting spanked by the server for being too fast.\n" );
          debugf( "Here's the packet which I don't totally understand yet:\n" );
          dataPacket->printData();
          debugf( "Attempting to compensate by slowing down all regulation threads.\n" );
          for ( i=0; sessionData->rateInformation[i].rateClass; ++i )
          {
            sessionData->rateInformation[i].slowDown = 1;
          }
        } else if ( dataPacket->isMessageAck() )
        {
          // Don't really care about message acks
        } else {
          int retCode;
          Buddy *theBuddy;          
          
          retCode = dataPacket->isTypingNotification( &userName );
          if ( retCode )
          {
            switch ( retCode )
            {
              case 1:
                debugf( "Typing notification: %s has stopped typing.\n",
                 userName );
              break;
              case 2:
                debugf( "Typing notification: %s has text typed.\n",
                 userName );
              break;
              case 3:
                debugf( "Typing notification: %s has begun typing.\n",
                 userName );
              break;
              default:
                debugf( "Typing notification received, but type (%d) was not known.\n",
                 retCode - 1 );
            }
            
            theBuddy = sessionData->getBuddyFromName( userName );
            if ( theBuddy && theBuddy->imChatWin )
            {
              WinPostMsg( theBuddy->imChatWin,
               WM_TYPING_NOTIFY, MPFROMSHORT( retCode - 1 ), NULL );
            }
            
            CHECKED_FREE( userName );
          } else {
            debugf( "Unhandled data packet received from AOL server:\n" );
            dataPacket->printData();
          }
        }
      }
      break;
    }
  }
  
  debugf( "Ending data receiver thread.\n" );
  sessionData->dataReceiverThreadID = 0;
}

void rateThread( userRateInformation *rateInfo )
{
  REQUESTDATA rd;
  ULONG dataLen;
  PVOID theData;
  BYTE priority;
  ULONG cTime, lastSend, sleepTime;
  OscarData *queuedData;
  ULONG currentLevel, previousLevel, winNum;
  
  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &lastSend, 4 );
  previousLevel = rateInfo->currentLevel;
  rateInfo->slowDown = 0;
  winNum = 1;
  
  debugf( "Rate regulation thread is starting up.\n" );
  
  while ( DosReadQueue( rateInfo->outgoingData, &rd, &dataLen, &theData,
           0, DCWW_WAIT, &priority, 0 ) == 0 )
  {
    // First check if the server needs a breather.  This will stay posted
    //  until it is cleared due to a SNAC 1 sub B.
    DosWaitEventSem( rateInfo->pauseResume, SEM_INDEFINITE_WAIT );
    
    // Data is ready to be sent.  Check if we've waited long enough.
    
    if ( rateInfo->slowDown )
    {
      previousLevel = 0;
      // Zap the hell out of the rate level just to be safe.
      rateInfo->slowDown = 0;
    }
    
    if ( rd.ulData == 0 ) break;
    // That's our signal to shut down
    
    winNum++;
    if ( winNum > rateInfo->windowSize )
    {
      winNum = rateInfo->windowSize;
    }
    
    DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &cTime, 4 );
    currentLevel = (((winNum - 1) * previousLevel) /
     winNum) + ((cTime - lastSend) / winNum);
    
    // (w-1)*p/w + (x-l)/w >= C
    // Where w = the window size,
    //       p = the previous level,
    //       l = the last send time (in ms)
    //       C = the clear level
    //       x = the time at which it is safe to send
    //
    // Manipulation yields:
    // x >= Cw + l - (w-1)*p
    //
    // Time to sleep is x - cTime
     
    debugf( "Previous level: %lu, Proposed current level: %lu, Max level: %lu.\n",
     previousLevel, currentLevel, rateInfo->maxLevel );
    
    sleepTime = 0;
    if ( currentLevel < rateInfo->clearLevel )
    {
      sleepTime = (rateInfo->clearLevel * winNum) + lastSend -
       ((winNum - 1) * previousLevel) - cTime;
      debugf( "Sleeping for %lu milliseconds.\n", sleepTime );
      DosSleep( sleepTime );
      debugf( "Slept %lu milliseconds.\n", sleepTime );
    }
    
    debugf( "Sending queued data.  Last send: %lu, Queued: %lu, Rate: %lu\n",
     lastSend, cTime, rateInfo->clearLevel );
    
    queuedData = (OscarData *)rd.ulData;
    
    if ( rateInfo->sendSocket )
    {
      DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &cTime, 4 );
      queuedData->sendData( rateInfo->sendSocket );
      
      currentLevel = (((winNum - 1) * previousLevel) /
       winNum) + ((cTime - lastSend) / winNum);
      // Recalculate the level with the most accurate data
      
      if ( currentLevel > rateInfo->maxLevel )
      {
        currentLevel = rateInfo->maxLevel;
      }
      
      previousLevel = currentLevel;
    }
    // If the socket was zeroed out, just fast-forward through the rest of the
    //  queue and delete everything
    
    lastSend = cTime;
    
    delete queuedData;
  }
  
  debugf( "Rate regulation thread is shutting down.\n" );
}

static void addAllGroupMembers( int entry, Buddy **buddyList,
 SSIData *ssiData, HWND sessionWindow, HEV wakeupSem );
// Defined below

OscarSession :: OscarSession( char *userName, char *password, char *server,
 int port, HWND mgrWin, MINIRECORDCORE *theRecord, void *info,
 int threadEntry, sessionThreadSettings *mySettings, HWND progressDlg )
{
  struct hostent *hostEnt;
  struct in_addr oscarAddr;
  struct sockaddr_in serverDesc;
  char *tmpStr;
  int rc, i, j, k, found;
  buddyListCreateData buddyCreateData;
  sessionThreadInfo **threadInfo = (sessionThreadInfo **) info;
  
  startServer = server;
  startPort = port;
  myUserName = userName;
  myPassword = password;
  
  settings = mySettings;
  sendData.setSeqNumPointer( &currentSequence );
  
  sessionManagerWin = mgrWin;
  supportedFamilies = NULL;
  rateInformation = NULL;
  
  currentSequence = 0;
  myRecordInManager = theRecord;
  status = noSession;
  waitForState = fatalError;
  
  sessionWindow = 0;
  dataReceiverThreadID = 0;
  stateWakeup = 0;
  pauseResume = 0;
  
  errorCode = MRM_NO_ERROR;
  BOSsocket = 0;
  buddyList = NULL;
  numBuddies = 0;
  
  sessionShutdown = 0;
  numFlashing = 0;
  
  reconnect = 0;
  reconnectIP = NULL;
  reconnectCookie = NULL;
  reconnectCookieLen = 0;
  
  (*threadInfo)[threadEntry].theSession = this;
  
  debugf( "Creating socket.\n" );
  
  oscarSocket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
  if ( oscarSocket < 0 )
  {
    errorCode = MRM_OSC_SOCKET_OPEN;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  debugf( "Looking up Oscar server host IP address.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 0, 20 ),
   MPFROMP( "Looking up Oscar server..." ) );
  
  if ( !server )
  {
    hostEnt = gethostbyname( "login.oscar.aol.com" );
    if ( !hostEnt )
    {
      errorCode = MRM_OSC_GET_OSC_IP;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
       MPFROMP( theRecord ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      return;
    }
  } else {
    oscarAddr.s_addr = inet_addr( server );
    if ( oscarAddr.s_addr == 0xffffffff )
    {
      // 16-bit stack call to gethostbyname for a dotted decimal host name
      //  returns NULL.  Check with inet_addr for this condition first.
      
      hostEnt = gethostbyname( server );
      if ( !hostEnt )
      {
        errorCode = MRM_OSC_GET_OSC_IP;
        status = fatalError;
        debugf( "%s\n", errorMessageStrings[ errorCode ] );
        WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
         MPFROMP( theRecord ) );
        WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
        return;
      }
      memcpy( &oscarAddr.s_addr, hostEnt->h_addr, hostEnt->h_length );
    }
  }
  
  memset( &serverDesc, 0, sizeof( struct sockaddr_in ) );
  
  // serverDesc.sin_len = sizeof( struct sockaddr_in );
  serverDesc.sin_family = AF_INET;
  serverDesc.sin_addr.s_addr = oscarAddr.s_addr;
  if ( !port )
  {
    serverDesc.sin_port = SWAP2( 5190 ); // Standard OSCAR port
  } else {
    serverDesc.sin_port = SWAP2( port );
  }
  
  debugf( "Connecting socket to Oscar server.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 1, 20 ),
   MPFROMP( "Connecting to Oscar server..." ) );
  
  rc = connect( oscarSocket, (struct sockaddr *)&serverDesc,
   sizeof( struct sockaddr_in ) );
  
  if ( rc )
  {
    errorCode = MRM_OSC_SOCKET_CONNECT;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  debugf( "Connected to Oscar server.  Waiting for ACK.\n" );
  
  if ( DosCreateEventSem( NULL, &stateWakeup, 0, FALSE ) )
  {
    errorCode = MRM_GEN_CREATE_SEM;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  if ( DosCreateEventSem( NULL, &continuousReceive, 0, FALSE ) )
  {
    errorCode = MRM_GEN_CREATE_SEM;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  if ( DosCreateEventSem( NULL, &pauseResume, 0, TRUE ) )
  {
    // Initially posted... clear when we need to pause data sending
    errorCode = MRM_GEN_CREATE_SEM;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  if ( DosCreateMutexSem( NULL, &waitStateMux, 0, FALSE ) )
  {
    errorCode = MRM_GEN_CREATE_MTX;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  if ( DosCreateMutexSem( NULL, &buddyListAccessMux, 0, FALSE ) )
  {
    errorCode = MRM_GEN_CREATE_MTX;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  SET_NEXT_STATE( oscarConnectionAck );
  
  dataReceiverThreadID = _beginthread( (void (*) (void *))dataReceiverThread,
   NULL, 65536, this );
   
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_ACK_TIMEOUT );
  
  // Got our ACK within the timeout period

  ADVANCE_STATE_MACHINE;
  
  sendData.prepareConnectionAck();
  debugf( "Sending connection acknowledgement to server.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 2, 20 ),
   MPFROMP( "Sending connection acknowledgement..." ) );
  sendData.sendData( oscarSocket );
  
  sendData.prepareAuthKeyRequest( userName );
  debugf( "Requesting MD5 authorization key from server.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 3, 20 ),
   MPFROMP( "Requesting MD5 authorization key..." ) );
  sendData.sendData( oscarSocket );
  
  SET_NEXT_STATE( oscarMD5authKey );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_AUTH_TIMEOUT );
  ADVANCE_STATE_MACHINE;
  
  if ( !authData.errorCode )
  {
    // We could get a "service temporarily unavailable" message
    //  in response to requesting an MD5 key.
    
    debugf( "Authorization key was received.\n" );
    
    sendData.prepareLoginData( userName, password, &authData );
    debugf( "Sending login packet to Oscar server.\n" );
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 4, 20 ),
     MPFROMP( "Sending encrypted login..." ) );
    sendData.sendData( oscarSocket );
    
    debugf( "Waiting for authorization response.\n" );
    
    SET_NEXT_STATE( oscarLoginAuthorization );
    WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_AUTH_TIMEOUT );
    ADVANCE_STATE_MACHINE;
    
    authData.printData();
  }

  if ( !authData.errorCode && authData.errorURL )
  {
    if ( strstr( authData.errorURL, "PASSWD" ) )
    {
      // It's apparently too difficult for AOL to fill in the errorCode
      //  field so I have to search the error URL field to get more
      //  information about what the real problem was.  Sad.
      authData.errorCode = 5;
    }
  }
  
  if ( authData.errorCode )
  {
    USHORT theWindowError;
    
    errorCode = MRM_OSC_AUTH_ERROR;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    
    switch ( authData.errorCode )
    {
      case 1:
      case 4:
        theWindowError = MRM_AIM_BAD_IDPASS;
      break;
      case 5:
        theWindowError = MRM_AIM_BAD_PASSWORD;
      break;
      case 6:
        theWindowError = MRM_AIM_AUTH_FAILED;
      break;
      case 8:
        theWindowError = MRM_AIM_ACCOUNT_DELETED;
      break;
      case 2:
      case 12:
      case 13:
      case 18:
      case 19:
      case 20:
      case 21:
      case 26:
      case 31:
        theWindowError = MRM_AIM_SERVICE_TEMP_UNAVAILABLE;
      break;
      case 17:
        theWindowError = MRM_AIM_ACCOUNT_SUSPENDED;
      break;
      case 24:
      case 29:
        theWindowError = MRM_AIM_RATE_LIMIT_HIT;
      break;
      case 27:
      case 28:
        theWindowError = MRM_AIM_FORCE_UPGRADE;
      break;
      case 32:
        theWindowError = MRM_AIM_INVALID_SECUREID;
      break;
      case 34:
        theWindowError = MRM_AIM_ACCOUNT_SUSPENDED_MINOR;
      break;
      default:
        theWindowError = MRM_AIM_UNKNOWN_LOGIN_FAILURE;
    }
    
    debugf( "The specific problem was:\n" );
    debugf( "%s\n", errorMessageStrings[ theWindowError ] );
    
    if ( authData.errorURL )
    {
      debugf( "The server offers the following description of the message:\n" );
      debugf( "%s\n", authData.errorURL );
    }
     
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( theWindowError ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    
    return;
  }
  
  if ( !authData.BOSserver )
  {
    errorCode = MRM_OSC_NO_BOS;
    status = fatalError;
    
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  BOSsocket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
  
  if ( BOSsocket < 0 )
  {
    errorCode = MRM_BOS_SOCKET_OPEN;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  tmpStr = strrchr( authData.BOSserver, ':' );
  if ( tmpStr ) *tmpStr = 0;
  // The port number is usually returned, but not for old servers
  
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 5, 20 ),
   MPFROMP( "Looking up Basic Oscar Service (BOS) server..." ) );
  
  oscarAddr.s_addr = inet_addr( authData.BOSserver );
  if ( oscarAddr.s_addr == 0xffffffff )
  {
    // 16-bit stack call to gethostbyname for a dotted decimal host name
    //  returns NULL.  Check with inet_addr for this condition first.
    
    hostEnt = gethostbyname( authData.BOSserver );
    if ( !hostEnt )
    {
      errorCode = MRM_BOS_GET_BOS_IP;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
       MPFROMP( theRecord ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      return;
    }
    memcpy( &oscarAddr.s_addr, hostEnt->h_addr, hostEnt->h_length );
  }
  
  debugf( "Connecting to BOS server.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 6, 20 ),
   MPFROMP( "Connecting to Basic Oscar Service (BOS) server..." ) );
  
  memset( &serverDesc, 0, sizeof( struct sockaddr_in ) );
  // serverDesc.sin_len = sizeof( struct sockaddr_in );
  serverDesc.sin_family = AF_INET;
  serverDesc.sin_addr.s_addr = oscarAddr.s_addr;
  if ( tmpStr )
  {
    serverDesc.sin_port = SWAP2( atol( tmpStr+1 ) );
  } else {
    serverDesc.sin_port = SWAP2( 5190 ); // Standard OSCAR port
  }
  
  rc = connect( BOSsocket, (struct sockaddr *)&serverDesc,
   sizeof( struct sockaddr_in ) );
   
  if ( rc )
  {
    errorCode = MRM_BOS_SOCKET_CONNECT;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( mgrWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( theRecord ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  SET_NEXT_STATE( BOSconnectionAck );
  
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 7, 20 ),
   MPFROMP( "Signing off from Oscar login server..." ) );
  
  sendData.prepareSignoffData();
  sendData.sendData( oscarSocket );
  
  rc = soclose( oscarSocket );
  oscarSocket = 0;
  if ( rc )
  {
    errorCode = MRM_OSC_SOCKET_CLOSE;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    // Not really a fatal error, so keep going.
  }
  
  DosPostEventSem( continuousReceive );
  // Allow us to receive data again
  
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_ACK_TIMEOUT );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareAuthorizationData( &authData );
  debugf( "Sending authorization information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 8, 20 ),
   MPFROMP( "Sending authorization cookie..." ) );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOShostReady );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_LOGIN_TIMEOUT );
  ADVANCE_STATE_MACHINE;

  sendData.prepareVersionsRequest();
  debugf( "Requesting versions of available Oscar services.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 9, 20 ),
   MPFROMP( "Querying service information..." ) );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSservicesVersion );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_SERV_VER_TIMEOUT );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareReqForSelfInformation();
  debugf( "Requesting user information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 10, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOScurrentUserInfo );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  userInfo.printData();
  
  sendData.prepareRateInfoRequest();
  debugf( "Requesting rate information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 11, 20 ), NULL );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSrateInformation );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  if ( rateInformation )
  {
    sendData.prepareRateInformationAck( rateInformation );
    debugf( "Acknowledging rate information.\n" );
    sendData.sendData( BOSsocket );
  }
  
  sendData.prepareCapabilitiesInfo( settings->profile );
  debugf( "Sending capabilties information.\n" );
  sendData.sendData( BOSsocket );
  
  sendData.prepareRequestSSIlimits();
  debugf( "Requesting server-stored information limitations.\n" );
  sendData.sendData( BOSsocket );
  
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 12, 20 ), NULL );
  
  SET_NEXT_STATE( BOSserverStoredLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;

  sendData.prepareReqForSSI();
  debugf( "Requesting server-stored information (SSI).\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 13, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSserverStoredInfo );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareReqLocationLimits();
  debugf( "Requesting location service limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 14, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSlocationLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  ssiData.printData();
  
  buddyCreateData.theSession = this;
  buddyCreateData.userInfo = &userInfo;
  DosCreateEventSem( NULL, &buddyCreateData.wakeupSem, 0, 0 );
  
  buddyCreateData.buddyListWin = NULLHANDLE;
  // Will get populated after event sem is posted
  
  WinPostMsg( mgrWin, WM_CREATEBUDDYLIST, MPFROMP( theRecord ),
   MPFROMP( &buddyCreateData ) );
   
  DosWaitEventSem( buddyCreateData.wakeupSem, SEM_INDEFINITE_WAIT );
  // Buddy list window handle is now valid
  
  sessionWindow = buddyCreateData.buddyListWin;
  
  numBuddies = ssiData.numRootBuddies;
  
  if ( numBuddies )
  {
    CHECKED_CALLOC( numBuddies * sizeof( Buddy * ), buddyList );
  } else {
    buddyList = NULL;
  }
  
  for ( i=0; i<numBuddies; ++i )
  {
    ssiData.rootBuddies[i].beenHere = 0;
  }
  
  for ( i=0; i<numBuddies; ++i )
  {
    if ( ssiData.rootBuddies[i].numMembers &&
          !ssiData.rootBuddies[i].beenHere )
    {
      found = 0;
      
      for ( j=0; j<numBuddies; ++j )
      {
        if ( !ssiData.rootBuddies[j].beenHere && j!=i &&
              ssiData.rootBuddies[j].numMembers )
        {
          for ( k=0; k<ssiData.rootBuddies[j].numMembers; ++k )
          {
            if ( ssiData.rootBuddies[j].memberIDs[k] ==
                  ssiData.rootBuddies[i].id &&
                  ssiData.rootBuddies[i].gid == ssiData.rootBuddies[j].gid )
            {
              found = 1;
            }
          }
        }
      }
      
      if ( !found )
      {
        // Current highest level group
        ssiData.rootBuddies[i].parentRecord = NULL;
        ssiData.rootBuddies[i].beenHere = 1;
        
        buddyList[i] = new Buddy( ssiData.rootBuddies + i );
        
        DosResetEventSem( buddyCreateData.wakeupSem, (ULONG *) &j );
        WinPostMsg( sessionWindow, WM_ADDBUDDY,
         MPFROMP( ssiData.rootBuddies + i ),
         MPFROMLONG( buddyCreateData.wakeupSem ) );
        DosWaitEventSem( buddyCreateData.wakeupSem, SEM_INDEFINITE_WAIT );
        
        addAllGroupMembers( i, buddyList, &ssiData, sessionWindow,
         buddyCreateData.wakeupSem );
      }
    }
  }
  
  for ( i=0; i<numBuddies; ++i )
  {
    if ( !ssiData.rootBuddies[i].beenHere )
    {
      debugf( "Orphan Member: %s\n", ssiData.rootBuddies[i].entryName );
      ssiData.rootBuddies[i].parentRecord = NULL;
      buddyList[i] = new Buddy( ssiData.rootBuddies + i );

      DosResetEventSem( buddyCreateData.wakeupSem, (ULONG *) &j );
      WinPostMsg( sessionWindow, WM_ADDBUDDY,
       MPFROMP( ssiData.rootBuddies + i ),
       MPFROMLONG( buddyCreateData.wakeupSem ) );
      DosWaitEventSem( buddyCreateData.wakeupSem, SEM_INDEFINITE_WAIT );
      
      addAllGroupMembers( i, buddyList, &ssiData, sessionWindow,
       buddyCreateData.wakeupSem );
    } else {
      ssiData.rootBuddies[i].beenHere = 0;
      // Reset the status to be neat about it
    }
  }
  
  DosCloseEventSem( buddyCreateData.wakeupSem );
  
  sendData.prepareReqBuddyManagementLimits();
  debugf( "Requesting buddy list management limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 15, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSbuddyManagementLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareRequestPrivacyParams();
  debugf( "Requesting privacy management limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 16, 20 ), NULL );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSprivacyLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareRequestICBMParams();
  debugf( "Requesting ICBM parameter information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 17, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSICBMparams );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareSSIActivation();
  debugf( "Activating server-stored profile.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 18, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  sendData.prepareSetStatus();
  debugf( "Setting status to on-line.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 19, 20 ),
   MPFROMP( "Setting status to on-line.\n" ) );
  sendData.sendData( BOSsocket );

  status = BOSserviceLoop;
  DosPostEventSem( continuousReceive );
  // All received data should be asynchronous from now on...
  
  sendData.prepareSetICBMParams();
  debugf( "Setting ICBM parameters for IMs.\n" );
  sendData.sendData( BOSsocket );

  sendData.prepareClientReady( supportedFamilies );
  debugf( "Sending the client ready signal.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 20, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  CHECKED_MALLOC( 17 + strlen( userInfo.screenName ), tmpStr );
  sprintf( tmpStr, "\\QUEUES\\%s\\Rate", userInfo.screenName );
  j = strlen( tmpStr );
  
  for ( i=0; rateInformation[i].rateClass; ++i )
  {
    // Create rate regulation threads and queues
    if ( rateInformation[i].numAppliesTo )
    {
      rateInformation[i].sendSocket = BOSsocket;
      rateInformation[i].pauseResume = pauseResume;
      ltoa( i, tmpStr + j, 10 );
      if ( DosCreateQueue( &rateInformation[i].outgoingData,
            QUE_FIFO | QUE_PRIORITY, tmpStr ) )
      {
        debugf( "Failed to create rate regulation queue.  Cannot service session.\n" );
        status = fatalError;
        CHECKED_FREE( tmpStr );
        WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
        return;
      }
      rateInformation[i].rateThread =
       _beginthread( (void (*) (void *))rateThread, NULL, 16384,
        rateInformation + i );
    }
  }
  
  CHECKED_FREE( tmpStr );
  
  WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
}

static void addAllGroupMembers( int entry, Buddy **buddyList,
 SSIData *ssiData, HWND sessionWindow, HEV wakeupSem )
{
  int i, j, junk;
  
  for ( i=0; i < ssiData->rootBuddies[entry].numMembers; ++i )
  {
    for ( j=0; j < ssiData->numRootBuddies; ++j )
    {
      if ( ssiData->rootBuddies[j].id ==
            ssiData->rootBuddies[entry].memberIDs[i] &&
           ssiData->rootBuddies[j].gid == ssiData->rootBuddies[entry].gid )
      {
        ssiData->rootBuddies[j].parentRecord =
         ssiData->rootBuddies[entry].myRecord;
         
        ssiData->rootBuddies[j].beenHere = 1;
        
        buddyList[j] = new Buddy( ssiData->rootBuddies + j );
        
        DosResetEventSem( wakeupSem, (ULONG *) &junk );
        WinPostMsg( sessionWindow, WM_ADDBUDDY,
         MPFROMP( ssiData->rootBuddies + j ), MPFROMLONG( wakeupSem ) );
        DosWaitEventSem( wakeupSem, SEM_INDEFINITE_WAIT );
        
        if ( ssiData->rootBuddies[j].numMembers )
        {
          // Recurse until we hit the leaf nodes
          addAllGroupMembers( j, buddyList, ssiData, sessionWindow, wakeupSem );
        }
      }
    }
  }
}

Buddy *OscarSession :: createBuddy( buddyListEntry *buddyInfo,
 int isAutomatic )
{
  // Note: Assumes that you are calling it from a message queue thread!
  int i, found;
  
  DosRequestMutexSem( buddyListAccessMux, SEM_INDEFINITE_WAIT );
  found = -1;
  for ( i=0; i<numBuddies; ++i )
  {
    if ( buddyList[i] == NULL )
    {
      found = i;
    } else if ( screenNamesEqual( buddyList[i]->userData.screenName,
                 buddyInfo->entryName ) )
    {
      // Matches an existing buddy in the list
      
      debugf( "Attempted to add buddy to buddy list (%s) again.\n",
       buddyInfo->entryName );
       
      if ( !isAutomatic )
      {
        eventData theEventData;
        sessionThreadSettings *globalSettings = (sessionThreadSettings *)
         WinQueryWindowPtr( sessionManagerWin, 60 );
        
        theEventData.currentUser = myUserName;
        theEventData.otherUser = buddyInfo->entryName;
        theEventData.message = "DUPLICATE USER NOT ADDED";
        
        handleMrMessageEvent( EVENT_ERRORBOX, settings, globalSettings,
         &theEventData, 0 );
        WinMessageBox( HWND_DESKTOP, sessionWindow,
         "You attempted to add a buddy that is already in your buddy list.  The buddy entry will not be duplicated.",
         "Cannot add a duplicate buddy", 999,
         MB_CANCEL | MB_APPLMODAL | MB_MOVEABLE );
      }
      
      DosReleaseMutexSem( buddyListAccessMux );
      return NULL;
    }
  }
  
  if ( found == -1 )
  {
    numBuddies++;
    if ( buddyList )
    {
      CHECKED_REALLOC( buddyList, sizeof( Buddy * ) * numBuddies,
       buddyList );
      found = numBuddies - 1;
    } else {
      CHECKED_MALLOC( sizeof( Buddy * ) * numBuddies, buddyList );
      found = 0;
    }
  }
  
  buddyList[found] = new Buddy( buddyInfo );
  
  WinSendMsg( sessionWindow, WM_ADDBUDDY, MPFROMP( buddyInfo ),
   MPFROMLONG( 0 ) );
  
  DosReleaseMutexSem( buddyListAccessMux );
  return buddyList[found];
}

void OscarSession :: serviceSessionUntilClosed( void )
{
  ULONG junk;
  
  if ( status == fatalError )
    return;
  // Nothing to service.  We couldn't start up.
  
  debugf( "Session was established successfully.\n" );
  reconnect = 0;
  
  reconnected:
  
  DosCreateEventSem( NULL, &sessionShutdown, 0, 0 );
  WinPostMsg( sessionWindow, WM_REGISTERWAKEUPSEM,
   MPFROMLONG( sessionShutdown ), NULL );
  
  if ( !reconnect )
  {
    WinPostMsg( sessionWindow, WM_SESSIONPOSTINIT, NULL, NULL );
  } else {
    reconnect = 0;
  }
  
  DosWaitEventSem( sessionShutdown, SEM_INDEFINITE_WAIT );
  debugf( "Session thread %d awoke from its slumber.\n", *_threadid );
  
  if ( reconnect )
  {
    DosResetEventSem( sessionShutdown, &junk );
    warmBoot( reconnectIP, reconnectCookie, reconnectCookieLen );
    goto reconnected;
  }
}

void OscarSession :: closeSession( void )
{
  HWND tmpWin = sessionWindow;
  sessionWindow = 0;
  debugf( "A forceful close of this session was initiated.  Closing window.\n" );
  WinSendMsg( tmpWin, WM_CLOSE, NULL, NULL );
}

void OscarSession :: windowClosed( void )
{
  sessionWindow = 0;
  // Make sure we don't try to close the window again when we destroy this
  //  object as it has already been closed by the user.
}

void OscarSession :: warmBoot( char *ipAddr, unsigned char *cookie,
 unsigned short cookieLen )
{
  // Migrate over to a new server while keeping connection up
  char *tmpStr;
  struct hostent *hostEnt;
  struct in_addr oscarAddr;
  struct sockaddr_in serverDesc;
  HEV theSem;
  HWND progressDlg;
  ULONG rc;
  int i, j;
  
  status = shutDown;
  
  receiveData.shutdown();
  
  if ( continuousReceive )
  {
    DosPostEventSem( continuousReceive );
  }
  
  if ( pauseResume )
  {
    DosPostEventSem( pauseResume );
  }
  
  if ( BOSsocket )
  {
    debugf( "Closing connection to old BOS server.\n" );
    soclose( BOSsocket );
  }
  
  endReceiverThread();
  clearServiceInfo();
  receiveData.reset();
  
  DosCreateEventSem( NULL, &theSem, 0, FALSE );
  WinPostMsg( sessionWindow, WM_REBOOTSESSION, MPFROMLONG( theSem ),
   MPFROMP( &progressDlg ) );
  DosWaitEventSem( theSem, SEM_INDEFINITE_WAIT );
  DosCloseEventSem( theSem );
  
  if ( !ipAddr )
  {
    // No proper migration message came through, so start the connection
    //  from scratch with a new cookie.
    debugf( "Creating socket.\n" );
    
    status = noSession;
    authData.clear();
    
    oscarSocket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( oscarSocket < 0 )
    {
      errorCode = MRM_OSC_SOCKET_OPEN;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
       MPFROMP( myRecordInManager ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      return;
    }
    
    debugf( "Looking up Oscar server host IP address.\n" );
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 0, 20 ),
     MPFROMP( "Looking up Oscar server..." ) );

    if ( !startServer )
    {
      hostEnt = gethostbyname( "login.oscar.aol.com" );
      if ( !hostEnt )
      {
        errorCode = MRM_OSC_GET_OSC_IP;
        status = fatalError;
        debugf( "%s\n", errorMessageStrings[ errorCode ] );
        WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE,
         MPFROMSHORT( errorCode ), MPFROMP( myRecordInManager ) );
        WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
        return;
      }
    } else {
      oscarAddr.s_addr = inet_addr( startServer );
      if ( oscarAddr.s_addr == 0xffffffff )
      {
        // 16-bit stack call to gethostbyname for a dotted decimal host name
        //  returns NULL.  Check with inet_addr for this condition first.
        
        hostEnt = gethostbyname( startServer );
        if ( !hostEnt )
        {
          errorCode = MRM_OSC_GET_OSC_IP;
          status = fatalError;
          debugf( "%s\n", errorMessageStrings[ errorCode ] );
          WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE,
           MPFROMSHORT( errorCode ), MPFROMP( myRecordInManager ) );
          WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
          return;
        }
        memcpy( &oscarAddr.s_addr, hostEnt->h_addr, hostEnt->h_length );
      }
    }
    
    memset( &serverDesc, 0, sizeof( struct sockaddr_in ) );
     
    // serverDesc.sin_len = sizeof( struct sockaddr_in );
    serverDesc.sin_family = AF_INET;
    serverDesc.sin_addr.s_addr = oscarAddr.s_addr;
    if ( !startPort )
    {
      serverDesc.sin_port = SWAP2( 5190 ); // Standard OSCAR port
    } else {
      serverDesc.sin_port = SWAP2( startPort );
    }
    
    debugf( "Connecting socket to Oscar server.\n" );
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 1, 20 ),
     MPFROMP( "Connecting to Oscar server..." ) );
    
    rc = connect( oscarSocket, (struct sockaddr *)&serverDesc,
     sizeof( struct sockaddr_in ) );
    
    if ( rc )
    {
      errorCode = MRM_OSC_SOCKET_CONNECT;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
       MPFROMP( myRecordInManager ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      return;
    }
    
    SET_NEXT_STATE( oscarConnectionAck );
    
    dataReceiverThreadID = _beginthread( (void (*) (void *))dataReceiverThread,
     NULL, 65536, this );
    
    WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_ACK_TIMEOUT );
    
    // Got our ACK within the timeout period
    
    ADVANCE_STATE_MACHINE;
    
    sendData.prepareConnectionAck();
    debugf( "Sending connection acknowledgement to server.\n" );
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 2, 20 ),
     MPFROMP( "Sending connection acknowledgement..." ) );
    sendData.sendData( oscarSocket );
    
    sendData.prepareAuthKeyRequest( myUserName );
    debugf( "Requesting MD5 authorization key from server.\n" );
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 3, 20 ),
     MPFROMP( "Requesting MD5 authorization key..." ) );
    sendData.sendData( oscarSocket );
    
    SET_NEXT_STATE( oscarMD5authKey );
    WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_AUTH_TIMEOUT );
    ADVANCE_STATE_MACHINE;
    
    if ( !authData.errorCode )
    {
      // We could get a "service temporarily unavailable" message
      //  in response to requesting an MD5 key.
      
      debugf( "Authorization key was received.\n" );
      
      sendData.prepareLoginData( myUserName, myPassword, &authData );
      debugf( "Sending login packet to Oscar server.\n" );
      WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 4, 20 ),
       MPFROMP( "Sending encrypted login..." ) );
      sendData.sendData( oscarSocket );
      
      debugf( "Waiting for authorization response.\n" );
      
      SET_NEXT_STATE( oscarLoginAuthorization );
      WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_OSC_AUTH_TIMEOUT );
      ADVANCE_STATE_MACHINE;
      
      authData.printData();
    }
    
    if ( authData.errorCode )
    {
      USHORT theWindowError;
      
      errorCode = MRM_OSC_AUTH_ERROR;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      
      switch ( authData.errorCode )
      {
        case 1:
        case 4:
          theWindowError = MRM_AIM_BAD_IDPASS;
        break;
        case 5:
          theWindowError = MRM_AIM_BAD_PASSWORD;
        break;
        case 6:
          theWindowError = MRM_AIM_AUTH_FAILED;
        break;
        case 8:
          theWindowError = MRM_AIM_ACCOUNT_DELETED;
        break;
        case 2:
        case 12:
        case 13:
        case 18:
        case 19:
        case 20:
        case 21:
        case 26:
        case 31:
          theWindowError = MRM_AIM_SERVICE_TEMP_UNAVAILABLE;
        break;
        case 17:
          theWindowError = MRM_AIM_ACCOUNT_SUSPENDED;
        break;
        case 24:
        case 29:
          theWindowError = MRM_AIM_RATE_LIMIT_HIT;
        break;
        case 27:
        case 28:
          theWindowError = MRM_AIM_FORCE_UPGRADE;
        break;
        case 32:
          theWindowError = MRM_AIM_INVALID_SECUREID;
        break;
        case 34:
          theWindowError = MRM_AIM_ACCOUNT_SUSPENDED_MINOR;
        break;
        default:
          theWindowError = MRM_AIM_UNKNOWN_LOGIN_FAILURE;
      }
      
      debugf( "The specific problem was:\n" );
      debugf( "%s\n", errorMessageStrings[ theWindowError ] );
      
      if ( authData.errorURL )
      {
        debugf( "The server offers the following description of the message:\n" );
        debugf( "%s\n", authData.errorURL );
      }
      
      WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( theWindowError ),
       MPFROMP( myRecordInManager ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      
      return;
    }
    
    WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 5, 20 ),
     MPFROMP( "Signing off from Oscar login server..." ) );
    
    sendData.prepareSignoffData();
    sendData.sendData( oscarSocket );
    
    rc = soclose( oscarSocket );
    oscarSocket = 0;
    if ( rc )
    {
      errorCode = MRM_OSC_SOCKET_CLOSE;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      // Not really a fatal error, so keep going.
    }
    
    DosPostEventSem( continuousReceive );
    // Allow us to receive data again
  } else {
    if ( authData.BOSserver ) CHECKED_FREE( authData.BOSserver );
    if ( authData.authCookie ) CHECKED_FREE( authData.authCookie );
    
    authData.authCookieLength = cookieLen;
    CHECKED_MALLOC( cookieLen, authData.authCookie );
    memcpy( authData.authCookie, cookie, cookieLen );
    CHECKED_STRDUP( ipAddr, authData.BOSserver );
  }
  
  debugf( "Initiating connection to new BOS server (%s).\n",
   authData.BOSserver );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 6, 20 ),
   MPFROMP( "Looking up Basic Oscar Service (BOS) server..." ) );
  
  BOSsocket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
  
  if ( BOSsocket < 0 )
  {
    errorCode = MRM_BOS_SOCKET_OPEN;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( myRecordInManager ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  tmpStr = strrchr( authData.BOSserver, ':' );
  if ( tmpStr ) *tmpStr = 0;
  // The port number is usually returned, but not for old servers
  
  oscarAddr.s_addr = inet_addr( authData.BOSserver );
  if ( oscarAddr.s_addr == 0xffffffff )
  {
    // 16-bit stack call to gethostbyname for a dotted decimal host name
    //  returns NULL.  Check with inet_addr for this condition first.
    
    hostEnt = gethostbyname( authData.BOSserver );
    if ( !hostEnt )
    {
      errorCode = MRM_BOS_GET_BOS_IP;
      status = fatalError;
      debugf( "%s\n", errorMessageStrings[ errorCode ] );
      WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
       MPFROMP( myRecordInManager ) );
      WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
      return;
    }
    memcpy( &oscarAddr.s_addr, hostEnt->h_addr, hostEnt->h_length );
  }
  
  debugf( "Connecting to BOS server.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 7, 20 ),
   MPFROMP( "Connecting to Basic Oscar Service (BOS) server..." ) );
  
  memset( &serverDesc, 0, sizeof( struct sockaddr_in ) );
  // serverDesc.sin_len = sizeof( struct sockaddr_in );
  serverDesc.sin_family = AF_INET;
  serverDesc.sin_addr.s_addr = oscarAddr.s_addr;
  
  if ( tmpStr )
  {
    serverDesc.sin_port = SWAP2( atol( tmpStr+1 ) );
  } else {
    serverDesc.sin_port = SWAP2( 5190 ); // Standard OSCAR port
  }
  
  status = oscarLoginAuthorization;

  rc = connect( BOSsocket, (struct sockaddr *)&serverDesc,
   sizeof( struct sockaddr_in ) );
   
  if ( rc )
  {
    errorCode = MRM_BOS_SOCKET_CONNECT;
    status = fatalError;
    debugf( "%s\n", errorMessageStrings[ errorCode ] );
    WinPostMsg( sessionManagerWin, WM_ERRORMESSAGE, MPFROMSHORT( errorCode ),
     MPFROMP( myRecordInManager ) );
    WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
    return;
  }
  
  if ( !dataReceiverThreadID )
  {
    dataReceiverThreadID = _beginthread( (void (*) (void *))dataReceiverThread,
     NULL, 65536, this );
  }
  
  SET_NEXT_STATE( BOSconnectionAck );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_ACK_TIMEOUT );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareAuthorizationData( &authData );
  debugf( "Sending authorization information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 8, 20 ),
   MPFROMP( "Sending authorization cookie..." ) );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOShostReady );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_LOGIN_TIMEOUT );
  ADVANCE_STATE_MACHINE;

  sendData.prepareVersionsRequest();
  debugf( "Requesting versions of available Oscar services.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 9, 20 ),
   MPFROMP( "Querying service information..." ) );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSservicesVersion );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_BOS_SERV_VER_TIMEOUT );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareReqForSelfInformation();
  debugf( "Requesting user information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 10, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOScurrentUserInfo );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  userInfo.printData();
  
  sendData.prepareRateInfoRequest();
  debugf( "Requesting rate information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 11, 20 ), NULL );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSrateInformation );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  if ( rateInformation )
  {
    sendData.prepareRateInformationAck( rateInformation );
    debugf( "Acknowledging rate information.\n" );
    sendData.sendData( BOSsocket );
  }
  
  sendData.prepareCapabilitiesInfo( settings->profile );
  debugf( "Sending capabilties information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 12, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  sendData.prepareRequestSSIlimits();
  debugf( "Requesting server-stored information limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 13, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSserverStoredLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;

  status = BOSserverStoredInfo;
  // Don't need to query or re-create the buddy list again
  
  sendData.prepareReqLocationLimits();
  debugf( "Requesting location service limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 14, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSlocationLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareReqBuddyManagementLimits();
  debugf( "Requesting buddy list management limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 15, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSbuddyManagementLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareRequestPrivacyParams();
  debugf( "Requesting privacy management limitations.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 16, 20 ), NULL );
  sendData.sendData( BOSsocket );

  SET_NEXT_STATE( BOSprivacyLimits );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareRequestICBMParams();
  debugf( "Requesting ICBM parameter information.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 17, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  SET_NEXT_STATE( BOSICBMparams );
  WAIT_CHECK_FOR_ERRORS( RESPONSE_TIMEOUT, MRM_GEN_WAIT_SEM );
  ADVANCE_STATE_MACHINE;
  
  sendData.prepareSSIActivation();
  debugf( "Activating server-stored profile.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 18, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  sendData.prepareSetStatus();
  debugf( "Setting status to on-line.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 19, 20 ),
   MPFROMP( "Setting status to on-line.\n" ) );
  sendData.sendData( BOSsocket );

  status = BOSserviceLoop;
  DosPostEventSem( continuousReceive );
  // All received data should be asynchronous from now on...
  
  sendData.prepareSetICBMParams();
  debugf( "Setting ICBM parameters for IMs.\n" );
  sendData.sendData( BOSsocket );

  sendData.prepareClientReady( supportedFamilies );
  debugf( "Sending the client ready signal.\n" );
  WinPostMsg( progressDlg, WM_PROGRESSREPORT, MPFROM2SHORT( 20, 20 ), NULL );
  sendData.sendData( BOSsocket );
  
  CHECKED_MALLOC( 17 + strlen( userInfo.screenName ), tmpStr );
  sprintf( tmpStr, "\\QUEUES\\%s\\Rate", userInfo.screenName );
  j = strlen( tmpStr );
  
  for ( i=0; rateInformation[i].rateClass; ++i )
  {
    // Create rate regulation threads and queues
    if ( rateInformation[i].numAppliesTo )
    {
      rateInformation[i].sendSocket = BOSsocket;
      rateInformation[i].pauseResume = pauseResume;
      ltoa( i, tmpStr + j, 10 );
      if ( DosCreateQueue( &rateInformation[i].outgoingData,
            QUE_FIFO | QUE_PRIORITY, tmpStr ) )
      {
        debugf( "Failed to create rate regulation queue.  Cannot service session.\n" );
        status = fatalError;
        CHECKED_FREE( tmpStr );
        WinPostMsg( progressDlg, WM_CLOSE, NULL, NULL );
        return;
      }
      rateInformation[i].rateThread =
       _beginthread( (void (*) (void *))rateThread, NULL, 16384,
        rateInformation + i );
    }
  }
  
  CHECKED_FREE( tmpStr );
  WinPostMsg( sessionWindow, WM_SESSIONUP, MPFROMLONG( progressDlg ), NULL );
  
  debugf( "Warm-booting the session has finished.\n" );
}

void OscarSession :: endReceiverThread( void )
{
  ULONG receiverThread = dataReceiverThreadID;
  
  if ( receiverThread )
  {
    int rc, timePolled = 0;
    debugf( "Receiver thread %d is still active.  Waiting for it to end.\n",
     receiverThread );
    
    DosResumeThread( receiverThread );
    // Just in case it was suspended
    
    rc = DosWaitThread( &receiverThread, DCWW_NOWAIT );
    
    while ( dataReceiverThreadID && timePolled < 5 &&
             rc == ERROR_THREAD_NOT_TERMINATED )
    {
      DosSleep( 1000 );
      timePolled++;
      rc = DosWaitThread( &receiverThread, DCWW_NOWAIT );
    }
    
    if ( rc == ERROR_THREAD_NOT_TERMINATED && dataReceiverThreadID )
    {
      debugf( "Timed out waiting for thread to end.  Killing thread %d.\n",
       receiverThread );
      DosKillThread( receiverThread );
      // Somehow we stayed blocked somewhere in the receiver thread even
      //  though the sockets were closed.  Should never really happen, but
      //  just in case, this will get us out of a tight spot.
    } else {
      debugf( "Receiver thread %d ended nicely.\n", receiverThread );
    }
  }
}

void OscarSession :: clearServiceInfo( void )
{
  if ( supportedFamilies ) CHECKED_FREE( supportedFamilies );
  
  supportedFamilies = NULL;
  
  if ( rateInformation )
  {
    REQUESTDATA rd;
    PVOID crap;
    BYTE crap2;
    ULONG len;
    int i = 0;
    OscarData *queuedData;
    
    while ( rateInformation[i].rateClass )
    {
      if ( rateInformation[i].numAppliesTo )
      {
        rateInformation[i].sendSocket = 0;
        // Fast-forward through any pending data by not sending it
        
        DosPostEventSem( pauseResume );
        
        DosWriteQueue( rateInformation[i].outgoingData, 0, 0, 0, 0 );
        // Unblock if we're waiting for data
        
        DosWaitThread( &rateInformation[i].rateThread, DCWW_WAIT );
        // Thread should be shutting down or done shutting down after
        //  writing a 0 to the queue.
        
        while ( DosReadQueue( rateInformation[i].outgoingData, &rd, &len, &crap,
                 0, DCWW_NOWAIT, &crap2, NULLHANDLE ) == 0 )
        {
          queuedData = (OscarData *)rd.ulData;
          delete queuedData;
          // Free any remaining data in the queue
        }
        
        DosCloseQueue( rateInformation[i].outgoingData );
        
        CHECKED_FREE( rateInformation[i].appliesToFam );
        CHECKED_FREE( rateInformation[i].appliesToSub );
      }
      ++i;
    }
    CHECKED_FREE( rateInformation );
    rateInformation = NULL;
  }
}

OscarSession :: ~OscarSession()
{
  status = shutDown;
  
  if ( sessionWindow )
  {
    debugf( "Closing up session's buddy list window.\n" );
    WinPostMsg( sessionWindow, WM_CLOSE, NULL, NULL );
  }
  
  receiveData.shutdown();
  
  if ( continuousReceive )
  {
    DosPostEventSem( continuousReceive );
  }
  
  if ( pauseResume )
  {
    DosPostEventSem( pauseResume );
  }
  
  if ( BOSsocket )
  {
    debugf( "Cleanup shutting down BOS socket.\n" );
    soclose( BOSsocket );
  }
  
  if ( oscarSocket )
  {
    debugf( "Cleanup shutting down Oscar socket.\n" );
    soclose( oscarSocket );
  }
  
  // Hopefully closing the sockets will wake up the thread waiting to receive
  //  from them.
  
  endReceiverThread();

  if ( stateWakeup )
  {
    DosCloseEventSem( stateWakeup );
  }
  
  if ( continuousReceive )
  {
    DosCloseEventSem( continuousReceive );
  }
  
  if ( waitStateMux )
  {
    DosCloseMutexSem( waitStateMux );
  }
  
  if ( buddyListAccessMux )
  {
    DosCloseMutexSem( buddyListAccessMux );
  }
  
  clearServiceInfo();
  
  if ( pauseResume )
  {
    DosCloseEventSem( pauseResume );
  }
  
  if ( sessionShutdown ) DosCloseEventSem( sessionShutdown );
  
  if ( numBuddies )
  {
    int i;
    for ( i=0; i<numBuddies; ++i )
    {
      if ( buddyList[i] )
      {
        delete buddyList[i];
        buddyList[i] = NULL;
      }
    }
    CHECKED_FREE( buddyList );
  }
  
  if ( reconnectIP ) CHECKED_FREE( reconnectIP );
  if ( reconnectCookie ) CHECKED_FREE( reconnectCookie );
  
  debugf( "Session has ended.\n" );
}

void OscarSession :: reportAvailableServices( void )
{
  unsigned short i=0;
  
  debugf( "\n" );
  debugf( "Available IM services include:\n" );
  debugf( "--------------------------------------\n" );  
  
  while ( supportedFamilies[i] )
  {
    switch ( supportedFamilies[i] )
    {
      case 1:  debugf( "Generic service controls   [Family 1]\n" );  break;
      case 2:  debugf( "Location services          [Family 2]\n" );  break;
      case 3:  debugf( "Buddy List management      [Family 3]\n" );  break;
      case 4:  debugf( "Messaging (ICBM) service   [Family 4]\n" );  break;
      case 5:  debugf( "Advertisements 'service'   [Family 5]\n" );  break;
      case 6:  debugf( "Invitation service         [Family 6]\n" );  break;
      case 7:  debugf( "Administration service     [Family 7]\n" );  break;
      case 8:  debugf( "Popup notices service      [Family 8]\n" );  break;
      case 9:  debugf( "Privacy management         [Family 9]\n" );  break;
      case 10: debugf( "User lookup service        [Family 10]\n" ); break;
      case 11: debugf( "Usage stats service        [Family 11]\n" ); break;
      case 12: debugf( "Translation service        [Family 12]\n" ); break;
      case 13: debugf( "Chat navigation service    [Family 13]\n" ); break;
      case 14: debugf( "Chat service               [Family 14]\n" ); break;
      case 15: debugf( "Directory user search      [Family 15]\n" ); break;
      case 16: debugf( "Buddy icon service         [Family 16]\n" ); break;
      case 19: debugf( "Server-stored information  [Family 19]\n" ); break;
      case 21: debugf( "ICQ extensions             [Family 21]\n" ); break;
      case 23: debugf( "Authorization/registration [Family 23]\n" ); break;
      default:
        debugf( "Unknown service            [Family %d]\n",
         supportedFamilies[i] );
    }
    ++i;
  }
  debugf( "--------------------------------------\n" );
  debugf( "\n" );
}

void OscarSession :: queueForSend( OscarData *queueData,
 unsigned long priority )
{
  unsigned short family, subType;
  int i, j;
  
  queueData->setSeqNumPointer( &currentSequence );
  // Ensure that this is set correctly.
  
  queueData->getSNACtype( &family, &subType );
  if ( !family )
  {
    // No SNAC.  Just send it directly.
    queueData->sendData( BOSsocket );
    return;
  }
  
  debugf( "Enqueuing data for family %d, subtype %d.\n", family, subType );
  
  for ( i=0; rateInformation[i].rateClass; ++i )
  {
    for ( j=0; j<rateInformation[i].numAppliesTo; ++j )
    {
      if ( rateInformation[i].appliesToFam[j] == family &&
            rateInformation[i].appliesToSub[j] == subType )
      {
        DosWriteQueue( rateInformation[i].outgoingData, (ULONG)queueData,
         0, NULL, priority );
        return;
      }
    }
  }
  
  // If we got here, then we couldn't match the family/subtype with a rate
  //  class.  So just send the data in an unrestricted way.
  
  debugf( "Family/subtype is not regulated." );
  queueData->sendData( BOSsocket );
}

Buddy *OscarSession :: getBuddyFromName( char *buddyName ) const
{
  int i;
  
  DosRequestMutexSem( buddyListAccessMux, SEM_INDEFINITE_WAIT );
  
  for ( i=0; i<numBuddies; ++i )
  {
    if ( buddyList[i] == NULL ) continue;
    
    if ( screenNamesEqual( buddyList[i]->userData.screenName, buddyName ) )
    {
      DosReleaseMutexSem( buddyListAccessMux );
      return buddyList[i];
    }
  }
  
  DosReleaseMutexSem( buddyListAccessMux );
  return NULL;
}

