#include <string.h>
#include <malloc.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include "oscarprotocol.h"
#include "oscardata.h"
#include "compatibility.h"

#define FREE_ALLOCD_STRING( theString ) \
  if ( theString ) { CHECKED_FREE( theString ); theString = NULL; }

AuthResponseData :: AuthResponseData()
{
  screenName = NULL;
  BOSserver = NULL;
  authCookieLength = 0;
  authCookie = NULL;
  emailAddress = NULL;
  registrationStatus = 0;
  passwordURL = NULL;
  errorCode = 0;
  errorURL = NULL;
  authKey = NULL;
  authKeyLen = 0;
}

void AuthResponseData :: clear( void )
{
  FREE_ALLOCD_STRING( screenName );
  FREE_ALLOCD_STRING( BOSserver );
  FREE_ALLOCD_STRING( authCookie );
  FREE_ALLOCD_STRING( emailAddress );
  FREE_ALLOCD_STRING( passwordURL );
  FREE_ALLOCD_STRING( errorURL );
  FREE_ALLOCD_STRING( authKey );
  screenName = NULL;
  BOSserver = NULL;
  authCookieLength = 0;
  authCookie = NULL;
  emailAddress = NULL;
  registrationStatus = 0;
  passwordURL = NULL;
  errorCode = 0;
  errorURL = NULL;
  authKey = NULL;
  authKeyLen = 0;
}

AuthResponseData :: ~AuthResponseData()
{
  FREE_ALLOCD_STRING( screenName );
  FREE_ALLOCD_STRING( BOSserver );
  FREE_ALLOCD_STRING( authCookie );
  FREE_ALLOCD_STRING( emailAddress );
  FREE_ALLOCD_STRING( passwordURL );
  FREE_ALLOCD_STRING( errorURL );
  FREE_ALLOCD_STRING( authKey );
}

void AuthResponseData :: printData( void )
{
  if ( !debugOutputStream ) return;
  
  fprintf( debugOutputStream, "\n" );
  fprintf( debugOutputStream, "Authorization Response Data\n" );
  fprintf( debugOutputStream, "===========================\n" );
  if ( screenName )
    fprintf( debugOutputStream, "Screen name:                  %s\n", screenName );
  if ( BOSserver )
    fprintf( debugOutputStream, "BOS server:                   %s\n", BOSserver );
  if ( authKeyLen )
    fprintf( debugOutputStream, "MD5 Authorization key length: %d\n", authCookieLength );
  if ( authCookieLength )
    fprintf( debugOutputStream, "Authorization cookie length:  %d\n", authCookieLength );
  if ( emailAddress )
    fprintf( debugOutputStream, "E-mail address:               %s\n", emailAddress );
  if ( registrationStatus )
    fprintf( debugOutputStream, "Registration status:          %d\n", registrationStatus );
  if ( passwordURL )
    fprintf( debugOutputStream, "Password change URL:          %s\n", passwordURL );
  if ( errorCode )
    fprintf( debugOutputStream, "Error code:                   %d\n", errorCode );
  if ( errorURL )
    fprintf( debugOutputStream, "Error message URL:            %s\n", errorURL );
  fprintf( debugOutputStream, "===========================\n\n" );
}

UserInformation :: UserInformation()
{
  statusUninit = 1;
  screenName = NULL;
  warningLevel = 0;
  userClass = 0;
  userStatus = 0;
  signupDate = (time_t)0;
  signonDate = (time_t)0;
  memberSinceDate = (time_t)0;
  idleMinutes = 0;
  ipAddress[0] = 0; ipAddress[1] = 0; ipAddress[2] = 0; ipAddress[3] = 0;
  sessionLen = 0;
  buddyIconChecksum = NULL;
  buddyIconChecksumLen = 0;
  statusMessage = NULL;
  statusMessageLen = 0;
  statusMessageEncoding = NULL;
  statusMessageEncodingLen = 0;
  clientProfile = NULL;
  clientProfileLen = 0;
  clientProfileEncoding = NULL;
  clientProfileEncodingLen = 0;
  awayMessage = NULL;
  awayMessageLen = 0;
  awayMessageEncoding = NULL;
  awayMessageEncodingLen = 0;
  isOnline = 0;
}

