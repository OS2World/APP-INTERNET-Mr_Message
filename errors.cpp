#include "errors.h"

char *errorMessageStrings[] =
{
  "No error.",
  
  "Error creating event semaphore.",
  "Error creating mutex semaphore.",
  "Error waiting on event semaphore.",
  
  "Error opening socket to connect to Oscar.",
  "Error obtaining IP address of Oscar server.",
  "Error closing socket to Oscar server.",
  "Error connecting to Oscar server.",
  "Timed out waiting for connection acknowledgement from Oscar server.",
  "Bad connection acknowledgement was received from Oscar server.",
  "Timed out waiting for login response from Oscar server.",
  "Bad authorization information was received from Oscar server.",
  "Oscar reported an authorization error (username or password is incorrect).",
  "Oscar did not refer me to a BOS server.",
  
  "Error opening socket to connect to BOS server.",
  "Error obtaining IP address for BOS server.",
  "Error connecting to BOS server.",
  "Error closing socket to BOS server.",
  "Timed out waiting for connection acknowledgement from BOS server.",
  "Bad connection acknowledgement was received from BOS server.",
  "Timed out waiting for login confirmation from BOS server.",
  "Bad login confirmation was received from BOS server.",
  "BOS server did not report services version information in the way that I expected.",
  "BOS server did not respond to a services version request in the allotted time.",
  "Error obtaining user's self-information from BOS server.",
  "Error getting rate information from BOS server.",
  "Error obtaining user's server-stored information from BOS server.",
  "Error obtaining SSI limitations from BOS server.",
  "Error obtaining location service limitations from BOS server.",
  "Error obtaining buddy list management limitations from BOS server.",
  "Error obtaining privacy limitations from BOS server.",
  
  "AOL rejected your screen name or password saying that one or both of them were invalid.  Please verify that your session settings are correct.",
  "AOL reports that you have entered an incorrect password.  Please verify that your session settings are correct.",
  "AOL reports that this client has sent it bad authorization data.  Please report this message to the developer of this client.\n",
  "AOL reports that your account has been deleted.  You'll have to register for a new account to connect to their network.",
  "AOL services are temporarily unavailable.  Try connecting again in a few minutes.",
  "AOL reports that your account has been suspended.  Shame on you.",
  "AOL reports that you have attempted to log in too many times over a short period of time.  Wait a few minutes and try again.",
  "AOL is forcing you to 'upgrade' your client to a newer version.  They will not allow you to connect to their network with this client.  Please check for an update and hopefully I'll have the problem addressed shortly.",
  "AOL reports that you attempted to use and invalid SecurdID.",
  "AOL reports that you are a pimply-faced geek attempting to access their network and annoy the adults.  Get yourself a paper route and go make something of your life.",
  "AOL reported a login failure that is not known to this client.  Please report this failure to the developers.",
  
  "This is the highest numbered message and you should never see it unless you're as cool as Michal Necasek.  :-)"
};

