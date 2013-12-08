/* PRE_SEND processor */

myuser = ARG(1)
otheruser = ARG(2)
message = ARG(3)

SEND_IM myuser otheruser '"'message'", said 'myuser'.'