void UserInformation :: clear( void )
{
  FREE_ALLOCD_STRING( screenName );
  FREE_ALLOCD_STRING( buddyIconChecksum );
  FREE_ALLOCD_STRING( statusMessage );
  FREE_ALLOCD_STRING( statusMessageEncoding );
  FREE_ALLOCD_STRING( clientProfile );
  FREE_ALLOCD_STRING( clientProfileEncoding );
  FREE_ALLOCD_STRING( awayMessage );
  FREE_ALLOCD_STRING( awayMessageEncoding );
  statusUninit = 1;
  screenName = NULL;
  warningLevel = 0;
  userClass = 0;
  userStatus = 0;
  signupDate = (time_t)0;
  signonDate = (time_t)0;
  memberSinceDate = (time_t)0;
  idleMinutes = 0;
  ipAddress[0] = 0; ipAddress[1] = 0; ipAddress[2] = 0; ipAddress[3] = 0;
  sessionLen = 0;
  buddyIconChecksumLen = 0;
  statusMessageLen = 0;
  statusMessageEncodingLen = 0;
  clientProfileLen = 0;
  clientProfileEncodingLen = 0;
  awayMessageLen = 0;
  awayMessageEncodingLen = 0;
  isOnline = 0;
}

UserInformation :: UserInformation( const UserInformation &copyMe )
{
  if ( copyMe.screenName )
  {
    CHECKED_MALLOC( strlen( copyMe.screenName ) + 1, screenName );
    strcpy( screenName, copyMe.screenName );
  } else {
    screenName = NULL;
  }
  statusUninit = 0;
  warningLevel = copyMe.warningLevel;
  userClass = copyMe.userClass;
  userStatus = copyMe.userStatus;
  signupDate = copyMe.signupDate;
  signonDate = copyMe.signonDate;
  memberSinceDate = copyMe.memberSinceDate;
  idleMinutes = copyMe.idleMinutes;
  ipAddress[0] = copyMe.ipAddress[0];
  ipAddress[1] = copyMe.ipAddress[1];
  ipAddress[2] = copyMe.ipAddress[2];
  ipAddress[3] = copyMe.ipAddress[3];
  sessionLen = copyMe.sessionLen;
  if ( copyMe.buddyIconChecksumLen )
  {
    CHECKED_MALLOC( copyMe.buddyIconChecksumLen, buddyIconChecksum );
    buddyIconChecksumLen = copyMe.buddyIconChecksumLen;
    memcpy( buddyIconChecksum, copyMe.buddyIconChecksum, buddyIconChecksumLen );
  } else {
    buddyIconChecksumLen = 0;
    buddyIconChecksum = NULL;
  }
  if ( copyMe.statusMessageLen )
  {
    CHECKED_MALLOC( copyMe.statusMessageLen + 1, statusMessage );
    statusMessageLen = copyMe.statusMessageLen;
    strcpy( statusMessage, copyMe.statusMessage );
  } else {
    statusMessageLen = 0;
    statusMessage = NULL;
  }
  if ( copyMe.statusMessageEncodingLen )
  {
    CHECKED_MALLOC( copyMe.statusMessageEncodingLen + 1, statusMessageEncoding );
    statusMessageEncodingLen = copyMe.statusMessageEncodingLen;
    strcpy( statusMessageEncoding, copyMe.statusMessageEncoding );
  } else {
    statusMessageEncodingLen = 0;
    statusMessageEncoding = NULL;
  }
  if ( copyMe.clientProfileLen )
  {
    CHECKED_MALLOC( copyMe.clientProfileLen + 1, clientProfile );
    clientProfileLen = copyMe.clientProfileLen;
    strcpy( clientProfile, copyMe.clientProfile );
  } else {
    clientProfileLen = 0;
    clientProfile = NULL;
  }
  if ( copyMe.clientProfileEncodingLen )
  {
    CHECKED_MALLOC( copyMe.clientProfileEncodingLen + 1, clientProfileEncoding );
    clientProfileEncodingLen = copyMe.clientProfileEncodingLen;
    strcpy( clientProfileEncoding, copyMe.clientProfileEncoding );
  } else {
    clientProfileEncodingLen = 0;
    clientProfileEncoding = NULL;
  }
  if ( copyMe.awayMessageLen )
  {
    CHECKED_MALLOC( copyMe.awayMessageLen + 1, awayMessage );
    awayMessageLen = copyMe.awayMessageLen;
    strcpy( awayMessage, copyMe.awayMessage );
  } else {
    awayMessageLen = 0;
    awayMessage = NULL;
  }
  if ( copyMe.awayMessageEncodingLen )
  {
    CHECKED_MALLOC( copyMe.awayMessageEncodingLen + 1, awayMessageEncoding );
    awayMessageEncodingLen = copyMe.awayMessageEncodingLen;
    strcpy( awayMessageEncoding, copyMe.awayMessageEncoding );
  } else {
    awayMessageEncodingLen = 0;
    awayMessageEncoding = NULL;
  }
  isOnline = copyMe.isOnline;
}

