/* REXX script to forward instant messages from one session to another.
   In order to work properly, this should be activated on the "IM received"
   event within Mr. Message.  This script will cause an error if you attempt
   to run it outside of Mr. Message.
   
   To change the target of who the messages are forwarded to, change the
   forwardedTo variable below.  Note that since a status check is done
   against this user, he or she will have to be in your buddy list. */

forwardedTo = SomebodyElse

myuser = ARG(1)
otheruser = ARG(2)
message = ARG(3)

USER_STATS myuser forwardedTo
PARSE VALUE IMSTATS WITH status .

if status = "ONLINE" then
do
  SEND_IM myuser forwardedTo otheruser' said, "'message'"'
  DEBUG_IM "Message was forwarded to "forwardedTo
end
else
do
  DEBUG_IM "Message was not forwarded to "forwardedTo".  Reason: "status
end

