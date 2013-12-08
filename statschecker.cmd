/* REXX script demonstrating how to parse the user statistics information
   and print them to the debug log file. */

myuser = ARG(1)
otheruser = ARG(2)

USER_STATS myuser otheruser
PARSE VALUE IMSTATS WITH status online idle profile

USER_ALIAS myuser otheruser

POST_TO_CHAT_WINDOW myuser otheruser "Information about "otheruser":"
POST_TO_CHAT_WINDOW myuser otheruser "  Alias:       "IMALIAS
POST_TO_CHAT_WINDOW myuser otheruser "  Status:      "status
POST_TO_CHAT_WINDOW myuser otheruser "  Time online: "online
POST_TO_CHAT_WINDOW myuser otheruser "  Time idle:   "idle
POST_TO_CHAT_WINDOW myuser otheruser "  Profile:     "profile