void UserInformation :: incorporateData( UserInformation &copyMe )
{
  statusUninit = 0;
  
  if ( !screenName && copyMe.screenName )
  {
    CHECKED_MALLOC( strlen( copyMe.screenName ) + 1, screenName );
    strcpy( screenName, copyMe.screenName );
  }
  
  if ( copyMe.warningLevel )
    warningLevel = copyMe.warningLevel;
  
  if ( copyMe.userClass )
    userClass = copyMe.userClass;
  
  if ( copyMe.userStatus )
    userStatus = copyMe.userStatus;
  
  if ( copyMe.signupDate )
    signupDate = copyMe.signupDate;
  
  if ( copyMe.signonDate )
    signonDate = copyMe.signonDate;
  
  if ( copyMe.memberSinceDate )
    memberSinceDate = copyMe.memberSinceDate;
  
  idleMinutes = copyMe.idleMinutes;
  // Always copy this so we can sense when a person re-activates after idling
  
  if ( !(userStatus & 1) && !(userClass & 32) && awayMessageLen )
  {
    FREE_ALLOCD_STRING( awayMessage );
    awayMessageLen = 0;
    FREE_ALLOCD_STRING( awayMessageEncoding );
    awayMessageEncodingLen = 0;
  }
  
  if ( copyMe.ipAddress[0] )
  {
    ipAddress[0] = copyMe.ipAddress[0];
    ipAddress[1] = copyMe.ipAddress[1];
    ipAddress[2] = copyMe.ipAddress[2];
    ipAddress[3] = copyMe.ipAddress[3];
  }
  
  if ( copyMe.sessionLen )
    sessionLen = copyMe.sessionLen;
  
  if ( copyMe.buddyIconChecksumLen )
  {
    FREE_ALLOCD_STRING( buddyIconChecksum );
    CHECKED_MALLOC( copyMe.buddyIconChecksumLen + 1, buddyIconChecksum );
    buddyIconChecksumLen = copyMe.buddyIconChecksumLen;
    memcpy( buddyIconChecksum, copyMe.buddyIconChecksum, buddyIconChecksumLen );
  }
  
  if ( copyMe.statusMessageLen )
  {
    FREE_ALLOCD_STRING( statusMessage );
    CHECKED_MALLOC( copyMe.statusMessageLen + 1, statusMessage );
    statusMessageLen = copyMe.statusMessageLen;
    strcpy( statusMessage, copyMe.statusMessage );
  }
  
  if ( copyMe.statusMessageEncodingLen )
  {
    FREE_ALLOCD_STRING( statusMessageEncoding );
    CHECKED_MALLOC( copyMe.statusMessageEncodingLen + 1, statusMessageEncoding );
    statusMessageEncodingLen = copyMe.statusMessageEncodingLen;
    strcpy( statusMessageEncoding, copyMe.statusMessageEncoding );
  }
  
  if ( copyMe.clientProfileLen )
  {
    FREE_ALLOCD_STRING( clientProfile );
    CHECKED_MALLOC( copyMe.clientProfileLen + 1, clientProfile );
    clientProfileLen = copyMe.clientProfileLen;
    strcpy( clientProfile, copyMe.clientProfile );
    if ( awayMessageLen )
    {
      FREE_ALLOCD_STRING( awayMessage );
      awayMessageLen = 0;
    }
  }
  
  if ( copyMe.clientProfileEncodingLen )
  {
    FREE_ALLOCD_STRING( clientProfileEncoding );
    CHECKED_MALLOC( copyMe.clientProfileEncodingLen + 1, clientProfileEncoding );
    clientProfileEncodingLen = copyMe.clientProfileEncodingLen;
    strcpy( clientProfileEncoding, copyMe.clientProfileEncoding );
    if ( awayMessageEncodingLen )
    {
      FREE_ALLOCD_STRING( awayMessageEncoding );
      awayMessageEncodingLen = 0;
    }
  }
  
  if ( copyMe.awayMessageLen )
  {
    FREE_ALLOCD_STRING( awayMessage );
    CHECKED_MALLOC( copyMe.awayMessageLen + 1, awayMessage );
    awayMessageLen = copyMe.awayMessageLen;
    strcpy( awayMessage, copyMe.awayMessage );
    if ( clientProfileLen )
    {
      FREE_ALLOCD_STRING( clientProfile );
      clientProfileLen = 0;
    }
  }
  
  if ( copyMe.awayMessageEncodingLen )
  {
    FREE_ALLOCD_STRING( awayMessageEncoding );
    CHECKED_MALLOC( copyMe.awayMessageEncodingLen + 1, awayMessageEncoding );
    awayMessageEncodingLen = copyMe.awayMessageEncodingLen;
    strcpy( awayMessageEncoding, copyMe.awayMessageEncoding );
    if ( clientProfileEncodingLen )
    {
      FREE_ALLOCD_STRING( clientProfileEncoding );
      clientProfileEncodingLen = 0;
    }
  }
  
  isOnline = copyMe.isOnline;
}

