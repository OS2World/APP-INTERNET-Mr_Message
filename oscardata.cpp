#define INCL_DOS
#define TCPV40HDRS

#include <ctype.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include <types.h>
#include <sys\socket.h>
}

#include "oscardata.h"
#include "compatibility.h"

FILE *debugOutputStream = NULL;

void announce_socket_error( char *message )
{
  int rc;
  rc = sock_errno();
  debugf( "%s: return code %d\n\n", message, rc );
}

OscarData :: OscarData()
{
  length = 0;
  position = 0;
  sequence = NULL;
  shuttingDown = 0;
  status = 0;
  ptr = NULL;
}

OscarData :: OscarData( int *sequenceNum )
{
  length = 0;
  position = 0;
  sequence = sequenceNum;
  shuttingDown = 0;
  status = 0;
  ptr = NULL;
}

OscarData :: ~OscarData()
{
  if ( ptr ) CHECKED_FREE( ptr );
  ptr = NULL;
  length = 0;
}

int OscarData :: getStatus( void )
{
  return status;
}

void OscarData :: reset( void )
{
  position = 0;
  shuttingDown = 0;
  status = 0;
}

void OscarData :: resizeAtLeast( int size )
{
  int newLength;
  FLAP *newPtr;
  
  if ( length < size )
  {
    if ( !length )
    {
      if ( size < STARTING_OSCARDATA_SIZE )
      {
        length = STARTING_OSCARDATA_SIZE;
      } else {
        length = size;
      }
      CHECKED_MALLOC( length, ptr );
    } else {
      newLength = length;
      while ( newLength < size ) newLength += length;
      
      CHECKED_REALLOC( ptr, newLength, newPtr );
      
      if ( !newPtr )
      {
        debugf( "Memory allocation failed.  Exitting.\n" );
        exit( 1 );
      }
      ptr = newPtr;
      length = newLength;
    }
  }
}

void OscarData :: addFLAP( FLAP *theFLAP )
{
  resizeAtLeast( position + sizeof( FLAP ) );
  memcpy( ((char *)ptr) + position, theFLAP, sizeof( FLAP ) );
  position += sizeof( FLAP );
}

void OscarData :: addFLAP( unsigned char channel )
{
  FLAP theFLAP;
  theFLAP.startByte = 0x2a;
  theFLAP.channel = channel;
  theFLAP.sequence = SWAP2( *sequence );
  theFLAP.dataLen = 0;
  addFLAP( &theFLAP );
}

void OscarData :: addSNAC( SNAC *theSNAC )
{
  SNAC *mySNAC = (SNAC *)(((char *)ptr) + position);
  
  resizeAtLeast( position + sizeof( SNAC ) );
  mySNAC->family = SWAP2( theSNAC->family );
  mySNAC->subType = SWAP2( theSNAC->subType );
  mySNAC->flags[0] = theSNAC->flags[0];
  mySNAC->flags[1] = theSNAC->flags[1];
  mySNAC->requestID = SWAP4( theSNAC->requestID );
  position += sizeof( SNAC );
}

void OscarData :: addTLV( int type, int length, const void *value )
{
  unsigned short *theTLV;
  resizeAtLeast( position + 4 + length );
  theTLV = (unsigned short *)(((char *)ptr) + position);
  theTLV[0] = SWAP2( type );
  theTLV[1] = SWAP2( length );
  if ( length )
  {
    memcpy( theTLV + 2, value, length );
  }
  position += 4 + length;
}

void OscarData :: addTLV( int type, const char *value )
{
  if ( value )
    addTLV( type, strlen( value ), value );
  else addTLV( type, 0, NULL );
}

void OscarData :: addData( const void *data, int dataLen )
{
  resizeAtLeast( position + dataLen );
  memcpy( ((char *)ptr) + position, data, dataLen );
  position += dataLen;
}

void OscarData :: tallyFLAPDataLen( void )
{
  unsigned short dataLen = (unsigned short) position -
   (unsigned short) sizeof( FLAP );
  ptr->dataLen = SWAP2( dataLen );
}

int OscarData :: parseTLV( unsigned char **TLVdata, unsigned short *tlvType )
{
  unsigned short *shortPtr =
   (unsigned short *) ((unsigned char *)ptr + position);
  unsigned short TLVlen, dataLen = SWAP2( ptr->dataLen );
  
  if ( position >= dataLen )
  {
    *TLVdata = NULL;
    *tlvType = 0;
    return 0;
  }
  
  *tlvType = SWAP2( shortPtr[0] );
  TLVlen = SWAP2( shortPtr[1] );
  
  if ( position + 4 + TLVlen - sizeof( FLAP ) > dataLen )
  {
    debugf( "Partial TLV was rejected.\n" );
    printData();
    *TLVdata = NULL;
    *tlvType = 0;
    return 0;
  }
  
  if ( TLVlen )
  {
    CHECKED_MALLOC( TLVlen + 1, *TLVdata );
    memcpy( *TLVdata, shortPtr + 2, TLVlen );
    (*TLVdata)[TLVlen] = 0;
    // throw in a NULL terminator byte to make string handling easier
  } else {
    *TLVdata = NULL;
  }
  
  position += 4 + TLVlen;
  
  return TLVlen;
}

