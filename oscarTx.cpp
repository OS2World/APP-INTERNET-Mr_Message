#include <string.h>
#include "oscardata.h"
#include "md5.h"
#include "compatibility.h"

static const char *defaultEncoding = "text/aolrtf; charset=\"us-ascii\"";


unsigned char *aim_encode_password( const char *password, int forceLength )
{
  int i, len;
  static unsigned char encoded[16];
  
  static unsigned char encoding_table[] = {
#if 0 /* old v1 table */
    0xf3, 0xb3, 0x6c, 0x99,
    0x95, 0x3f, 0xac, 0xb6,
    0xc5, 0xfa, 0x6b, 0x63,
    0x69, 0x6c, 0xc3, 0x9f
#else /* v2.1 table, also works for ICQ */
    0xf3, 0x26, 0x81, 0xc4,
    0x39, 0x86, 0xdb, 0x92,
    0x71, 0xa3, 0xb9, 0xe6,
    0x53, 0x7a, 0x95, 0x7c
#endif
  };

  if ( !forceLength ) 
    len = strlen( password );
  else
    len = forceLength;
  
  for ( i=0; i < len && i < 16; i++ )
  {
    encoded[i] = ((unsigned char)password[i] ^ encoding_table[i%16]);
  }

	return (unsigned char *)encoded;
}

void OscarData :: prepareConnectionAck( void )
{
  ULONG ackData = SWAP4( 1 );
  reset();
  addFLAP( 1 );
  addData( &ackData, 4 );
  tallyFLAPDataLen();
}

void OscarData :: prepareAuthKeyRequest( char *userName )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 0x17; // MD5 login
  theSNAC.subType = 6;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 6;
  addSNAC( &theSNAC );
  
  addTLV( 1, userName );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareLoginData( char *userName, char *password,
 AuthResponseData *authData )
{
  unsigned short shortData;
  unsigned long longData;
  SNAC theSNAC;
  md5_state_t state;
  md5_byte_t authHash[16];
  char charData;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 0x17; // MD5 login
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );

  addTLV( 1, userName );
  
  md5_init( &state );
  md5_append( &state, (unsigned char *)authData->authKey, authData->authKeyLen );
  md5_append( &state, (unsigned char *)password, strlen( password ) );
  md5_append( &state, (unsigned char *)"AOL Instant Messenger (SM)", 26 );
  md5_finish( &state, authHash );
  addTLV( 0x25, 16, authHash );

// Win32 client mimic would be:
// 
//  addTLV( 0x4c, 0, NULL ); // Unknown - Win32 client
//  addTLV( 3, "AOL Instant Messenger, version 5.9.3690/WIN32" );
//  shortData = SWAP2( 265 );
//  addTLV( 0x16, 2, &shortData ); // Client ID
//  shortData = SWAP2( 0x0009 );
//  addTLV( 0x18, 2, &shortData ); // Minor
//  shortData = SWAP2( 3690 );
//  addTLV( 0x1a, 2, &shortData ); // Build
//  charData = 1;
//  addTLV( 0x4a, 1, &charData ); // SSI use flag (0 = family 3, 1 = SSI only)

  addTLV( 3, "Mr. Message for OS/2" );
  shortData = SWAP2( 0x0500 );
  addTLV( 0x16, 2, &shortData ); // Client ID
  shortData = SWAP2( 0x0005 );
  addTLV( 0x17, 2, &shortData ); // Major
  shortData = SWAP2( 0x0001 );
  addTLV( 0x18, 2, &shortData ); // Minor
  shortData = SWAP2( 0x0000 );
  addTLV( 0x19, 2, &shortData ); // Point
  shortData = SWAP2( 0x1000 );
  addTLV( 0x1a, 2, &shortData ); // Build
  longData = 0;
  addTLV( 0x14, 4, &longData );  // distribution
  addTLV( 0x0f, "en" ); // language
  addTLV( 0x0e, "us" ); // country
  charData = 0;
  addTLV( 0x4a, 1, &charData ); // SSI use flag (0 = family 3, 1 = SSI only)
  tallyFLAPDataLen();
}