UserInformation :: ~UserInformation()
{
  FREE_ALLOCD_STRING( screenName );
  FREE_ALLOCD_STRING( buddyIconChecksum );
  FREE_ALLOCD_STRING( statusMessage );
  FREE_ALLOCD_STRING( statusMessageEncoding );
  FREE_ALLOCD_STRING( clientProfile );
  FREE_ALLOCD_STRING( clientProfileEncoding );
  FREE_ALLOCD_STRING( awayMessage );
  FREE_ALLOCD_STRING( awayMessageEncoding );
}

#define PRINT_USERCLASS_LINE( bit, classString )                       \
    if ( tmpLong & bit )                                               \
    {                                                                  \
      fprintf( debugOutputStream, classString );                       \
      tmpLong &= ~bit;                                                 \
      if ( tmpLong ) fprintf( debugOutputStream, "                " ); \
    }


void UserInformation :: printData( void )
{
  unsigned long tmpLong;
  
  if ( !debugOutputStream ) return;
  
  fprintf( debugOutputStream, "\n" );
  fprintf( debugOutputStream, "User Information Data\n" );
  fprintf( debugOutputStream, "=====================\n" );
  if ( screenName )
    fprintf( debugOutputStream, "Screen name:    %s\n", screenName );
  if ( warningLevel )
    fprintf( debugOutputStream, "Warning level:  %d\n", warningLevel );
  if ( userClass )
  {
    tmpLong = userClass & 4095;
    
    fprintf( debugOutputStream, "User class:     " );
    PRINT_USERCLASS_LINE( 1, "Trial (user less than 60 days)\n" );
    PRINT_USERCLASS_LINE( 2, "Administrator\n" );
    PRINT_USERCLASS_LINE( 4, "AOL Lemming\n" );
    PRINT_USERCLASS_LINE( 8, "Commercial Account\n" );
    PRINT_USERCLASS_LINE( 16, "Free Account\n" );
    PRINT_USERCLASS_LINE( 32, "Away\n" );
    PRINT_USERCLASS_LINE( 64, "ICQ User\n" );
    PRINT_USERCLASS_LINE( 128, "Wireless User\n" );
    PRINT_USERCLASS_LINE( 256, "UNKNOWN USER CLASS (0x100)\n" );
    PRINT_USERCLASS_LINE( 512, "UNKNOWN USER CLASS (0x200)\n" );
    PRINT_USERCLASS_LINE( 1024, "ActiveBuddy Account\n" );
    PRINT_USERCLASS_LINE( 2048, "UNKNOWN USER CLASS (0x800)\n" );
  }
  if ( userStatus )
  {
    tmpLong = userStatus & 0x312b0136; // Only the bits I recognize
    
    fprintf( debugOutputStream, "User status:    " );
    PRINT_USERCLASS_LINE( 0x00000001, "Away\n" );
    PRINT_USERCLASS_LINE( 0x00000002, "Do Not Disturb\n" );
    PRINT_USERCLASS_LINE( 0x00000004, "Not Available\n" );
    PRINT_USERCLASS_LINE( 0x00000010, "Occupied\n" );
    PRINT_USERCLASS_LINE( 0x00000020, "Free For Chat\n" );
    PRINT_USERCLASS_LINE( 0x00000100, "Invisible\n" );
    PRINT_USERCLASS_LINE( 0x00010000, "Web Aware\n" );
    PRINT_USERCLASS_LINE( 0x00020000, "Visible IP\n" );
    PRINT_USERCLASS_LINE( 0x00080000, "Birthday\n" );
    PRINT_USERCLASS_LINE( 0x00200000, "Web-front\n" );
    PRINT_USERCLASS_LINE( 0x01000000, "Direct Connection disabled\n" );
    PRINT_USERCLASS_LINE( 0x10000000, "Direct Connection on authorization\n" );
    PRINT_USERCLASS_LINE( 0x20000000, "Direct Connection for contacts\n" );
  }
  
  if ( signupDate )
    fprintf( debugOutputStream, "Signup date:    %s", ctime( &signupDate ) );
  if ( signonDate )
    fprintf( debugOutputStream, "Signon date:    %s", ctime( &signonDate ) );
  if ( memberSinceDate )
    fprintf( debugOutputStream, "Member since:   %s",
     ctime( &memberSinceDate ) );
  if ( idleMinutes )
    fprintf( debugOutputStream, "Idle time:      %ld minutes\n",
     idleMinutes );
  if ( ipAddress[0] )
    fprintf( debugOutputStream, "IP address:     %d.%d.%d.%d\n", 
     ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3] );
  if ( sessionLen )
    fprintf( debugOutputStream, "Session length: %ld\n", sessionLen );
  if ( statusMessageLen )
    fprintf( debugOutputStream, "Status message: %s\n", statusMessage );
  if ( statusMessageEncodingLen )
    fprintf( debugOutputStream, "Status message encoding: %s\n",
     statusMessageEncoding );
  if ( buddyIconChecksumLen )
    fprintf( debugOutputStream, "Received buddy icon checksum (%d bytes).\n",
     buddyIconChecksumLen );
  if ( clientProfileLen )
    fprintf( debugOutputStream, "Client profile:          %s\n",
     clientProfile );
  if ( clientProfileEncodingLen )
    fprintf( debugOutputStream, "Client profile encoding: %s\n",
     clientProfileEncoding );
  if ( awayMessageLen )
    fprintf( debugOutputStream, "Away message:            %s\n",
     awayMessage );
  if ( awayMessageEncoding )
    fprintf( debugOutputStream, "Away message encoding:   %s\n",
     awayMessageEncoding );
  fprintf( debugOutputStream, "=====================\n\n" );
}

