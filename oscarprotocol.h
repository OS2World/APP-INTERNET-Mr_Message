#ifndef OSCARPROTOCOL_H_INCLUDED
#define OSCARPROTOCOL_H_INCLUDED

#define INCL_WIN
#define INCL_DOS
#include <os2.h>
#include <time.h>

char *stripTags( char *origString );
// Remove HTML tags and leave plain text.
// Allocates a new string and returns it, so free it when done.

char *convertTagsToRTV( char *origString );
// Converts AOL-RTF text to "ACL"-RTF text
char *convertTextToAOL( char *origString );
// Converts raw formatted text to AOL-RTF
char *convertTextToRTV( char *origString );
// Converts raw formatted text to "ACL"-RTF (escapes <>'s)

class AuthResponseData
{
public:
  char *screenName;
  char *BOSserver;
  int authCookieLength;
  char *authCookie;
  char *emailAddress;
  int registrationStatus;
  char *passwordURL;
  char *authKey;
  int authKeyLen;
  
  int errorCode;
  char *errorURL;

  AuthResponseData();
  ~AuthResponseData();
  void clear( void );
  void printData( void );
};

class UserInformation
{
public:
  char *screenName;
  unsigned long warningLevel;
  unsigned long userClass, userStatus;
  time_t signupDate;
  time_t signonDate;
  time_t memberSinceDate;
  unsigned long idleMinutes;
  unsigned char ipAddress[4]; // w.x.y.z
  unsigned long sessionLen;
  unsigned char *buddyIconChecksum;
  unsigned char buddyIconChecksumLen;
  char *statusMessage;
  unsigned short statusMessageLen;
  char *statusMessageEncoding;
  unsigned short statusMessageEncodingLen;
  char *clientProfile;
  unsigned short clientProfileLen;
  char *clientProfileEncoding;
  unsigned short clientProfileEncodingLen;
  char *awayMessage;
  unsigned short awayMessageLen;
  char *awayMessageEncoding;
  unsigned short awayMessageEncodingLen;
  
  unsigned char isOnline, statusUninit;
  
  UserInformation();
  UserInformation( const UserInformation &copyMe );
  
  void clear( void );
  
  ~UserInformation();
  
  void incorporateData( UserInformation &copyMe );
  // Only copy updated fields
  
  void printData( void );
  // Print info to console
  
  char *getUserBlurbString( char *additionalInfo );
  // Get a summary string about this user for the GUI
};

typedef struct
{
  unsigned short rateClass;
  unsigned long windowSize, clearLevel, alertLevel, limitLevel,
   disconnectLevel, currentLevel, maxLevel, lastTime;
  unsigned char currentState;
  unsigned short numAppliesTo;
  unsigned short *appliesToFam;
  unsigned short *appliesToSub;
  unsigned long rateThread;
  HQUEUE outgoingData;
  ULONG sendSocket;
  HEV pauseResume;
  unsigned short slowDown;
} userRateInformation;

typedef struct
{
  char *entryName;
  int gid, id;
  int numMembers;
  int beenHere;
  unsigned short *memberIDs;
  RECORDCORE *parentRecord, *myRecord;
  void *theBuddyPtr;
} buddyListEntry;

#define RENDEVOUS_REQUEST 0
#define RENDEVOUS_CANCEL  1
#define RENDEVOUS_ACCEPT  2

#define RENDEVOUS_TYPE_VOID      0
#define RENDEVOUS_TYPE_CHAT      1
#define RENDEVOUS_TYPE_FILE_SEND 2
#define RENDEVOUS_TYPE_FILE_RECV 3

typedef struct
{
  unsigned short requestType, rendevousType;
  unsigned short chatExchange, chatInstance;
  char *chatroomName, *invitationText;
} rendevousInfo;

class SSIData
{
public:
  int numRootBuddies;
  buddyListEntry *rootBuddies;
  time_t dateStamp;

  SSIData();
  ~SSIData();
  void printData( void );
};

#endif