void OscarData :: sendData( int socketHandle )
{
  int rc;
  
  DosEnterCritSec();
  ptr->sequence = SWAP2( *sequence );
  // Ensure that the most recent sequence number is used here
  
  rc = send( socketHandle, (char *)ptr, position, 0 );
  (*sequence)++;
  DosExitCritSec();
  
  // Don't want any other threads to "steal" our sequence ID resulting
  //  in the server receiving sequence IDs out of order (which is grounds
  //  for immediate disconnection by AOL).
  
  if ( rc < position )
  {
    debugf( "Send returned %d instead of the expected value of %d.\n",
     rc, position );
    if ( rc == -1 )
    {
      announce_socket_error( "Send failed" );
      status = sock_errno();
    }
  }
}

void OscarData :: receiveData( int socketHandle )
{
  unsigned short dataLen;
  int rc, subtot;
  
  reset();
  resizeAtLeast( sizeof( FLAP ) );
  
  subtot = 0;
  do
  {
    rc = recv( socketHandle, ((char *)ptr) + subtot, sizeof( FLAP ), 0 );
    subtot += rc;
    debugf( "recv rc=%d, subtot=%d\n", rc, subtot );
  } while ( rc > 0 && subtot < sizeof( FLAP ) );
  
  if ( rc <= 0 )
  {
    if ( shuttingDown )
    {
      shuttingDown = 0;
      return;
    }
    // Socket closed in the process of shutting down.  No real problem.
    
    debugf( "Short/nonexistent FLAP received from server.  <boggle>  rc=%d\n",
     rc );
    announce_socket_error( "FLAP receive failed" );
    status = sock_errno();
    if ( status == 0 ) status--;
    // Need something to indicate an error
    return;
  }
  
  position += sizeof( FLAP );
  dataLen = SWAP2( ptr->dataLen );
  resizeAtLeast( dataLen + sizeof( FLAP ) );
  
  subtot = 0;
  do
  {
    rc = recv( socketHandle, ((char *)(ptr + 1)) + subtot, dataLen - subtot, 0 );
    subtot += rc;
    debugf( "recv rc=%d, subtot=%d, need %d\n", rc, subtot, dataLen );
  } while ( rc > 0 && subtot < dataLen );
  
  if ( rc <= 0 )
  {
    if ( shuttingDown ) return;
    // Socket closed in the process of shutting down.  No real problem.
    
    debugf( "Short/nonexistent SNAC/data received from server.  <boggle>  rc=%d\n",
     rc );
    position += rc;
    return;
  }
  position += dataLen;
}

void OscarData :: printData( void )
{
  int i, j, maxData;
  unsigned char *raw;
  SNAC mySNAC;
  SNAC *theSNAC;
  
  if ( !debugOutputStream ) return;
  
  debugf( "OscarData.FLAP.startByte = %02x\n", ptr->startByte );
  debugf( "OscarData.FLAP.channel = %d\n", ptr->channel );
  debugf( "OscarData.FLAP.sequence = %d\n", SWAP2( ptr->sequence ) );
  debugf( "OscarData.FLAP.dataLen = %d\n", SWAP2( ptr->dataLen ) );
  
  raw = (unsigned char *)(ptr + 1);  // After FLAP structure
  
  maxData = position - sizeof( FLAP );
  
  if ( ptr->channel == 2 && maxData >= sizeof( SNAC ) )
  {
    // This will have a SNAC (I hope!)
    theSNAC = (SNAC *)raw;
    raw += sizeof( SNAC );
    mySNAC.family = SWAP2( theSNAC->family );
    mySNAC.subType = SWAP2( theSNAC->subType );
    mySNAC.flags[0] = theSNAC->flags[0];
    mySNAC.flags[1] = theSNAC->flags[1];
    mySNAC.requestID = SWAP4( theSNAC->requestID );
    
    debugf( "OscarData.SNAC.family = %d\n", mySNAC.family );
    debugf( "OscarData.SNAC.subType = 0x%02x\n", mySNAC.subType );
    debugf( "OscarData.SNAC.flags = 0x%02x%02x\n", mySNAC.flags[0],
     mySNAC.flags[1] );
    debugf( "OscarData.SNAC.requestID = 0x%04x\n", mySNAC.requestID );
    
    maxData -= sizeof( SNAC );
  }
  
  for ( i=0; i<maxData; ++i )
  {
    if ( !(i%16) )
    {
      fprintf( debugOutputStream, "\n" );
      debugf( "" );  // Print the thread id header
      for ( j=0; j<16 && (i+j) < maxData; ++j )
      {
        if ( isprint( raw[i+j] ) )
        {
          fprintf( debugOutputStream, "%c", raw[i+j] );
        } else {
          fprintf( debugOutputStream, "." );
        }
      }
      for ( ; j<16; ++j ) fprintf( debugOutputStream, " " );
      
      fprintf( debugOutputStream, " ==> " );
    }
    fprintf( debugOutputStream, "%02x ", raw[i] );
  }
  fprintf( debugOutputStream, "\n\n" );
}

void OscarData :: getSNACtype( unsigned short *family, unsigned short *subType )
{
  SNAC *theSNAC;
  
  if ( position < (sizeof( FLAP ) + sizeof( SNAC )) || ptr->channel != 2 )
  {
    // Only channel 2 FLAPs have SNACs
    *family = 0;
    *subType = 0;
    return;
  }
  theSNAC = (SNAC *) (ptr + 1);
  *family = SWAP2( theSNAC->family );
  *subType = SWAP2( theSNAC->subType );
}