#define STRCAT( stringToAdd ) \
{ \
  addStrLen = strlen( stringToAdd ); \
  if ( addStrLen ) \
  { \
    if ( (strLen + addStrLen + 1) > allocLen ) \
    { \
      while ( (strLen + addStrLen + 1) > allocLen ) allocLen *= 2; \
      CHECKED_REALLOC( buffer, allocLen, buffer ); \
    } \
    strcpy( buffer + strLen, (char *)stringToAdd ); \
    strLen += addStrLen; \
  } \
}

char *stripTags( char *origString )
{
  char *retString;
  int i, editStart, len;
  
  len = strlen( origString );
  CHECKED_MALLOC( len + 1, retString );
  strcpy( retString, origString );
  
  for ( i=0; i < len; ++i )
  {
    if ( i <= (len - 4) && strnicmp( retString + i, "&gt;", 4 ) == 0 )
    {
      retString[i] = '>';
      strcpy( retString + i + 1, retString + i + 4 );
      len -= 3;
      continue;
    }
    // Replace &gt; with a greater than sign
    
    if ( i <= (len - 4) && strnicmp( retString + i, "&lt;", 4 ) == 0 )
    {
      retString[i] = '<';
      strcpy( retString + i + 1, retString + i + 4 );
      len -= 3;
      continue;
    }
    // Replace &lt; with a less than sign
    
    if ( i <= (len - 6) && strnicmp( retString + i, "&quot;", 6 ) == 0 )
    {
      retString[i] = '"';
      strcpy( retString + i + 1, retString + i + 6 );
      len -= 5;
      continue;
    }
    // Replace &quot; with a quote
    
    if ( i <= (len - 5) && strnicmp( retString + i, "&amp;", 5 ) == 0 )
    {
      retString[i] = '&';
      strcpy( retString + i + 1, retString + i + 5 );
      len -= 4;
      continue;
    }
    // Replace &amp; with ampersand
    
    if ( retString[i] == 10 || retString[i] == 13 )
    {
      strcpy( retString + i, retString + i + 1 );
      len--;
      i--;
      continue;
    }
    // Eat any actual CR/LF characters, since only <br>s should work.
    
    if ( retString[i] == '<' )
    {
      editStart = i;
      while ( i < len && retString[i] != '>' ) ++i;
      if ( retString[i] == '>' )
      {
        retString[i] = 0;
        if ( stricmp( retString + editStart + 1, "br" ) == 0 )
        {
          retString[editStart] = '\n';
          editStart++;
          len++;
        }
        // Replace <BR> tag with a \n
        
        strcpy( retString + editStart, retString + i + 1 );
        len -= (i - editStart) + 1;
        i = editStart - 1;
        continue;
      } else {
        retString[editStart] = 0;
        return retString;
      }
    }
  }
  return retString;
}

