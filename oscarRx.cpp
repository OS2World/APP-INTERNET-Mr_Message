#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oscardata.h"
#include "compatibility.h"

int OscarData :: isConnectionAck( void )
{
  unsigned long ackDWord;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 1 ) return 0;
  if ( position != 4 + sizeof( FLAP ) ) return 0;
  ackDWord = *((unsigned long *)(ptr + 1));
  if ( SWAP4( ackDWord ) != 1 ) return 0;
  debugf( "Connection Acknowledgement received from server.\n" );
  return 1;
}

int OscarData :: getMD5AuthKey( AuthResponseData *authData )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  USHORT *loc;
  
  authData->authKey = NULL;
  authData->authKeyLen = 0;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 0x17 || subType != 7 )
    return 0;
  
  loc = (USHORT *)(theSNAC + 1);
  authData->authKeyLen = SWAP2( *loc );
  CHECKED_MALLOC( authData->authKeyLen, authData->authKey );
  loc++;
  memcpy( authData->authKey, loc, authData->authKeyLen );
  
  return 1;
}

int OscarData :: getAuthResponseData( AuthResponseData *theData )
{
  SNAC *theSNAC;
  unsigned short TLVtype, family, subType;
  int length, totLength, tmpPosition = position;
  unsigned char *theTLVstring;
  
  theData->clear();
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return -1;
  if ( position < sizeof( SNAC ) ) return -1;
  
  theSNAC = (SNAC *)(ptr + 1);
  
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 0x17 || subType != 3 ) return -1;
  
  totLength = position;
  position = sizeof( FLAP ) + sizeof( SNAC );
  
  while ( (length = parseTLV( &theTLVstring, &TLVtype )) != 0 )
  {
    switch ( TLVtype )
    {
      case 0x01:
        theData->screenName = (char *)theTLVstring;
      break;
      case 0x04:
        theData->errorURL = (char *)theTLVstring;
      break;
      case 0x05:
        theData->BOSserver = (char *)theTLVstring;
      break;
      case 0x06:
        theData->authCookie = (char *)theTLVstring;
        theData->authCookieLength = length;
      break;
      case 0x08:
        if ( length != 2 )
        {
          debugf( "Bad error code TLV.\n" );
          printData();
        } else {
          unsigned short tmpShort = *((unsigned short *)theTLVstring);
          theData->errorCode = SWAP2( tmpShort );
        }
        CHECKED_FREE( theTLVstring );
      break;
      case 0x11:
        theData->emailAddress = (char *)theTLVstring;
      break;
      case 0x13:
        if ( length != 2 )
        {
          debugf( "Bad registration status TLV.\n" );
        } else {
          unsigned short tmpShort = *((unsigned short *)theTLVstring);
          theData->registrationStatus = SWAP2( tmpShort );
        }
        CHECKED_FREE( theTLVstring );
      break;
      case 0x40:
        // They'll give us a properly parsed ASCII version so don't bother
        //  with this TLV
        CHECKED_FREE( theTLVstring );
      break;
      case 0x41:
        debugf( "AOL's latest client beta version available here:\n" );
        debugf( "%s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x42:
        debugf( "Information for AOL's latest beta: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x43:
        debugf( "AOL's latest beta's name is: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x44:
        // They'll give us a properly parsed ASCII version so don't bother
        //  with this TLV
        CHECKED_FREE( theTLVstring );
      break;
      case 0x45:
        debugf( "AOL's latest client release version available here:\n" );
        debugf( "%s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x46:
        debugf( "Information for AOL's latest release: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x47:
        debugf( "AOL's latest release's name is: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x48:
        debugf( "Latest MD5 checksum for the beta client: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x49:
        debugf( "Latest MD5 checksum for the release client: %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
      break;
      case 0x54:
        theData->passwordURL = (char *)theTLVstring;
      break;
      default:
        debugf( "Unrecognized TLV type on authentication response:\n" );
        debugf( "Type:   %d\n", TLVtype );
        debugf( "Length: %d\n", length );
        debugf( "Value:  %s\n", theTLVstring );
        CHECKED_FREE( theTLVstring );
    }
  }
  
  position = tmpPosition;
  return 0;
}

int OscarData :: isHostReadyMessage( unsigned short **supportedFamilies )
{
  SNAC *theSNAC;
  unsigned short family, subType, i, familyDataLen;
  unsigned short *familyData;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 3 || theSNAC->flags[0] ||
        theSNAC->flags[1] )
    return 0;
  
  familyDataLen = (unsigned short)
   (SWAP2(ptr->dataLen) - sizeof( SNAC ));
  
  CHECKED_MALLOC( familyDataLen + 2, *supportedFamilies );
  
  familyDataLen /= sizeof( unsigned short );
  familyData = (unsigned short *) (theSNAC + 1); // After the SNAC data
  
  for ( i=0; i<familyDataLen; i++ )
  {
    (*supportedFamilies)[i] = SWAP2( familyData[i] );
  }
  (*supportedFamilies)[i] = 0;
  // Terminate the list with a 0
  
  return 1;
}

int OscarData :: isFamilyVersions( void )
{
  SNAC *theSNAC;
  unsigned short family, subType, familyDataLen, i;
  unsigned short *familyData;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 24 )
    return 0;
  
  familyDataLen = (unsigned short)
   (SWAP2(ptr->dataLen) - sizeof( SNAC ));
   
  if ( familyDataLen % 4 )
    return 0;
  // If this number is not evenly divisible by 4, then we're not interpretting
  //  the data correctly.
  
  familyDataLen /= sizeof( unsigned short );
  familyData = (unsigned short *) (theSNAC + 1); // After the SNAC data

  printData();
  debugf( "\n" );
  debugf( "Versions of services available on this server:\n" );
  debugf( "----------------------------------------------\n" );
  for ( i=0; i<familyDataLen; i+=2 )
  {
    debugf( "Family %d version number is %d.\n", SWAP2( familyData[i] ),
     SWAP2( familyData[i+1] ) );
  }
  debugf( "----------------------------------------------\n" );
  debugf( "\n" );
  return 1;
}

int OscarData :: isMOTD( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 19 )
    return 0;
  
  return 1;
}

int OscarData :: isRateInformation( userRateInformation **rateInfo )
{
  SNAC *theSNAC;
  unsigned short family, subType, numRateClasses, rateClass, i, j, numPairs;
  unsigned short *shortPtr;
  unsigned long *longPtr;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
 
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 7 )
    return 0;
  
  shortPtr = (unsigned short *)(theSNAC + 1);
  numRateClasses = SWAP2( *shortPtr );
  ++shortPtr;
  longPtr = (unsigned long *) shortPtr;
  
  CHECKED_MALLOC( sizeof( userRateInformation ) * (numRateClasses + 1), *rateInfo );
  
  printData();
  debugf( "\n" );
  debugf( "Rate information\n" );
  debugf( "----------------\n" );
  
  for ( i=0; i<numRateClasses; ++i )
  {
    (*rateInfo)[i].rateClass = SWAP2( *shortPtr );
    ++shortPtr;
    longPtr = (unsigned long *)shortPtr;
    (*rateInfo)[i].windowSize      = SWAP4( longPtr[0] );
    (*rateInfo)[i].clearLevel      = SWAP4( longPtr[1] );
    (*rateInfo)[i].alertLevel      = SWAP4( longPtr[2] );
    (*rateInfo)[i].limitLevel      = SWAP4( longPtr[3] );
    (*rateInfo)[i].disconnectLevel = SWAP4( longPtr[4] );
    (*rateInfo)[i].currentLevel    = SWAP4( longPtr[5] );
    (*rateInfo)[i].maxLevel        = SWAP4( longPtr[6] );
    (*rateInfo)[i].lastTime        = SWAP4( longPtr[7] );
    (*rateInfo)[i].currentState    = *((unsigned char *) (longPtr + 8));
    
    debugf( "Rate class %d: Window Size      = %ld\n",
     (*rateInfo)[i].rateClass, (*rateInfo)[i].windowSize );
    debugf( "              Clear Level      = %ld\n",
     (*rateInfo)[i].clearLevel );
    debugf( "              Alert Level      = %ld\n",
     (*rateInfo)[i].alertLevel );
    debugf( "              Limit Level      = %ld\n",
     (*rateInfo)[i].limitLevel );
    debugf( "              Disconnect Level = %ld\n",
     (*rateInfo)[i].disconnectLevel );
    debugf( "              Current Level    = %ld\n",
     (*rateInfo)[i].currentLevel );
    debugf( "              Max Level        = %ld\n",
     (*rateInfo)[i].maxLevel );
    debugf( "              Last time        = %ld\n",
     (*rateInfo)[i].lastTime );
    debugf( "              Current state    = %d\n",
     (*rateInfo)[i].currentState );
    longPtr = (unsigned long *) ((unsigned char *)(longPtr + 8) + 1);
    shortPtr = (unsigned short *) longPtr;
  }
  
  (*rateInfo)[i].rateClass = 0;
  (*rateInfo)[i].windowSize = 0;
  // Terminating entry
  
  debugf( "----------------\n" );
  debugf( "\n" );
  debugf( "Rate information applicability\n" );
  debugf( "------------------------------\n" );
  
  for ( i=0; i<numRateClasses; ++i )
  {
    rateClass = SWAP2( *shortPtr );
    ++shortPtr;
    debugf( "Rate Group %d:\n", rateClass );
    numPairs = SWAP2( *shortPtr );
    ++shortPtr;
    longPtr = (unsigned long *)shortPtr;
    
    (*rateInfo)[i].numAppliesTo = numPairs;
    if ( numPairs )
    {
      CHECKED_MALLOC( numPairs * sizeof( unsigned short ),
       (*rateInfo)[i].appliesToFam );
      CHECKED_MALLOC( numPairs * sizeof( unsigned short ),
       (*rateInfo)[i].appliesToSub );
    } else {
      (*rateInfo)[i].appliesToFam = NULL;
      (*rateInfo)[i].appliesToSub = NULL;
    }
    
    (*rateInfo)[i].outgoingData = 0;
    (*rateInfo)[i].rateThread = 0;
    
    for ( j=0; j<numPairs; ++j )
    {
      (*rateInfo)[i].appliesToFam[j] = (USHORT)
       ((SWAP4( *longPtr ) & 0xffff0000) >> 16);
      (*rateInfo)[i].appliesToSub[j] = (USHORT) (SWAP4( *longPtr ) & 0xffff);
      
      debugf( "  Family 0x%04x / Subtype 0x%04x\n",
       (*rateInfo)[i].appliesToFam[j], (*rateInfo)[i].appliesToSub[j] );
      ++longPtr;
    }
    
    if ( !j ) debugf( "  N/A\n" );
    
    shortPtr = (unsigned short *) longPtr;
  }
  debugf( "------------------------------\n" );
  
  return 1;
}

int OscarData :: getUserInformation( UserInformation *theInfo )
{
  SNAC *theSNAC;
  unsigned short family, subType, tmpShort, i, tlvType, totLen;
  unsigned short *rawPtr;
  unsigned char *tlvData;
  unsigned char tmpChar;
  int tlvLen;
  
  theInfo->clear();
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return -1;
  if ( position < sizeof( SNAC ) ) return -1;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( (family != 1 || subType != 0xf) && (family != 2 || subType != 6) &&
        (family != 3 || (subType != 0xb && subType != 0xc)) )
    return -1;
  
  debugf( "User information packet received (family %d, subtype %d).\n",
   family, subType );
  
  totLen = (USHORT) (sizeof( FLAP ) + SWAP2( ptr->dataLen ));
  
  if ( family == 3 && subType == 0xc ) theInfo->isOnline = 0;
   else theInfo->isOnline = 1;
  
  rawPtr = (unsigned short *)(theSNAC + 1); // Just past SNAC info
  tmpChar = *((unsigned char *)rawPtr);
  
  position = sizeof( FLAP ) + sizeof( SNAC );
  
  if ( tmpChar == 0 )
  {
    // Sometimes a family 1, sub 0xf doesn't start right in with the
    //  screen name length and name.  To signal that it is changing
    //  things up, it seems to use a "length" of 0, followed by another
    //  byte indicating the length of some extra information that I
    //  don't know how to parse yet.  After this extra data, it will
    //  pick up with the screen name length and the rest that I handle.
    rawPtr = (unsigned short *) (((unsigned char *)rawPtr) + 1);
    tmpChar = *((unsigned char *)rawPtr);
    if ( !tmpChar || tmpChar + position + 6 > totLen )
    {
      debugf( "I don't seem to understand this user information packet:\n" );
      printData();
      return -1;
    }
    rawPtr = (unsigned short *)(((unsigned char *)rawPtr) + 1 + tmpChar);
    position += tmpChar + 2;
    tmpChar = *((unsigned char *)rawPtr);
    if ( !tmpChar )
    {
      debugf( "I don't seem to understand this user information packet:\n" );
      printData();
      return -1;
    }
  }
  
  CHECKED_MALLOC( tmpChar + 1, theInfo->screenName );
  
  memcpy( theInfo->screenName, ((unsigned char *)rawPtr) + 1, tmpChar );
  theInfo->screenName[ tmpChar ] = 0;  // Add NULL termination
  
  rawPtr = (unsigned short *)(((unsigned char *)rawPtr) + 1 + tmpChar);
  
  theInfo->warningLevel = SWAP2( rawPtr[0] );
  
  if ( family == 3 && subType == 0xc )
  {
    // I've seen some weird info in these "going offline" packets here, so
    //  avoid confusion in the code by just stopping the interpretation
    //  here, since I don't understand the rest anyway.
    return 0;
  }
  
  tmpShort = SWAP2( rawPtr[1] );  // Number of TLVs to follow
  
  position += 5 + tmpChar;
  
  for ( i=0; i<tmpShort && position < totLen; ++i )
  {
    tlvLen = parseTLV( &tlvData, &tlvType );
    
    switch ( tlvType )
    {
      case 0:
        // Two bytes, value 1 usually - during signoff...
        //  meaning unknown, but don't whine about it
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 1:
        if ( tlvLen == 4 )
          theInfo->userClass = SWAP4( *((unsigned long *)tlvData) );
        else if ( tlvLen == 2 )
        {
          theInfo->userClass = SWAP2( *((unsigned short *)tlvData) );
        }
        else {
          debugf( "Unexpected length of User Class TLV: %d.\n", tlvLen );
          printData();
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 2:
        if ( tlvLen != sizeof( time_t ) )
        {
          debugf( "Bad Signup Date TLV.\n" );
          printData();
        } else {
          time_t theTime = *((time_t *)tlvData);
          theInfo->signupDate = SWAP4( theTime );
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 3:
        if ( tlvLen != sizeof( time_t ) )
        {
          debugf( "Bad Signon Date TLV.\n" );
          printData();
        } else {
          time_t theTime = *((time_t *)tlvData);
          theInfo->signonDate = SWAP4( theTime );
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 4:
        if ( tlvLen == 4 )
          theInfo->idleMinutes = SWAP4( *((unsigned long *)tlvData) );
        else if ( tlvLen == 2 )
          theInfo->idleMinutes = SWAP2( *((unsigned short *)tlvData) );
        else {
          debugf( "Unexpected length of Idle Minutes TLV: %d.\n", tlvLen );
          printData();
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 5:
        if ( tlvLen != sizeof( time_t ) )
        {
          debugf( "Bad Member Since Date TLV.\n" );
          printData();
        } else {
          time_t theTime = *((time_t *)tlvData);
          theInfo->memberSinceDate = SWAP4( theTime );
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 6:
        if ( tlvLen == 4 )
          theInfo->userStatus = SWAP4( *((unsigned long *)tlvData) );
        else if ( tlvLen == 2 )
          theInfo->userStatus = SWAP2( *((unsigned short *)tlvData) );
        else {
          debugf( "Unexpected length of User Status TLV: %d.\n", tlvLen );
          printData();
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 10:
        // IP address!
        if ( tlvLen != 4 )
        {
          debugf( "Bad IP address TLV.\n" );
          printData();
        } else {
          theInfo->ipAddress[0] = tlvData[0];
          theInfo->ipAddress[1] = tlvData[1];
          theInfo->ipAddress[2] = tlvData[2];
          theInfo->ipAddress[3] = tlvData[3];
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 12:
        // ICQ stuff not recognized by gAIM
        // See gAIM source, protocols\oscar\info.c line 599.
      case 13:
        // Capabilities - need to understand more gAIM code.
      case 14:
        // ??? Always 0 length, not used by gAIM
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 15:
      case 16:
        // Session length - 15 = AIM, 16 = AOL
        if ( tlvLen == 4 )
          theInfo->sessionLen = SWAP4( *((unsigned long *)tlvData) );
        else if ( tlvLen == 2 )
          theInfo->sessionLen = SWAP2( *((unsigned short *)tlvData) );
        else {
          debugf( "Unexpected length of Session Length TLV: %d.\n", tlvLen );
          printData();
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 20:
      case 25:
      case 27:
        // ??? Not known by gAIM
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 29:
      {
        // Cool stuff - status message, buddy icon
        int curTLVpos = 0;
        unsigned short subType;
        unsigned char subNumber, subLen;
        unsigned short *tlvPtr;
        
        while ( curTLVpos <= tlvLen - 4 )
        {
          tlvPtr = (unsigned short *) (tlvData + curTLVpos);
          subType = SWAP2( *tlvPtr );
          tlvPtr++;
          subNumber = ((unsigned char *)tlvPtr)[0];
          subLen = ((unsigned char *)tlvPtr)[1];
          tlvPtr++;
          curTLVpos += 4;
          
          switch ( subType )
          {
            case 0:
              // "Official" buddy icon.
              // 5 bytes: 0x02, 0x01, 0xd2, 0x04, 0x72
              // Meaning not really known, so who cares.
              curTLVpos += subLen;
            break;
            case 1:
              // Buddy icon checksum
              if ( subLen > 0 && subNumber == 1 )
              {
                CHECKED_MALLOC( subLen, theInfo->buddyIconChecksum );
                memcpy( theInfo->buddyIconChecksum, tlvPtr, subLen );
                theInfo->buddyIconChecksumLen = subLen;
              }
              curTLVpos += subLen;
            break;
            case 2:
              // Status message
              if ( subLen > 4 )
              {
                theInfo->statusMessageLen = SWAP2( *tlvPtr );
                ++tlvPtr;
                CHECKED_MALLOC( theInfo->statusMessageLen + 1, theInfo->statusMessage );
                memcpy( theInfo->statusMessage, tlvPtr,
                 theInfo->statusMessageLen );
                theInfo->statusMessage[ theInfo->statusMessageLen ] = 0;
                // NULL terminate string
                curTLVpos += 2 + theInfo->statusMessageLen;
                tlvPtr = (unsigned short *) (tlvData + curTLVpos);
                if ( *tlvPtr == SWAP2( 1 ) )
                {
                  // Some kind of encoding.  gAIM eats the first short and then
                  //  captures the encoding type.
                  tlvPtr += 2;
                  theInfo->statusMessageEncodingLen = SWAP2( *tlvPtr );
                  ++tlvPtr;
                  CHECKED_MALLOC( theInfo->statusMessageEncodingLen + 1, theInfo->statusMessageEncoding );
                  memcpy( theInfo->statusMessageEncoding, tlvPtr,
                   theInfo->statusMessageEncodingLen );
                  theInfo->statusMessageEncoding[
                   theInfo->statusMessageEncodingLen ] = 0;
                  // NULL terminate string
                  curTLVpos += 6 + theInfo->statusMessageEncodingLen;
                } else {
                  curTLVpos += 2;
                }
              } else {
                // Shouldn't happen, but just in case
                curTLVpos += subLen;
              }
            break;
            default:
              // Something we don't know or understand yet.  Skip.
              curTLVpos += subLen;
            break;
          }
        }
        if ( tlvLen ) CHECKED_FREE( tlvData );
      }
      break;
      case 30:
        // ??? Four unknown bytes, not used by gAIM
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      case 34:
        // Started getting these recently and haven't found info yet.
        if ( tlvLen ) CHECKED_FREE( tlvData );
      break;
      default:
        debugf( "Received unexpected User Info TLV.\nType: %d\nValue: %s\n",
         tlvType, tlvData );
        if ( tlvLen ) CHECKED_FREE( tlvData );
    }
  }
  
  if ( position < totLen && (family != 1 || subType != 0xf) )
  {
    // There is more to this packet than just the 'fixed part' TLVs.
    while ( position < totLen )
    {
      tlvLen = parseTLV( &tlvData, &tlvType );
      
      if ( !tlvLen ) break;
      
      switch ( tlvType )
      {
        case 1:
          if ( theInfo->clientProfileEncodingLen )
            CHECKED_FREE( theInfo->clientProfileEncoding );
          theInfo->clientProfileEncodingLen = tlvLen;
          theInfo->clientProfileEncoding = (char *)tlvData;
        break;
        case 2:
          if ( theInfo->clientProfileLen )
            CHECKED_FREE( theInfo->clientProfile );
          theInfo->clientProfileLen = tlvLen;
          theInfo->clientProfile = (char *)tlvData;
        break;
        case 3:
          if ( theInfo->awayMessageEncodingLen )
            CHECKED_FREE( theInfo->awayMessageEncoding );
          theInfo->awayMessageEncodingLen = tlvLen;
          theInfo->awayMessageEncoding = (char *)tlvData;
        break;
        case 4:
          if ( theInfo->awayMessageLen )
            CHECKED_FREE( theInfo->awayMessage );
          theInfo->awayMessageLen = tlvLen;
          theInfo->awayMessage = (char *)tlvData;
        break;
        default:
          debugf( "Note:  Unhandled non-fixed TLV type (%d) detected in user info.\n",
           tlvType );
          CHECKED_FREE( tlvData );
      }
    }
  }
  
  return 0;
}

int OscarData :: getSSIData( SSIData *theData )
{
  unsigned char SSIversion;
  SNAC *theSNAC;
  unsigned short family, subType, gid, bid, type, tlvLen, tlvType, tlvStart;
  unsigned short nameLen, i, j, itemsInSNAC, totLen, numBuddies, tlvTotLen;
  unsigned short curBuddy;
  unsigned short *rawPtr;
  unsigned char *tlvData;
  char *theName;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) + 3 ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 0x13 || subType != 6 )
    return -1;
    
  // DEBUG to see what's going on with the buddy list
  // printData();
  // DEBUG to see what's going on with the buddy list
  
  SSIversion = *((unsigned char *)(theSNAC + 1));
  
  if ( SSIversion != 0 )
    return -1;
  
  if ( theSNAC->flags[0] || theSNAC->flags[1] )
  {
    debugf( "This buddy list may have another part.\n" );
  }
  
  totLen = (USHORT) (sizeof( FLAP ) + SWAP2( ptr->dataLen ));
  rawPtr = (unsigned short *)(((unsigned char *)(theSNAC + 1)) + 1);
  itemsInSNAC = SWAP2( rawPtr[0] );
  ++rawPtr;
  
  position = sizeof( FLAP ) + sizeof( SNAC ) + 3;
  
  // Pre-scan the buddy list to determine the number of entries and
  //  check if the packet is complete and correct.
  
  numBuddies = 0;
  
  for ( i=0; i<itemsInSNAC; ++i )
  {
    if ( position + 10 > totLen )
    {
      return -1;
    }
    nameLen = SWAP2( rawPtr[0] );
    position += 2 + nameLen;
    
    if ( position + 8 > totLen ) return -1;
    rawPtr = (unsigned short *)(((unsigned char *)ptr) + position);
    type = SWAP2( rawPtr[2] );
    if ( (type == 1 || type == 0) && nameLen )
    {
      // Buddy or group heading
      numBuddies++;
    }
    tlvLen = SWAP2( rawPtr[3] );
    position += 8;
    if ( tlvLen ) position += tlvLen;
    rawPtr = (unsigned short *)(((unsigned char *)ptr) + position);
  }
  
  if ( position + 4 > totLen && !theSNAC->flags[0] && !theSNAC->flags[1] )
    return -1;
  // Date stamp (time_t) on the end only if this is the true end of the
  //  buddy list.  Flags indicate that it is not.  Long buddy lists may
  //  be continued in several parts.

  if ( numBuddies == 0 )
  {
    // Loser or someone who doesn't keep any server-stored info
    return 0;
  }
  
  if ( theData->rootBuddies )
  {
    // This buddy list already has some entries.  Add to them.
    CHECKED_REALLOC( theData->rootBuddies,
     (theData->numRootBuddies + numBuddies) * sizeof( buddyListEntry ),
     theData->rootBuddies );
    memset( theData->rootBuddies + theData->numRootBuddies, 0,
     numBuddies * sizeof( buddyListEntry ) );
    // Make sure the new entries are cleared.
  } else {
    CHECKED_CALLOC( numBuddies * sizeof( buddyListEntry ), theData->rootBuddies );
    // Everything is initialized to 0
  }
  
  position = sizeof( FLAP ) + sizeof( SNAC ) + 3;
  rawPtr = (unsigned short *)(((unsigned char *)ptr) + position);
  curBuddy = theData->numRootBuddies;
  
  for ( i=0; i<itemsInSNAC; ++i )
  {
    // We can throw caution to the wind in this execution of the loop.
    
    nameLen = SWAP2( rawPtr[0] );
    if ( nameLen )
    {
      theName = (char *)(rawPtr + 1);
      position += 2 + nameLen;
    } else {
      position += 2;
    }
    rawPtr = (unsigned short *)(((unsigned char *)ptr) + position);
    
    gid = SWAP2( rawPtr[0] );
    bid = SWAP2( rawPtr[1] );
    type = SWAP2( rawPtr[2] );
    tlvTotLen = SWAP2( rawPtr[3] );
    position += 8;
    tlvStart = position;
    tlvLen = 0;
    
    if ( (type == 1 || type == 0) && nameLen )
    {
      // Buddy or group heading
      CHECKED_MALLOC( nameLen + 1,
       theData->rootBuddies[ curBuddy ].entryName );
      strncpy( theData->rootBuddies[ curBuddy ].entryName,
       theName, nameLen );
      theData->rootBuddies[ curBuddy ].entryName[ nameLen ] = 0;
    }
    
    while ( position < tlvStart + tlvTotLen )
    {
      tlvLen = (unsigned short) parseTLV( &tlvData, &tlvType );
      // position is updated in here as the TLV is parsed
      
      if ( type == 1 && tlvType == 0xc8 && nameLen )
      {
        // Group (members IDs follow in USHORTs, must be an even number len
        theData->rootBuddies[ curBuddy ].gid = gid;
        theData->rootBuddies[ curBuddy ].id = bid;
        theData->rootBuddies[ curBuddy ].numMembers = tlvLen / 2;
        theData->rootBuddies[ curBuddy ].memberIDs =
         (unsigned short *) tlvData;
        for ( j=0; j<tlvLen/2; ++j )
        {
          ((unsigned short *)tlvData)[j] =
           SWAP2(((unsigned short *)tlvData)[j]);
        }
        theData->numRootBuddies++;
        curBuddy++;
      } else {
        // Unknown thinger dealie
        if ( tlvLen ) CHECKED_FREE( tlvData );
      }
    }
    
    if ( type == 0 && nameLen )
    {
      // Individual buddy
      theData->rootBuddies[ curBuddy ].gid = gid;
      theData->rootBuddies[ curBuddy ].id = bid;
      theData->numRootBuddies++;
      curBuddy++;
    }
    
    rawPtr = (unsigned short *)(((unsigned char *)ptr) + position);
  }
  
  theData->dateStamp = SWAP4( (*((unsigned long *)rawPtr)) );

  return 0;
}

unsigned short OscarData :: getAIMerror( void )
{
  SNAC *theSNAC;
  unsigned short family, subType, errorCode;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) + 3 ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( subType != 1 ) return 0;
  
  // Family is actually irrelevant for this message.  The same set of error
  //  codes is used for all families.
  
  errorCode = SWAP2( *((unsigned short *)(theSNAC + 1)) );
  
  if ( debugOutputStream )
  {
    debugf( "Error message received from AIM server\n" );
    debugf( "--------------------------------------\n" );
    debugf( "SNAC family: %d\n", family );
    debugf( "Error type:  " );
    
    switch( errorCode )
    {
      case 1:  fprintf( debugOutputStream, "Invalid SNAC header.\n" ); break;
      case 2:  fprintf( debugOutputStream, "Server rate limit exceeded.\n" ); break;
      case 3:  fprintf( debugOutputStream, "Client rate limit exceeded.\n" ); break;
      case 4:  fprintf( debugOutputStream, "Recipient is not logged in.\n" ); break;
      case 5:  fprintf( debugOutputStream, "Requested service is unavailable.\n" ); break;
      case 6:  fprintf( debugOutputStream, "Requested service is not defined.\n" ); break;
      case 7:  fprintf( debugOutputStream, "Obsolete SNAC was detected.\n" ); break;
      case 8:  fprintf( debugOutputStream, "Not supported by server.\n" ); break;
      case 9:  fprintf( debugOutputStream, "Not supported by client.\n" ); break;
      case 10: fprintf( debugOutputStream, "Refused by client.\n" ); break;
      case 11: fprintf( debugOutputStream, "Reply was too big.\n" ); break;
      case 12: fprintf( debugOutputStream, "Responses have been lost.\n" ); break;
      case 13: fprintf( debugOutputStream, "Request was denied.\n" ); break;
      case 14: fprintf( debugOutputStream, "Incorrect SNAC format.\n" ); break;
      case 15: fprintf( debugOutputStream, "Insufficient rights.\n" ); break;
      case 16: fprintf( debugOutputStream, "Recipient was blocked locally.\n" ); break;
      case 17: fprintf( debugOutputStream, "Sender is too frickin evil.\n" ); break;
      case 18: fprintf( debugOutputStream, "Receiver is too quasi-evil.\n" ); break;
      case 19: fprintf( debugOutputStream, "User is temporarily unavailable.\n" ); break;
      case 20: fprintf( debugOutputStream, "No match found.\n" ); break;
      case 21: fprintf( debugOutputStream, "List overflowed.\n" ); break;
      case 22: fprintf( debugOutputStream, "Request was ambiguous (as is this message).\n" ); break;
      case 23: fprintf( debugOutputStream, "Server queue is full.\n" ); break;
      case 24: fprintf( debugOutputStream, "Not while on AOL.  ??\n" ); break;
      default: fprintf( debugOutputStream, "UNKNOWN ERROR (%d)\n", errorCode );
    }
    debugf( "Error was in response to request ID: 0x%lx\n", theSNAC->requestID );
    debugf( "--------------------------------------\n" );
  }
  
  return errorCode;
}

int OscarData :: instantMessageReceived( char **theUser, char **theMessage,
 rendevousInfo *extraInfo )
{
  SNAC *theSNAC;
  unsigned short family, subType, tmpShort, channel, numTLVs, i;
  unsigned short tlvType, fragPos, fragLen;
  unsigned short *shortPtr;
  unsigned char tmpChar;
  unsigned char *charPtr, *fragPtr;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) + 3 ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  *theUser = NULL;
  *theMessage = NULL;
  extraInfo->rendevousType = RENDEVOUS_TYPE_VOID;
  
  if ( family != 4 || subType != 7 )
  {
    return 0;
  }
  
  shortPtr = ((unsigned short *)(theSNAC + 1)) + 4;
  // Skip over the initial QWORD uptime counter... who cares?
  
  channel = SWAP2( *shortPtr );
  ++shortPtr;
  
  charPtr = (unsigned char *)shortPtr;
  tmpChar = *charPtr;
  
  CHECKED_MALLOC( tmpChar + 1, *theUser );
  strncpy( *theUser, (char *)(charPtr + 1), tmpChar );
  (*theUser)[ tmpChar ] = 0;
  charPtr += 1 + tmpChar;
  
  shortPtr = ((unsigned short *)charPtr) + 1;
  // Advance past the warning level.  I certainly don't care about it.
  
  numTLVs = SWAP2( *shortPtr );
  ++shortPtr;
  
  position = sizeof( FLAP ) + sizeof( SNAC ) + tmpChar + 15;
  fragPos = 0;
  
  for ( i=0; i<numTLVs; ++i )
  {
    tmpShort = parseTLV( &charPtr, &tlvType );
    if ( tmpShort ) CHECKED_FREE( charPtr );
    fragPos += 4 + tmpShort;
  }
  // This is done for parsing only.  All of the information that is provided
  //  in these TLVs is redundant so I don't need to process it again.
  
  shortPtr = (unsigned short *)(((unsigned char *)shortPtr) + fragPos);
  
  switch ( channel )
  {
    case 1:  // Channel 1 - plain text
    {
      fragPos = 0;
      fragLen = parseTLV( &fragPtr, &tlvType );
      
      while ( tlvType != 2 && (fragLen || tlvType) )
      {
        debugf( "Received unknown TLV type in non-fixed part of IM.  TLV type: %d\n", tlvType );
        CHECKED_FREE( fragPtr );
        fragLen = parseTLV( &fragPtr, &tlvType );
      }
      
      if ( tlvType != 2 )
      {
        CHECKED_FREE( fragPtr );
        CHECKED_FREE( *theUser );
        *theUser = NULL;
        return 0;
      }
      
      // There is often other nonsense in the way before we get to the
      //  plain text message, including buddy icon information and typing
      //  notifications (even though those should have a separate message).
      //  Bypass all of that here and just get to what we care about.
      
      while ( fragPos < fragLen )
      {
        switch ( *(fragPtr + fragPos) )
        {
          case 5:
            // Fragment versions/caps.  I don't need to care about this.
            if ( fragPos + 4 > fragLen )
            {
              debugf( "Unexpected IM caps fragment length.\n" );
              break;
            }
            shortPtr = ((unsigned short *)(fragPtr + fragPos + 2));
            tmpShort = SWAP2( *shortPtr );
            fragPos += 4 + tmpShort;
          break;
          case 1:
            if ( fragPos + 4 > fragLen )
            {
              debugf( "Unexpected IM fragment length.\n" );
              break;
            }
            shortPtr = ((unsigned short *)(fragPtr + fragPos + 2));
            tmpShort = SWAP2( *shortPtr );
            if ( fragPos + tmpShort + 4 > fragLen )
            {
              debugf( "Unexpected IM fragment length.\n" );
              break;
            }
            CHECKED_MALLOC( tmpShort - 3, (*theMessage) );
            fragPos += 8;
            // I don't care about language or character sets now.
            
            strncpy( *theMessage, (char *)(fragPtr + fragPos), tmpShort - 4 );
            (*theMessage)[ tmpShort - 4 ] = 0;
            fragPos += tmpShort - 4;
          break;
        }
        
        if ( *theMessage && *theUser )
        {
          debugf( "IM Received- [%s]: %s\n", *theUser, *theMessage );
        }
      }
      
      CHECKED_FREE( fragPtr );
    }
    break;
    case 2: // Channel 2 - rendevous or fancier text
    {
      const unsigned char plainTextClass[] =
       { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
      const unsigned char chatClass[] =
       { 0x74, 0x8f, 0x24, 0x20, 0x62, 0x87, 0x11, 0xd1,
         0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
      
      unsigned char *classID;
      unsigned char *tlvData;
      char tmpChar;
      
      fragLen = parseTLV( &tlvData, &tlvType );
      
      if ( tlvType != 5 || fragLen < 26 )
      {
        CHECKED_FREE( tlvData );
        debugf( "I received an instant message that I didn't know how to deal with:\n" );
        printData();
        return 0;
      }
      
      // Word 1 = request type... 0=request, 1=cancel, 2=accept.
      // Next 4 words are the tick counter, which is highly irrelevant.
      // Next 16 bytes are the class ID that this data pertains to.  I'll
      //  check it against whichever ones I happen to recognize and support
      //  in this version of the code.  So far, I support chat and plain text.
      
      extraInfo->invitationText = NULL;
      extraInfo->chatroomName = NULL;
      extraInfo->requestType = *((unsigned short *) tlvData);
      classID = tlvData + 10;
      fragPos = 26;
      
      debugf( "Channel 2 instant message received.\n" );
      printData();
      
      while ( fragPos < fragLen )
      {
        shortPtr = (unsigned short *)(tlvData + fragPos);
        tmpShort = SWAP2( *shortPtr );
        fragPos += 2;
        switch ( tmpShort )
        {
          case 12:
          {
            if ( memcmp( classID, chatClass, 16 ) == 0 )
            {
              tmpShort = SWAP2( shortPtr[1] );
              fragPos += 2;
              CHECKED_MALLOC( tmpShort + 1, extraInfo->invitationText );
              memcpy( extraInfo->invitationText, (char *) (shortPtr + 2),
               tmpShort );
              extraInfo->invitationText[tmpShort] = 0;
              debugf( "Buddy chat invitation was received: %s\n",
               extraInfo->invitationText );
              fragPos += tmpShort;
            }
          }
          break;
          case 4:
          {
            unsigned char *ipPtr;
            tmpShort = SWAP2( shortPtr[1] );
            fragPos += 2;
            if ( tmpShort == 4 )
            {
              ipPtr = (unsigned char *) (shortPtr + 2);
              debugf( "Channel 2 instant message external IP address = %d.%d.%d.%d\n",
               ipPtr[0], ipPtr[1], ipPtr[2], ipPtr[3] );
            }
            fragPos += tmpShort;
          }
          break;
          case 5:
          {
            tmpShort = SWAP2( shortPtr[1] );
            fragPos += 2;
            if ( tmpShort == 2 )
            {
              tmpShort = SWAP2( shortPtr[2] );
              debugf( "Channel 2 instant message listening port = %d\n",
               tmpShort );
              fragPos += 2;
            } else {
              fragPos += tmpShort;
            }
          }
          break;
          case 0x2711:
          {
            if ( memcmp( plainTextClass, classID, 16 ) == 0 )
            {
              char *messagePtr;
              tmpShort = SWAP2( shortPtr[1] );
              fragPos += 2;
              messagePtr = (char *) (shortPtr + 2); 
              if ( tmpShort )
              {
                CHECKED_MALLOC( tmpShort + 1, *theMessage );
                memcpy( *theMessage, messagePtr, tmpShort );
                (*theMessage)[tmpShort] = 0;
                fragPos += tmpShort;
              }
            } else if ( memcmp( chatClass, classID, 16 ) == 0 )
            {
              tmpShort = SWAP2( shortPtr[1] );
              fragPos += 2;
              if ( tmpShort < 5 || fragPos + 5 < fragLen )
              {
                debugf( "A chat request was received, but could not be interpreted." );
                printData();
                fragPos += tmpShort;
                if ( extraInfo->invitationText )
                {
                  CHECKED_FREE( extraInfo->invitationText );
                  extraInfo->invitationText = NULL;
                }
                continue;
              }
              extraInfo->chatExchange = SWAP2( shortPtr[2] );
              fragPos += 2;
              tmpChar = *((unsigned char *)(shortPtr + 3));
              fragPos++;
              if ( tmpChar + fragPos + 2 < fragLen )
              {
                debugf( "A chat request was received, but could not be interpreted." );
                printData();
                fragPos += tmpShort;
                if ( extraInfo->invitationText )
                {
                  CHECKED_FREE( extraInfo->invitationText );
                  extraInfo->invitationText = NULL;
                }
                continue;
              }
              CHECKED_MALLOC( tmpChar + 1, extraInfo->chatroomName );
              memcpy( extraInfo->chatroomName, tlvData + fragPos, tmpChar );
              extraInfo->chatroomName[tmpChar] = 0;
              fragPos += tmpChar;
              extraInfo->chatInstance =
               SWAP2( *((unsigned short *) (tlvData + fragPos) ) );
              fragPos += 2;
              extraInfo->rendevousType = RENDEVOUS_TYPE_CHAT;
              debugf( "Chat request received successfully.\n" );
              debugf( "Chat room name: %s\n", extraInfo->chatroomName );
              debugf( "Chat instance: %d\n", extraInfo->chatInstance );
            } else {
              debugf( "Unsupported class ID specified in IM.\n" );
              printData();
            }
          }
          break;
          default:
          {
            debugf( "Skipping unknown TLV type %d.\n", tmpShort );
            tmpShort = SWAP2( shortPtr[1] );
            fragPos += tmpShort + 2;
          }
        }
      }
      CHECKED_FREE( tlvData );
    }
    break;
  }
  
  return 1;
}

int OscarData :: isSSIlimits( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 0x13 || subType != 3 ) return 0;
  
  return 1;
}

int OscarData :: isServerPause( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 0xb ) return 0;
  
  return 1;
}

int OscarData :: isServerResume( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 0xd ) return 0;
  
  return 1;
}

int OscarData :: isServerMigrate( char **IPaddress, unsigned char **cookie,
 unsigned short *cookieLen )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  unsigned short aFamilies, tlvType, length;
  unsigned short *shortPtr;
  int totLen = position;
  unsigned char *theTLVstring;
  
  *IPaddress = NULL;
  *cookie = NULL;
  *cookieLen = 0;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;
  
  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 0x12 ) return 0;
  
  debugf( "Migration message received.  If you disconnect as a result of a\n" );
  debugf( "migration message, please send me your logs so I can debug the\n" );
  debugf( "problem.  Debug information follows:\n" );
  printData();
  
  shortPtr = (unsigned short *)(theSNAC+1);
  
  // Docs say that what follows the SNAC should be the number of
  //  families, but in practice it does not appear to be the case.
  //  It looks more like a 0-terminated list of families, so I will
  //  try to interpret it this way.
  
  position = sizeof( FLAP ) + sizeof( SNAC );
  
  do
  {
    aFamilies = SWAP2( *shortPtr );
    shortPtr++;
    position += 2;
  } while ( aFamilies && position < totLen );
  
  if ( position >= totLen )
  {
    debugf( "Unable to correctly parse server migration message:\n" );
    position = totLen;
    printData();
    return 0;
  }
  
  // Assume any migration is a complete migration at this point.
  // This is oversimplified and will have to be changed later for
  //  better robustness.
  
  while ( (length = parseTLV( &theTLVstring, &tlvType )) != 0 )
  {
    switch ( tlvType )
    {
      case 5:
        if ( !length )
        {
          debugf( "Empty server IP address string in migration message!\n" );
          if ( *cookie )
          {
            CHECKED_FREE( *cookie );
            *cookie = NULL;
          }
          return 0;
        }
        *IPaddress = (char *)theTLVstring;
      break;
      case 6:
        if ( !length )
        {
          debugf( "Empty authentication cookie in migration message!\n" );
          if ( *IPaddress )
          {
            CHECKED_FREE( *IPaddress );
            *IPaddress = NULL;
          }
          return 0;
        }
        *cookie = theTLVstring;
        *cookieLen = length;
      break;
      default:
        debugf( "Unexpected TLV type (%d) in server migration message:\n",
         tlvType );
        printData();
    }
  }
  return 1;
}

int OscarData :: isServerRateWarning( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 1 || subType != 0xa ) return 0;
  
  // The documentation I have on this message doesn't line up with
  //  what I'm seeing from the server.  The AOL server sends me 6
  //  bytes before the expected structure (message type, rate class,
  //  window size, clear level, ...).  Interestingly enough, the
  //  first word is 0x0006.
  
  return 1;
}

int OscarData :: isTypingNotification( char **screenName )
{
  SNAC *theSNAC;
  char *retName;
  unsigned short family, subType, noteCode;
  unsigned char snLen;
  
  *screenName = NULL;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 4 || subType != 0x14 ) return 0;
  
  if ( position < sizeof( SNAC ) + 11 )
  {
    debugf( "Unable to interpret mini typing notification message:\n" );
    printData();
    return 0;
  }
  
  snLen = *(((unsigned char *)(theSNAC + 1)) + 10);
  
  if ( !snLen )
  {
    debugf( "Unable to interpret mini typing notification message:\n" );
    printData();
    return 0;
  }
  
  CHECKED_MALLOC( snLen + 1, retName );
  (*screenName) = retName;
  strncpy( *screenName, ((char *)(theSNAC + 1)) + 11, snLen );
  (*screenName)[snLen] = 0;
  
  noteCode = *((unsigned short *)(((char *)(theSNAC + 1)) + 11 + snLen));
  return SWAP2( noteCode ) + 1;
}

int OscarData :: isMessageAck( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  char *tmpStr, *uName;
  char len;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 4 || subType != 0xc ) return 0;
  
  if ( position < sizeof( SNAC ) + 11 ) return 0;
  
  tmpStr = ((char *) (theSNAC + 1)) + 10;
  len = *tmpStr;
  tmpStr++;
  
  if ( position < sizeof( SNAC ) + 11 + len ) return 0;
  
  CHECKED_MALLOC( len + 1, uName );
  strncpy( uName, tmpStr, len );
  uName[ len ] = 0;
  
  debugf( "Message acknowledgement received from %s.\n", uName );
  
  CHECKED_FREE( uName );
  
  return 1;
}

int OscarData :: isBuddyManagementLimits( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 3 || subType != 3 ) return 0;
  
  // Not using this information, but it's good to make sure that it is the
  //  message we expect.
  
  return 1;
}

int OscarData :: isPrivacyLimits( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 9 || subType != 3 ) return 0;
  
  // Not using this information, but it's good to make sure that it is the
  //  message we expect.
  
  return 1;
}

int OscarData :: isICBMparams( void )
{
  SNAC *theSNAC;
  unsigned short family, subType;
  
  if ( ptr->startByte != 0x2a || ptr->channel != 2 ) return 0;
  if ( position < sizeof( SNAC ) ) return 0;

  theSNAC = (SNAC *)(ptr + 1);  // Just past FLAP headers
  family = SWAP2( theSNAC->family );
  subType = SWAP2( theSNAC->subType );
  
  if ( family != 4 || subType != 5 ) return 0;
  
  // Not using this information, but it's good to make sure that it is the
  //  message we expect.
  
  return 1;
}
