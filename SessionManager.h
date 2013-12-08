#ifndef SESSIONMANAGER_H_INCLUDED
#define SESSIONMANAGER_H_INCLUDED

#include "oscarsession.h"

#define WM_INITULONGS         WM_USER + 100
#define WM_SESSIONCLOSED      WM_USER + 101
#define WM_ERRORMESSAGE       WM_USER + 102
#define WM_WINPPCHANGED       WM_USER + 103
#define WM_GIMMEDROPCOPYDATA  WM_USER + 104
#define WM_CREATEBUDDYLIST    WM_USER + 105
#define WM_ADDBUDDY           WM_USER + 106
#define WM_REGISTERWAKEUPSEM  WM_USER + 107
#define WM_BUDDYSTATUS        WM_USER + 108
#define WM_IMRECEIVED         WM_USER + 109
#define WM_NOTIDLE            WM_USER + 110
#define WM_USERBOOTED         WM_USER + 111
#define WM_SESSIONPOSTINIT    WM_USER + 112
#define WM_PROGRESSREPORT     WM_USER + 113
#define WM_SET_MAXMIN_VAL     WM_USER + 114
#define WM_SET_CURRENT_VAL    WM_USER + 115
#define WM_ADD_TO_CURRENT_VAL WM_USER + 116
#define WM_TYPING_NOTIFY      WM_USER + 117
#define WM_REBOOTSESSION      WM_USER + 118
#define WM_SESSIONUP          WM_USER + 119
#define WM_POSTMESSAGE        WM_USER + 120


typedef struct
{
  char *userName, *password;
  char *server;
  int port;
  HWND managerWin;
  MINIRECORDCORE *record;
  OscarSession *theSession;
  ULONG threadID;
  sessionThreadSettings *settings;
  int entryFromIniFile;
} sessionThreadInfo;

typedef struct
{
  ULONG *numItemsPtr;
  sessionThreadInfo **threadInfoPtr;
  HMTX mutexSem;
  HINI iniFile;
  sessionThreadSettings *globalSettings;
} sessionManagerULONGs;

typedef struct
{
  char *userName, *password, *server;
  int port;
} sessionEditorInit;

typedef struct
{
  HEV wakeupSem;
  HWND buddyListWin;
  UserInformation *userInfo;
  OscarSession *theSession;
} buddyListCreateData;

class SessionManager
{
private:
  HAB hab;
  HWND managerWinFrame, managerWinClient;
  HMTX infoMutex;
  sessionThreadInfo *sessions;
  ULONG numItems;

public:
  SessionManager();
  ~SessionManager();
  
  OscarSession *getOpenedSession( const char *screenName );
};

#endif