#define STRINGCAT( newString ) \
{ \
  tmpLen = strlen( newString ) + 1; \
  if ( tmpLen + curPos >= retStringLen ) \
  { \
    retStringLen *= 2; \
    CHECKED_REALLOC( retString, retStringLen, retString ); \
  } \
  strcat( retString, newString ); \
  curPos += tmpLen - 1; \
}

char *convertTextToAOL( char *origString )
{
  char *retString;
  char tmpStr[2];
  int tmpLen, retStringLen, len, i, curPos;
  
  curPos = 0;
  len = strlen( origString );
  CHECKED_MALLOC( len + 1, retString );
  retString[0] = 0;
  retStringLen = len + 1;
  
  for ( i=0; i<len; ++i )
  {
    switch ( origString[i] )
    {
      case '<':  STRINGCAT( "&lt;" ); break;
      case '>':  STRINGCAT( "&gt;" ); break;
      case '"':  STRINGCAT( "&quot;" ); break;
      case '&':  STRINGCAT( "&amp;" ); break;
      case '\n': STRINGCAT( "<br>" ); break;
      default:
      {
        tmpStr[0] = origString[i];
        tmpStr[1] = 0;
        STRINGCAT( tmpStr );
      }
    }
  }
  return retString;
}

char *convertTextToRTV( char *origString )
{
  char *retString;
  char tmpStr[2];
  int tmpLen, retStringLen, len, i, curPos;
  
  curPos = 0;
  len = strlen( origString );
  CHECKED_MALLOC( len + 1, retString );
  retString[0] = 0;
  retStringLen = len + 1;
  
  for ( i=0; i<len; ++i )
  {
    switch ( origString[i] )
    {
      case '<':  STRINGCAT( "<<" ); break;
      case '>':  STRINGCAT( ">>" ); break;
      default:
      {
        tmpStr[0] = origString[i];
        tmpStr[1] = 0;
        STRINGCAT( tmpStr );
      }
    }
  }
  return retString;
}

