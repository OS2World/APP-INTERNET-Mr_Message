Mr. Message for OS/2 - REXX interface information
=================================================

This is a quick guide to creating a REXX extension for Mr. Message for OS/2.
Mr. Message supports a number of events which can trigger the execution of
REXX scripts.  Information about the event is passed in to the REXX script,
and the REXX script can send commands back to Mr. Message.

REXX scripts are each launched in a separate thread running under the
Mr. Message process ID.  So scripts can remain running as long as needed to
accomplish their tasks without interfering with the operation of Mr. Message
itself.  If a REXX script is still running when Mr. Message shuts down, it
will be killed automatically.  Also note that standard in and standard out are
both inaccessible in this environment.  Any I/O operations will have to be
performed against files on disk, or using DEBUG_IM to post to the debug log
(if active) or POST_TO_CHAT_WINDOW to have the message appear in a chat
window.

Currently, the events that can be used to launch REXX scripts are:

Program Startup - no parameters are passed in

Session Startup - ARG(1) = Screen name of the current session

Error - ARG(1) = Screen name of the current session
        ARG(2) = Other screen name involved in error if applicable
        ARG(3) = Error description
        
Buddy arrival - ARG(1) = Screen name of the current session
                ARG(2) = Screen name of the buddy who arrived
                
Buddy departure - ARG(1) = Screen name of the current session
                  ARG(2) = Screen name of the buddy who arrived

Instant message received - ARG(1) = Screen name of the current session
                           ARG(2) = Screen name of the sender of the message
                           ARG(3) = Message that was received
                           
Post-processing for receive - ARG(1) = Screen name of the current session
                              ARG(2) = Screen name of the sender of the message
                              ARG(3) = Message that was received

Pre-processing for send - ARG(1) = Screen name of the current session
                          ARG(2) = Screen name of the intended receiver
                          ARG(3) = Message to be sent

Instant message sent - ARG(1) = Screen name of the current session
                       ARG(2) = Screen name of the intended receiver
                       ARG(3) = Message that was sent

First instant message with buddy - ARG(1) = Screen name of the current session
                                   ARG(2) = Screen name of the buddy

Session shutdown - ARG(1) = Screen name of the current session


You can assign a REXX script to any one of these events.  Please keep in mind
that your REXX script will execute asynchronously, so for example if you assign
a script for the "session shutdown" event, the session will already be in the
process of closing while your script is executing, and hence any sending of
IMs or queries for information will most likely not be possible or reliable.

Also note that the post-processing and pre-processing events are special in
that if you assign a REXX script to them, they will prevent normal processing
of the instant message.  A received IM with post-processing active will not
be displayed in the chat window automatically in order to give your script a
chance to modify the text.  The script can change the text and use
POST_TO_CHAT_WINDOW (see below) to display the modified message.  Likewise
the preprocessing message prevents automatically sending the instant message.
This allows the script to modify the message contents before it is sent to the
user.  The script must use SEND_IM (see below) to explicitly send the message.

Using these two events, it will be possible to create translators for other
character sets and even languages if you get fancy enough.


REXX subcommands
================

A REXX subcommand is something that the REXX interpreter itself does not
recognize, but can be passed back to another environment, via the ADDRESS
command, to be interpreted and executed.  Mr. Message implements several
subcommands for the REXX environment to allow you to query information and
send information from your scripts.  To access the subcommand environment set
up by Mr. Message in your scripts, use "ADDRESS MrMessage" or simply use the
subcommands by themselves, since this is the default environment for REXX
scripts launched from Mr. Message.

The following subcommands are supported:

SEND_IM <fromScreenName> <toScreenName> <message>
  The from and to screen names must not have any spaces in them.  If these
  names were passed in from Mr. Message events, this will already be the case.
  The message can contain anything, however, if you issue this statement
  without using "ADDRESS MrMessage", you will probably need to put some
  characters in quotes so that REXX doesn't try to interpret them.

USER_STATS <sessionScreenName> <buddyScreenName>
  This will query the status of a given buddy on the buddy list of the named
  session.  Again, the screen names must contain no spaces.  Information will
  only be retrieved if the buddy is on the buddy list in the named session.
  This call does not actually query for information from the server, just the
  Mr. Message buddy list.  Information is returned in a single string variable
  called IMSTATS which can be parsed like this:
  PARSE VALUE IMSTATS WITH status onlineTime idleTime profileMessage
  (status is either ONLINE, AWAY, or OFFLINE if there were no errors)

USER_ALIAS <sessionScreenName> <buddyScreenName>
  Similar to USER_STATS above, but returns the buddy's alias so you can use
  a name you are familiar with in you logs and whatnot instead of the screen
  name.

POST_TO_CHAT_WINDOW <sessionScreenName> <buddyScreenName> <message>
  This is similar to SEND_IM except the message is displayed in your local
  buddy IM window and is not actually sent to your buddy.  Use this to have
  your REXX scripts send you a message on status or anything that you want.
  This can also be used to display an IM that you received in the
  post-processing event.

DEBUG_IM <debugMessage>
  This will cause the debugMessage to be added to your debug.log output from
  Mr. Message if the debug log is enabled.


More subcommands can be added as time goes on.  Any suggestions for additional
commands or events which could be useful are welcomed.  Anyone who has written
as useful extension to Mr. Message in REXX can also feel free to submit them
to me for inclusion in the base release if they want.

The following sample REXX scripts are included:

forwarder.cmd - Forwards messages received by one AIM session to another if
                this is set for the "IM received" event.
statschecker.cmd - Example for how to grab user statistics and send them to
                   the Mr. Message debug log.
imfilter.cmd - Modifies an outgoing message if this is set for the
               "pre-processing for send" event.
infilter.cmd - Modifies an incoming message if this is set for the
               "post-processing for receive" event.
