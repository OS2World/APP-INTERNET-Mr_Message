#ifndef OSCARSESSION_H_INCLUDED
#define OSCARSESSION_H_INCLUDED

#include "oscardata.h"
#include "oscarprotocol.h"
#include "buddy.h"
#include "errors.h"

typedef enum
{
  EVENT_SPLASHSCREEN, EVENT_STARTSESSION, EVENT_ERRORBOX, EVENT_FIRSTCOMM,
  EVENT_RECEIVED, EVENT_SENT, EVENT_ARRIVE, EVENT_LEAVE, EVENT_ENDSESSION,
  EVENT_POST_RECEIVE, EVENT_PRE_SEND,
  EVENT_MAXEVENTS
} UI_EventType;

#define SET_SOUND_ENABLED      1
#define SET_REXX_ENABLED       2
#define SET_SHELL_ENABLED      4
// Applies to an entry in the settingFlags field of sessionThreadSettings

#define EVENT_PROCESSED_SOUND  1
#define EVENT_PROCESSED_SCRIPT 2
#define EVENT_PROCESSED_SHELL  4
// Returned mask from handleMrMessageEvent based on what was actually done

#define SET_TIMESTAMPS_ENABLED   1
#define SET_AUTOSTART            2
#define SET_AUTOMINIMIZE         4
#define SET_TYPING_NOTIFICATIONS 8
// Applies to the sessionFlags field of sessionThreadSettings

typedef enum
{
  noSession, oscarConnectionAck, oscarMD5authKey, oscarLoginAuthorization,
  BOSconnectionAck, BOShostReady, BOSservicesVersion, BOScurrentUserInfo,
  BOSrateInformation, BOSserverStoredLimits, BOSserverStoredInfo,
  BOSlocationLimits, BOSbuddyManagementLimits, BOSprivacyLimits,
  BOSICBMparams, BOSserviceLoop, shutDown, nextSocket, fatalError
} oscarSessionState;

typedef struct
{
  unsigned long sessionFlags;
  unsigned long settingFlags[ EVENT_MAXEVENTS ];
  char *sounds[ EVENT_MAXEVENTS ];
  char *rexxScripts[ EVENT_MAXEVENTS ];
  char *shellCmds[ EVENT_MAXEVENTS ];
  char *profile;
  char *awayMessage;
  unsigned char inheritSettings;
} sessionThreadSettings;

class OscarSession
{
protected:
  //
  // Network-related data
  //
  
  oscarSessionState status, waitForState;
  // status is where we are now
  // waitForStatus is the state we want to have the stateWakeup sem posted for
  int currentSequence;
  OscarData sendData, receiveData;
  int oscarSocket, BOSsocket;
  AuthResponseData authData;
  UserInformation userInfo;
  SSIData ssiData;
  char reconnect;
  char *reconnectIP;
  unsigned char *reconnectCookie;
  int reconnectCookieLen;
  char *startServer;
  int startPort;
  char *myUserName, *myPassword;
  
  //
  // GUI-related data
  //
  
  HWND sessionWindow, sessionManagerWin;
  Buddy **buddyList;
  int numBuddies;
  MINIRECORDCORE *myRecordInManager;
  
  //
  // Thread management
  //
  
  ULONG dataReceiverThreadID;
  HEV stateWakeup, continuousReceive, sessionShutdown, pauseResume;
  HMTX waitStateMux;
  
  //
  // Internal data
  //
  
  errorStatus errorCode;

public:
  HMTX buddyListAccessMux;
  unsigned short *supportedFamilies;
  userRateInformation *rateInformation;
  int numFlashing;
  sessionThreadSettings *settings;
  
  OscarSession( char *userName, char *password, char *server, int port,
   HWND mgrWin, MINIRECORDCORE *theRecord, void *info,
   int threadEntry, sessionThreadSettings *mySettings, HWND progressDlg );
  // Username and password must be allocated outside of this function and will
  //  be freed inside of this function.  Do not use these pointers in your
  //  code after returning from here.
  
  ~OscarSession();
  
  errorStatus getErrorStatus( void ) const;
  oscarSessionState getState( void ) const;
  
  void setState( oscarSessionState newState );
  
  void warmBoot( char *ipAddr, unsigned char *cookie, unsigned short cookieLen );
  void endReceiverThread( void );
  void clearServiceInfo( void );
  
  Buddy **getBuddyList( int *numBuds );
  HWND getBuddyListWindow( void ) const;
  HWND getSessionManagerWindow( void ) const;
  int *getSeqNumPointer( void );
  const char *getUserName( void ) const;
  const int getDataReceiverThreadID( void ) const;
  Buddy *getBuddyFromName( char *buddyName ) const;
  
  void queueForSend( OscarData *queueData, unsigned long priority );
  void serverPause( void );
  
  Buddy *createBuddy( buddyListEntry *buddyInfo, int isAutomatic );
  
  void serviceSessionUntilClosed( void );
  void closeSession( void );
  void windowClosed( void );
  
  void reportAvailableServices( void );
  
  friend void dataReceiverThread( OscarSession *sessionData );
  friend void rateRegulationThread( OscarSession *sessionData );
  // Allows the threads to touch my naughty-bits
};

void dataReceiverThread( OscarSession *sessionData );
// Handles all data received from Oscar/BOS server

void rateRegulationThread( OscarSession *sessionData );
// Controls the rate at which certain messages are sent to avoid flooding
//  the AOL servers

inline errorStatus OscarSession :: getErrorStatus( void ) const
{
  return errorCode;
}

inline oscarSessionState OscarSession :: getState( void ) const
{
  return status;
}

inline void OscarSession :: setState( oscarSessionState newState )
{
  status = newState;
}

inline Buddy **OscarSession :: getBuddyList( int *numBuds )
{
  *numBuds = numBuddies;
  return buddyList;
}

inline HWND OscarSession :: getBuddyListWindow( void ) const
{
  return sessionWindow;
}

inline HWND OscarSession :: getSessionManagerWindow( void ) const
{
  return sessionManagerWin;
}

inline int *OscarSession :: getSeqNumPointer( void )
{
  return &currentSequence;
}

inline const char *OscarSession :: getUserName( void ) const
{
  return userInfo.screenName;
}

inline const int OscarSession :: getDataReceiverThreadID( void ) const
{
  return dataReceiverThreadID;
}

#endif