static const char *tagNoXlate[14] =
{
  "b", "/b", "i", "/i", "u", "/u", "tt", "/tt", "h1", "h2",
  "h3", "/h", "br", "\n"
};

char *convertTagsToRTV( char *origString )
{
  char *retString;
  int i, j, tagStart, tagLen, len, tmpLen, retStringLen, curPos;
  
  curPos = 0;
  len = strlen( origString );
  CHECKED_MALLOC( len + 1, retString );
  retString[0] = 0;
  retStringLen = len + 1;
  
  for ( i=0; i<len; ++i )
  {
    if ( i <= (len - 4) && strnicmp( origString + i, "&gt;", 4 ) == 0 )
    {
      STRINGCAT( ">>" );
      i += 3;
      continue;
    }
    // Replace &gt; with a great than sign
    
    if ( i <= (len - 4) && strnicmp( origString + i, "&lt;", 4 ) == 0 )
    {
      STRINGCAT( "<<" );
      i += 3;
      continue;
    }
    // Replace &lt; with a less than sign
    
    if ( i <= (len - 6) && strnicmp( origString + i, "&quot;", 6 ) == 0 )
    {
      STRINGCAT( "\"" );
      i += 5;
      continue;
    }
    // Replace &quot; with a quote
    
    if ( i <= (len - 5) && strnicmp( origString + i, "&amp;", 5 ) == 0 )
    {
      STRINGCAT( "&" );
      i += 4;
      continue;
    }
    // Replace &quot; with a quote
    
    if ( origString[i] == '<' )
    {
      tagStart = i + 1;
      while ( i < len && origString[i] != '>' ) ++i;
      tagLen = i - tagStart;
      
      if ( origString[i] == '>' )
      {
        for ( j=0; j<14; ++j )
        {
          if ( tagLen == strlen(tagNoXlate[j]) &&
                strnicmp( origString + tagStart, tagNoXlate[j], tagLen ) == 0 )
          {
            STRINGCAT( "<" );
            STRINGCAT( tagNoXlate[j] );
            STRINGCAT( ">" );
          }
        }
        if ( tagLen == 2 && strnicmp( origString + tagStart, "br", 2 ) == 0 )
          // Replace <BR> tag with a \n
          STRINGCAT( "\n" );
        
      } else {
        return retString;
      }
    } else {
      char nextChar[2];
      nextChar[0] = origString[i];
      nextChar[1] = 0;
      STRINGCAT( nextChar );
    }
  }
  return retString;
}

