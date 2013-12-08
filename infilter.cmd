/* POST_RECEIVE processor */

myuser = ARG(1)
otheruser = ARG(2)
message = ARG(3)

POST_TO_CHAT_WINDOW myuser otheruser "<color blue>Message received:<color black> "message
