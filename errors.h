#ifndef ERRORS_H_INCLUDED
#define ERRORS_H_INCLUDED

typedef enum
{
  MRM_NO_ERROR,
  
  MRM_GEN_CREATE_SEM, MRM_GEN_CREATE_MTX, MRM_GEN_WAIT_SEM,
  
  MRM_OSC_SOCKET_OPEN, MRM_OSC_GET_OSC_IP, MRM_OSC_SOCKET_CLOSE,
  MRM_OSC_SOCKET_CONNECT, MRM_OSC_ACK_TIMEOUT, MRM_OSC_BAD_ACK,
  MRM_OSC_AUTH_TIMEOUT, MRM_OSC_BAD_AUTH, MRM_OSC_AUTH_ERROR,
  MRM_OSC_NO_BOS,
  
  MRM_BOS_SOCKET_OPEN, MRM_BOS_GET_BOS_IP, MRM_BOS_SOCKET_CONNECT,
  MRM_BOS_SOCKET_CLOSE, MRM_BOS_ACK_TIMEOUT, MRM_BOS_BAD_ACK,
  MRM_BOS_LOGIN_TIMEOUT, MRM_BOS_BAD_LOGIN, MRM_BOS_BAD_SERV_VER,
  MRM_BOS_SERV_VER_TIMEOUT, MRM_BOS_NO_SELF_INFO, MRM_BOS_NO_RATE_INFO,
  MRM_BOS_NO_SSI, MRM_BOS_NO_SSI_LIM, MRM_BOS_NO_LOCATION_LIM,
  MRM_BOS_NO_BLM_LIM, MRM_BOS_NO_PRIVACY_LIM,
  
  MRM_AIM_BAD_IDPASS, MRM_AIM_BAD_PASSWORD, MRM_AIM_AUTH_FAILED,
  MRM_AIM_ACCOUNT_DELETED, MRM_AIM_SERVICE_TEMP_UNAVAILABLE,
  MRM_AIM_ACCOUNT_SUSPENDED, MRM_AIM_RATE_LIMIT_HIT, MRM_AIM_FORCE_UPGRADE,
  MRM_AIM_INVALID_SECUREID, MRM_AIM_ACCOUNT_SUSPENDED_MINOR,
  MRM_AIM_UNKNOWN_LOGIN_FAILURE,
  
  MRM_MAX_MESSAGE
} errorStatus;

extern char *errorMessageStrings[];

#endif