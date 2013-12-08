#define INCL_DOS
#define INCL_WIN
#include <os2.h>
#include <malloc.h>
#include <string.h>
#include "oscarsession.h"
#include "sessionmanager.h"

// Set the debugging options here
//
// A non-developmental release should have these both
//  not defined for a small performance improvement.
// #define MRM_DEBUG_MEMORY
#define MRM_DEBUG_PRINTF

#define MUX_DEBUG_ACQ debugf( "MUTEX ACQUIRED: %s line %d\n", __FILE__, __LINE__ );
#define MUX_DEBUG_REL debugf( "MUTEX RELEASED: %s line %d\n", __FILE__, __LINE__ );

#define SWAP4( theLong ) \
  (unsigned long)                                   \
   ((((unsigned long)theLong & 0xff000000ul)>>24) | \
    (((unsigned long)theLong & 0x00ff0000ul)>>8) |  \
    (((unsigned long)theLong & 0x0000ff00ul)<<8) |  \
    (((unsigned long)theLong & 0x000000fful)<<24))

#define SWAP2( theShort )                      \
  (unsigned short)                             \
   ((((unsigned short)theShort & 0xff00)>>8) | \
    (((unsigned short)theShort & 0x00ff)<<8))

#ifndef MRM_DEBUG_PRINTF
#define debugf()
#else
extern int debugf( char *, ... );
#endif

#ifndef MRM_DEBUG_MEMORY

#define CHECKED_FREE( ptr ) \
  free( ptr )
#define CHECKED_MALLOC( theSize, newPtr ) \
  *((void **)(&(newPtr))) = malloc( theSize )
#define CHECKED_REALLOC( ptr, theSize, newPtr ) \
  *((void **)(&(newPtr))) = realloc( ptr, theSize )
#define CHECKED_CALLOC( theSize, newPtr ) \
  *((void **)(&(newPtr))) = calloc( theSize, 1 )
#define CHECKED_STRDUP( theString, newPtr ) \
  *((char **)(&(newPtr))) = strdup( (const char *)(theString) );

#else

#define MRM_DEBUG_MAX_TRACERS 1048576
// Only keep a history of this many tracers

typedef struct
{
  const void *blockAddr;
  int allocLine;
  const char *allocModule;
  int freeLine;
  const char *freeModule;
  unsigned long timestamp, size;
} blockHistory;

extern blockHistory *memTracer;
extern int numMemTracers, allocMemTracers;

#define MRM_DEBUG_MEMCHECK_ISREALLOC 1
#define MRM_DEBUG_MEMCHECK_ISCALLOC  2

extern void CHECKED_FREE_TRACER( void *ptr, const char *file, int line,
 int flags );
extern void *CHECKED_MALLOC_TRACER( int size, const char *file, int line,
 int flags, void *reallocPtr );

#define CORRUPTION_REPORT( function, beforeAfter ) \
  debugf( "HEAP CORRUPTION DETECTED %s %s.\nFILE: %s\nLINE: %d\n\n", beforeAfter, function, __FILE__, __LINE__ ); \
  exit( 1 );
  
#define CHECKED_FREE( ptr ) \
{ \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "FREE", "PRIOR TO" ); } \
  CHECKED_FREE_TRACER( (void *)ptr, __FILE__, __LINE__, 0 ); \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "FREE", "AFTER" ); } \
}
    
#define CHECKED_MALLOC( theSize, newPtr ) \
{ \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "MALLOC", "PRIOR TO" ); } \
  *((void **)(&(newPtr))) = CHECKED_MALLOC_TRACER( theSize, __FILE__, \
   __LINE__, 0, NULL ); \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "MALLOC", "AFTER" ); } \
}

#define CHECKED_REALLOC( ptr, theSize, newPtr ) \
{ \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "REALLOC", "PRIOR TO" ); } \
  if ( ptr ) CHECKED_FREE_TRACER( (void *)ptr, __FILE__, __LINE__, \
   MRM_DEBUG_MEMCHECK_ISREALLOC ); \
  if ( theSize ) *((void **)(&(newPtr))) = CHECKED_MALLOC_TRACER( theSize, __FILE__, \
   __LINE__, MRM_DEBUG_MEMCHECK_ISREALLOC, ptr ); \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "REALLOC", "AFTER" ); } \
}

#define CHECKED_CALLOC( theSize, newPtr ) \
{ \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "CALLOC", "PRIOR TO" ); } \
  *((void **)(&(newPtr))) = CHECKED_MALLOC_TRACER( theSize, __FILE__, \
   __LINE__, MRM_DEBUG_MEMCHECK_ISCALLOC, NULL ); \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "CALLOC", "AFTER" ); } \
}

#define CHECKED_STRDUP( theString, newPtr ) \
{ \
  int theSize = strlen( theString ) + 1; \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "STRDUP", "PRIOR TO" ); } \
  *((void **)(&(newPtr))) = CHECKED_MALLOC_TRACER( theSize, __FILE__, \
   __LINE__, 0, NULL ); \
  if ( newPtr ) \
  { \
    strcpy( newPtr, (const char *)(theString) ); \
  } \
  if ( _heapchk() != _HEAPOK ) { CORRUPTION_REPORT( "STRDUP", "AFTER" ); } \
}

#endif

int initializeCompatibility( int initMMPM );
void deinitializeCompatibility( void );

void registerSessionManager( SessionManager *smgr );

int isMultiMediaCapable( void );
int isRexxCapable( void );

typedef struct
{
  const char *currentUser;
  const char *otherUser;
  const char *message;
} eventData;

int handleMrMessageEvent( UI_EventType eventType,
 sessionThreadSettings *specificSettings,
 sessionThreadSettings *globalSettings,
 eventData *theEventData, int noSound );

unsigned long testSettingsFlag( unsigned long theFlag,
 sessionThreadSettings *specificSettings,
 sessionThreadSettings *globalSettings );

void playMediaFile( const char *fileName );

ULONG getAudioVolume( void );
void setAudioVolume( ULONG volume );