void OscarData :: prepareSignoffData( void )
{
  reset();
  addFLAP( 4 );
  tallyFLAPDataLen();
}

void OscarData :: prepareAuthorizationData( AuthResponseData *theData )
{
  unsigned short shortData;
  
  reset();
  addFLAP( 1 );
  shortData = 0;
  addData( &shortData, 2 );
  shortData = SWAP2( 1 );
  addData( &shortData, 2 );
  addTLV( 6, theData->authCookieLength, theData->authCookie );
  tallyFLAPDataLen();
}

void OscarData :: prepareVersionsRequest( void )
{
  SNAC theSNAC;
  unsigned short servicesRequested[] =
//  { SWAP2( 1 ), SWAP2( 3 ),  // Family number followed by version.
  { SWAP2( 1 ), SWAP2( 4 ),  // Family number followed by version.
    SWAP2( 2 ), SWAP2( 1 ),
    SWAP2( 3 ), SWAP2( 1 ),
    SWAP2( 4 ), SWAP2( 1 ),
    SWAP2( 9 ), SWAP2( 1 ),
//    SWAP2( 11 ), SWAP2( 1 ),
//    SWAP2( 13 ), SWAP2( 3 ),
//    SWAP2( 14 ), SWAP2( 1 ),
//    SWAP2( 15 ), SWAP2( 1 ),
//    SWAP2( 16 ), SWAP2( 1 ),
    SWAP2( 19 ), SWAP2( 3 ) // ,
//    SWAP2( 23 ), SWAP2( 1 )
  };
  
  // Request Generic Controls, Location Services, Buddy List Management,
  //  ICBM, Privacy Management, Usage Stats, Chat Navigation, Chat,
  //  Directory User Search, Buddy Icon Support, SSI Service,
  //  and Authorization and Registration Services
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 1;
  theSNAC.subType = 0x17;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0x17;
  addSNAC( &theSNAC );
  
  addData( servicesRequested, 12 * sizeof( unsigned short ) );
  tallyFLAPDataLen();
}

void OscarData :: prepareReqForSelfInformation( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 1;
  theSNAC.subType = 0xe; // User information request (self)
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0xe;
  addSNAC( &theSNAC );
  tallyFLAPDataLen();
}

void OscarData :: prepareRequestSSIlimits( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 0x13;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  tallyFLAPDataLen();
}

void OscarData :: prepareRateInfoRequest( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 1;
  theSNAC.subType = 6;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 6;
  addSNAC( &theSNAC );
  tallyFLAPDataLen();
}

void OscarData :: prepareRateInformationAck( userRateInformation *rateInfo )
{
  SNAC theSNAC;
  unsigned short shortData;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 1;
  theSNAC.subType = 8;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 8;
  addSNAC( &theSNAC );
  
  while ( rateInfo->windowSize )
  {
    shortData = SWAP2( rateInfo->rateClass );
    addData( &shortData, 2 );
    rateInfo++;
  }
  
  tallyFLAPDataLen();
}

void OscarData :: prepareCapabilitiesInfo( char *profileMessage )
{
  SNAC theSNAC;
  const unsigned char classInfo[] =
  {
    0x09, 0x46, 0x13, 0x43, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // File transfer (send)
    0x09, 0x46, 0x13, 0x48, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // File transfer (receive)
    0x09, 0x46, 0x13, 0x4b, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // Buddy list transfer
    0x09, 0x46, 0x13, 0x4d, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // ICQ interoperability
    0x09, 0x46, 0x13, 0x4e, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // UTF-8 message support
    0x74, 0x8f, 0x24, 0x20, 0x62, 0x87, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // Chat support
//    0x09, 0x46, 0x13, 0x45, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
//     0x53, 0x54, 0x00, 0x00, // Direct IM
    0x09, 0x46, 0x13, 0x46, 0x4c, 0x7f, 0x11, 0xd1, 0x82, 0x22, 0x44, 0x45,
     0x53, 0x54, 0x00, 0x00, // Avatar (buddy icon) support
  };
  static const char *defaultProfile = "This IM user is using Mr. Message for OS/2.";
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 2;
  theSNAC.subType = 4;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  if ( profileMessage )
  {
    addTLV( 1, defaultEncoding );
    addTLV( 2, profileMessage );
  } else {
    addTLV( 1, defaultEncoding );
    addTLV( 2, defaultProfile );
  }
  addTLV( 5, sizeof( classInfo ), classInfo );
  tallyFLAPDataLen();
  
  debugf( "\n" );
  debugf( "Client capabilities include:\n");
  debugf( "  File transfer (send and receive)\n" );
  debugf( "  Buddy List transfer\n" );
  debugf( "  ICQ interoperability\n" );
  debugf( "  UTF-8 support\n" );
  debugf( "  Chat support\n" );
  debugf( "  Avatar (buddy icon) support\n" );
  debugf( "\n" );
}