char *UserInformation :: getUserBlurbString( char *additionalInfo )
{
  char *buffer;
  char buffer2[16];
  int allocLen, strLen, addStrLen;
  
  allocLen = 128;
  CHECKED_MALLOC( 128, buffer );
  strLen = 0;
  
  if ( !screenName || !isOnline )
  {
    STRCAT( "Not signed on." );
    return buffer;
  }
  
  STRCAT( "Screen Name: " );
  STRCAT( screenName );
  
  if ( additionalInfo )
  {
    STRCAT( "\n" );
    STRCAT( additionalInfo );
  }
  
  if ( signonDate )
  {
    STRCAT( "\nSigned on: " );
    STRCAT( ctime( &signonDate ) );
    strLen--;
    buffer[ strLen ] = 0;
    // Hack off the newline
  }
  if ( idleMinutes )
  {
    STRCAT( "\nIdle (hours:mins): " );
    sprintf( buffer2, "%d:%02d", idleMinutes / 60, idleMinutes % 60 );
    STRCAT( buffer2 );
  }
  if ( awayMessageLen )
  {
    char *tmpString;
    STRCAT( "\nAway message:\n" );
    tmpString = stripTags( awayMessage );
    STRCAT( tmpString );
    CHECKED_FREE( tmpString );
  } else {
    if ( clientProfileLen )
    {
      char *tmpString;
      STRCAT( "\nUser profile:\n" );
      tmpString = stripTags( clientProfile );
      STRCAT( tmpString );
      CHECKED_FREE( tmpString );
    }
    if ( statusMessageLen )
    {
      char *tmpString;
      STRCAT( "\nStatus:\n" );
      tmpString = stripTags( statusMessage );
      STRCAT( tmpString );
      CHECKED_FREE( tmpString );
    }
  }
  return buffer;
}

SSIData :: SSIData()
{
  numRootBuddies = 0;
  rootBuddies = NULL;
  dateStamp = (time_t) 0;
}

SSIData :: ~SSIData()
{
  int i;
  
  for ( i=0; i<numRootBuddies; ++i )
  {
    if ( rootBuddies[i].entryName )
    {
      CHECKED_FREE( rootBuddies[i].entryName );
    }
    if ( rootBuddies[i].memberIDs )
    {
      CHECKED_FREE( rootBuddies[i].memberIDs );
    }
  }

  if ( numRootBuddies )  
    CHECKED_FREE( rootBuddies );
}

void SSIData :: printData( void )
{
  int i, j, k, needSkip;
  
  if ( !debugOutputStream ) return;
  
  fprintf( debugOutputStream, "\n" );
  fprintf( debugOutputStream, "Server Stored Information (Buddy List)\n" );
  fprintf( debugOutputStream, "======================================\n" );

  needSkip = 0;
  for ( i=0; i<numRootBuddies; ++i )
  {
    if ( rootBuddies[i].numMembers )
    {
      if ( needSkip )
      {
        fprintf( debugOutputStream, "\n" );
      } else needSkip = 1;
      
      rootBuddies[i].beenHere = 1;
      fprintf( debugOutputStream, "Group Name:    %s\n",
       rootBuddies[i].entryName );
      fprintf( debugOutputStream, "Group ID:      %d\n", rootBuddies[i].gid );
      fprintf( debugOutputStream, "Group Members: " );
      if ( !rootBuddies[i].numMembers )
      {
        fprintf( debugOutputStream, "<NONE>\n" );
      } else for ( j=0; j<rootBuddies[i].numMembers; ++j )
      {
        for ( k=0; k<numRootBuddies; ++k )
        {
          if ( rootBuddies[k].id == rootBuddies[i].memberIDs[j] &&
                rootBuddies[k].gid == rootBuddies[i].gid )
          {
            // Found the right entry
            if ( j )
            {
              fprintf( debugOutputStream, "               " );
            }
            fprintf( debugOutputStream, "%s\n", rootBuddies[k].entryName );
            rootBuddies[k].beenHere = 1;
          }
        }
      }
    }
  }
  
  for ( i=0; i<numRootBuddies; ++i )
  {
    if ( !rootBuddies[i].beenHere )
    {
      if ( needSkip )
      {
        needSkip = 0;
        fprintf( debugOutputStream, "\n" );
      }
      fprintf( debugOutputStream, "Orphan Member: %s\n",
       rootBuddies[i].entryName );
    } else {
      rootBuddies[i].beenHere = 0;
      // Reset the status to be neat about it
    }
  }
  
  if ( dateStamp )
  {
    if ( numRootBuddies )
      fprintf( debugOutputStream, "\n" );
    
    fprintf( debugOutputStream, "Last update:   %s", ctime( &dateStamp ) );
  }
  
  fprintf( debugOutputStream, "======================================\n\n" );
}

