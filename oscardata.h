#ifndef OSCARDATA_H_INCLUDED
#define OSCARDATA_H_INCLUDED

#include <stdlib.h>
#include <stdio.h>
#include "oscarprotocol.h"

extern FILE *debugOutputStream;

#define STARTING_OSCARDATA_SIZE 64

#define HTML_WRAP    1
#define NO_HTML_WRAP 0

typedef struct
{
  unsigned char startByte;
  unsigned char channel;
  unsigned short sequence;
  unsigned short dataLen;
} FLAP;

typedef struct
{
  unsigned short family;
  unsigned short subType;
  char flags[2];
  unsigned long requestID;
} SNAC;

extern unsigned char *aim_encode_password( const char *password,
 int forceLength = 0 );

class OscarData
{
private:
  int length, position, shuttingDown, status;
  int *sequence;
  FLAP *ptr;
  
  void resizeAtLeast( int size );
  
public:
  OscarData();
  OscarData( int *sequenceNum );
  ~OscarData();
  
  void setSeqNumPointer( int *sequenceNum );
  
  void reset( void );
  void addFLAP( FLAP *theFLAP );
  void addFLAP( unsigned char channel );
  void addSNAC( SNAC *theSNAC );
  void addTLV( int type, int length, const void *value );
  void addTLV( int type, const char *value );
  void addData( const void *data, int dataLen );
  void tallyFLAPDataLen( void );
  void sendData( int socketHandle );
  void receiveData( int socketHandle );
  void printData( void );

  int parseTLV( unsigned char **TLVdata, unsigned short *tlvType );
  
  void shutdown( void );
  
  int getStatus( void );
  void getSNACtype( unsigned short *family, unsigned short *subType );

  // PREPARING DATA FOR SEND (oscarTx.cpp)
  // Main Oscar messages
  void prepareConnectionAck( void );
  void prepareAuthKeyRequest( char *userName );
  void prepareLoginData( char *userName, char *password,
        AuthResponseData *authData );
  void prepareSignoffData( void );
  
  // BOS server messages
  void prepareAuthorizationData( AuthResponseData *theData );
  void prepareVersionsRequest( void );
  void prepareReqForSelfInformation( void );
  void prepareRateInfoRequest( void );
  void prepareCapabilitiesInfo( char *profileMessage );
  void prepareRequestSSIlimits( void );
  void prepareReqForSSI( void );
  void prepareSSIActivation( void );
  void prepareClientReady( const unsigned short *supportedFamilies );
  void prepareRequestICBMParams( void );
  void prepareSetICBMParams( void );
  void prepareRateInformationAck( userRateInformation *rateInfo );
  void prepareSetStatus( void );
  void prepareReqLocationLimits( void );
  void prepareReqBuddyManagementLimits( void );
  void prepareRequestPrivacyParams( void );
  void prepareRequestUserInfo( char *screenName, unsigned short infoType );
  void prepareRequestICBMparams( void );
  void prepareServerPauseAck( void );
  
  void prepareSetIdle( int idleTime );
  void sendInstantMessage( const char *toUser, const char *message,
   int wrapIt = HTML_WRAP );
  void prepareAddClientSideBuddy( char *screenName );
  void prepareRemoveClientSideBuddy( char *screenName );
  void prepareSetClientProfile( char *profileMessage );
  void prepareSetClientAway( char *awayMessage );
  void prepareTypingNotification( const char *screenName, unsigned short type );
 
  // PROCESSING RECEIVED DATA (oscarRx.cpp)
  // Main Oscar messages
  int isConnectionAck( void );
  int getMD5AuthKey( AuthResponseData *authData );
  int getAuthResponseData( AuthResponseData *theData );
  
  // BOS server messages
  int isHostReadyMessage( unsigned short **supportedFamilies );
  int isFamilyVersions( void );
  int isMOTD( void );
  int isRateInformation( userRateInformation **rateInfo );
  int isSSIlimits( void );
  int isServerPause( void );
  int isServerResume( void );
  int isServerMigrate( char **IPaddress, unsigned char **cookie,
   unsigned short *cookieLen );
  int isServerRateWarning( void );
  int isTypingNotification( char **screenName );
  int isMessageAck( void );
  int isBuddyManagementLimits( void );
  int isPrivacyLimits( void );
  int isICBMparams( void );
  int getUserInformation( UserInformation *theInfo );
  int getSSIData( SSIData *theData );
  
  int instantMessageReceived( char **theUser, char **theMessage,
   rendevousInfo *extraInfo );
  
  unsigned short getAIMerror( void );
};

inline void OscarData :: shutdown( void )
{
  shuttingDown = 1;
}

inline void OscarData :: setSeqNumPointer( int *sequenceNum )
{
  sequence = sequenceNum;
}

#endif