void OscarData :: prepareSetClientProfile( char *profileMessage )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 2;
  theSNAC.subType = 4;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  addTLV( 1, defaultEncoding );
  addTLV( 2, profileMessage );
  tallyFLAPDataLen();
}

void OscarData :: prepareSetClientAway( char *awayMessage )
{
  // If the away message is NULL, status will be set to "back",
  //  otherwise the status goes to "away".
  
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  theSNAC.family = 2;
  theSNAC.subType = 4;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  if ( awayMessage )
  {
    addTLV( 3, defaultEncoding );
    addTLV( 4, awayMessage );
  } else {
    addTLV( 4, "" );
  }
  tallyFLAPDataLen();
}

void OscarData :: prepareReqForSSI( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 0x13; // Server-stored information (SSI)
  theSNAC.subType = 4;   // Request all SSI
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  tallyFLAPDataLen();
}

void OscarData :: prepareSSIActivation( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 0x13; // Server-stored information (SSI)
  theSNAC.subType = 7;   // Activate SSI data and report buddy presence info
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0x13;
  addSNAC( &theSNAC );
  tallyFLAPDataLen();
}

void OscarData :: prepareSetStatus( void )
{
  SNAC theSNAC;
  unsigned long longData;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 1;
  theSNAC.subType = 0x1e;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0x1e;
  addSNAC( &theSNAC );
  longData = SWAP4( 0x01010020 );
  // Web aware, DC disabled, online, free for chat
  addTLV( 6, 4, &longData );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareClientReady( const unsigned short *supportedFamilies )
{
  SNAC theSNAC;
  int i, j;
  
  typedef struct
  {
    unsigned short family, version, toolid, toolversion;
  } moduleVersionData;
  
  const moduleVersionData modVersions[] =
  {
    { SWAP2( 0x0001 ), SWAP2( 0x0003 ), SWAP2( 0x0110 ), SWAP2( 0x047b ) },// JSC
    { SWAP2( 0x0002 ), SWAP2( 0x0001 ), SWAP2( 0x0101 ), SWAP2( 0x047b ) },// JSC
    { SWAP2( 0x0003 ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x047b ) },// JSC
    { SWAP2( 0x0004 ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x047b ) },// JSC
//    { SWAP2( 0x0005 ), SWAP2( 0x0001 ), SWAP2( 0x0001 ), SWAP2( 0x0001 ) },
//    { SWAP2( 0x0006 ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x0007 ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x0008 ), SWAP2( 0x0001 ), SWAP2( 0x0104 ), SWAP2( 0x0001 ) },
    { SWAP2( 0x0009 ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x047b ) },// JSC
//    { SWAP2( 0x000a ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x000b ), SWAP2( 0x0001 ), SWAP2( 0x0104 ), SWAP2( 0x0001 ) },
//    { SWAP2( 0x000c ), SWAP2( 0x0001 ), SWAP2( 0x0104 ), SWAP2( 0x0001 ) },
//    { SWAP2( 0x000d ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x000e ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x000f ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
//    { SWAP2( 0x0010 ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
    { SWAP2( 0x0013 ), SWAP2( 0x0003 ), SWAP2( 0x0110 ), SWAP2( 0x047b ) },// JSC
//    { SWAP2( 0x0015 ), SWAP2( 0x0001 ), SWAP2( 0x0110 ), SWAP2( 0x047c ) },
//    { SWAP2( 0x0018 ), SWAP2( 0x0001 ), SWAP2( 0x0010 ), SWAP2( 0x0629 ) },
    { 0, 0, 0, 0 } // Terminator
  };
  
  reset();
  addFLAP( 2 );
  
  theSNAC.family = 1;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  
  i = 0;
  while ( supportedFamilies[i] )
  {
    j = 0;
    while ( modVersions[j].family )
    {
      if ( modVersions[j].family == SWAP2( supportedFamilies[i] ) )
      {
        addData( &(modVersions[j].family), 2 );
        addData( &(modVersions[j].version), 2 );
        addData( &(modVersions[j].toolid), 2 );
        addData( &(modVersions[j].toolversion), 2 );
        break;
      }
      ++j;
    }
    ++i;
  }
  
  tallyFLAPDataLen();
}

void OscarData :: prepareRequestICBMParams( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 4;
  theSNAC.subType = 4;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareSetICBMParams( void )
{
  SNAC theSNAC;
  const unsigned char ICBMparams[16] = {
    0x00, 0x00,             // Channel (1=IMs, 2=xfer/chat inv, 3=chat)
                            // For some reason this needs to be 0
    0x00, 0x00, 0x00, 0x0b, // Flags (support messages, missed calls,
                            //  and typing notifications)
    0x1f, 0x40,             // Max message SNAC size
    0x03, 0xe7,             // Max sender warning level
    0x03, 0xe7,             // Max receiver warning level
    0x00, 0x00, 0x00, 0x00  // Minimum message interval
  };
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 4;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  
  addData( ICBMparams, 16 );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareReqLocationLimits( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  
  theSNAC.family = 2;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareReqBuddyManagementLimits( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );
  
  theSNAC.family = 3;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareRequestPrivacyParams( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 9;
  theSNAC.subType = 2;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 2;
  addSNAC( &theSNAC );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareRequestUserInfo( char *screenName,
 unsigned short infoType )
{
  SNAC theSNAC;
  unsigned short shortData;
  unsigned char nameLen;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 2;
  theSNAC.subType = 5;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 5;
  addSNAC( &theSNAC );
  
  shortData = SWAP2( infoType );
  addData( &shortData, 2 );
  nameLen = strlen( screenName );
  addData( &nameLen, 1 );
  addData( screenName, nameLen );
  
  tallyFLAPDataLen();
}


// Channel 2 plain text messages aren't recognized by any client I've seen.
// Bummer.  Other kinds of channel 2 messages are, so I'll save this code here
// for them.

/*
void OscarData :: sendInstantMessage( const char *toUser, const char *message )
{
  SNAC theSNAC;
  unsigned long longData, tickCount;
  unsigned short shortData;
  unsigned char charData;
  char *messageWithTags;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 4;
  theSNAC.subType = 6;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 6;
  addSNAC( &theSNAC );
  
  longData = 0;
  addData( &longData, 4 );
  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &tickCount, 4 );
  longData = SWAP4( tickCount );
  addData( &longData, 4 );
  shortData = SWAP2( 2 );
  addData( &shortData, 2 );
  charData = strlen( toUser );
  addData( &charData, 1 );
  addData( toUser, charData );
  
  shortData = SWAP2( 5 ); 
  addData( &shortData, 2 ); // Message data TLV type
  shortData = strlen( message ) + 30;
  shortData = SWAP2( shortData );
  addData( &shortData, 2 ); // Message data TLV length
  
  shortData = SWAP2( 0 );
  addData( &shortData, 2 ); // Message type = request
  longData = 0;
  addData( &longData, 4 );
  longData = SWAP4( tickCount );
  addData( &longData, 4 );  // Message cookie (tick count in this case)
  
  longData = 0;
  addData( &longData, 4 );
  addData( &longData, 4 );
  addData( &longData, 4 );
  addData( &longData, 4 ); // Class ID = 0x0000000000000000 - Plain text
  
  debugf( "Sending message: %s\n", message );
  
  addTLV( 0x2711, message ); // Add the message 
  
  tallyFLAPDataLen();
  printData();
}
*/

void OscarData :: sendInstantMessage( const char *toUser, const char *message,
 int wrapIt )
{
  SNAC theSNAC;
  unsigned long longData, tickCount;
  unsigned short shortData;
  unsigned char charData;
  char *messageWithTags;
  
  if ( wrapIt )
  {
    CHECKED_MALLOC( strlen( message ) + 27, messageWithTags );
    sprintf( messageWithTags, "<HTML><BODY>%s</BODY></HTML>", message );
    // Some clients are too stupid to display a plain text message unless it
    //  is wrapped in HTML.
  } else {
    messageWithTags = (char *)message;
  }
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 4;
  theSNAC.subType = 6;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 6;
  addSNAC( &theSNAC );
  
  longData = 0;
  addData( &longData, 4 );
  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &tickCount, 4 );
  longData = SWAP4( tickCount );
  addData( &longData, 4 );
  shortData = SWAP2( 1 );
  addData( &shortData, 2 );
  charData = strlen( toUser );
  addData( &charData, 1 );
  addData( toUser, charData );
  
  shortData = SWAP2( 2 ); 
  addData( &shortData, 2 ); // Message data TLV type
  shortData = strlen( messageWithTags ) + 13;
  shortData = SWAP2( shortData );
  addData( &shortData, 2 ); // Message data TLV length
  
  charData = 5;
  addData( &charData, 1 );
  charData = 1;
  addData( &charData, 1 );
  shortData = SWAP2( 1 );
  addData( &shortData, 2 );
  charData = 1;
  addData( &charData, 1 ); // First fragment- text capability required
  
  charData = 1;
  addData( &charData, 1 );
  addData( &charData, 1 );
  shortData = 4 + strlen( messageWithTags );
  shortData = SWAP2( shortData );
  addData( &shortData, 2 );
  shortData = 0;
  addData( &shortData, 2 );
  addData( &shortData, 2 );
  addData( messageWithTags, strlen( messageWithTags ) );
  
  if ( wrapIt )
  {
    CHECKED_FREE( messageWithTags );
  }
    
  tallyFLAPDataLen();
}

void OscarData :: prepareSetIdle( int idleTime )
{
  SNAC theSNAC;
  unsigned long idle;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 1;
  theSNAC.subType = 0x11;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0x11;
  addSNAC( &theSNAC );
  idle = SWAP4( idleTime );
  addData( &idle, 4 );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareServerPauseAck( void )
{
  SNAC theSNAC;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 1;
  theSNAC.subType = 0xc;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0xc;
  addSNAC( &theSNAC );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareAddClientSideBuddy( char *screenName )
{
  SNAC theSNAC;
  char len;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 3;
  theSNAC.subType = 4;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  
  len = (char) strlen( screenName );
  addData( &len, 1 );
  addData( screenName, len );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareRemoveClientSideBuddy( char *screenName )
{
  SNAC theSNAC;
  unsigned char len;
  
  reset();
  addFLAP( 2 );

  theSNAC.family = 3;
  theSNAC.subType = 5;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 4;
  addSNAC( &theSNAC );
  
  len = (unsigned char) strlen( screenName );
  addData( &len, 1 );
  addData( screenName, len );
  
  tallyFLAPDataLen();
}

void OscarData :: prepareTypingNotification( const char *screenName,
 unsigned short type )
{
  SNAC theSNAC;
  unsigned char len;
  unsigned short tmpShort;
  
  reset();
  addFLAP( 2 );
  
  len = (unsigned char) strlen( screenName );
  
  theSNAC.family = 4;
  theSNAC.subType = 0x14;
  theSNAC.flags[0] = 0;
  theSNAC.flags[1] = 0;
  theSNAC.requestID = 0x14;
  addSNAC( &theSNAC );
  
  tmpShort = 0;
  addData( &tmpShort, 2 );
  addData( &tmpShort, 2 );
  addData( &tmpShort, 2 );
  addData( &tmpShort, 2 );
  tmpShort = SWAP2( 1 );
  addData( &tmpShort, 2 );
  addData( &len, 1 );
  addData( screenName, len );
  
  tmpShort = SWAP2( type );
  addData( &tmpShort, 2 );
  
  tallyFLAPDataLen();
}

