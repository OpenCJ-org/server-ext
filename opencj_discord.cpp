// TODO this class is currently Linux-specific. A Windows port needs to be added

#include "shared.hpp"

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <errno.h>

#ifdef COD4
    #define SOCKET_PATH     "/tmp/opencj_events_cod4"
#else
    #define SOCKET_PATH     "/tmp/opencj_events_cod2"
#endif

// These events are from the game to Discord
typedef enum
{
    PLAYER_MESSAGE          = 0,
    MAP_STARTED             = 1,
    PLAYER_COUNT_CHANGED    = 2,
    PLAYER_JOINED           = 3,
    PLAYER_LEFT             = 4,
    RUN_FINISHED            = 5,
    PLAYER_RENAMED          = 6,
} eGameEvent_t;

// These events are from Discord to the game
typedef enum
{
    DISCORD_MESSAGE         = 0,
} eDiscordEvent_t;


static int g_fileDescriptor = -1;

// Currently, just set latest Discord event, if one comes too quick, overwrite it.
// In future it may be an idea to queue them, but there'd have to be overflow guards
static bool g_hasLogged = false;
static int setupAndConnect()
{
    extern cvar_t *net_port;
    if (net_port->integer != 28960)
    {
        // Don't allow any other server than main to connect for now
        return -1;
    }

    // Check if we need to close the socket first
    if (g_fileDescriptor != -1)
    {
        // Socket error, disconnect
        close(g_fileDescriptor);
        g_fileDescriptor = -1;
        Com_PrintError(CON_CHANNEL_ERROR, "Lost connection with Discord socket\n");
        g_hasLogged = false;
    }

    g_fileDescriptor = socket(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (g_fileDescriptor >= 0)
    {
        struct sockaddr_un server;
        server.sun_family = AF_UNIX;
        strcpy(server.sun_path, SOCKET_PATH);
        if (connect(g_fileDescriptor, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) >= 0)
        {
            Com_Printf(CON_CHANNEL_SYSTEM, "Successfully connected to Discord socket\n");
            g_hasLogged = false;
        }
        else
        {
            close(g_fileDescriptor);
            g_fileDescriptor = -1;
            if (!g_hasLogged)
            {
                Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not connect to Discord socket\n");
                g_hasLogged = true;
            }
        }
    }
    else
    {
        g_fileDescriptor = -1;
        Com_PrintError(CON_CHANNEL_ERROR, "Could not setup Discord socket\n");
    }

    return g_fileDescriptor;
}

void Gsc_Discord_Connect()
{
    if (g_fileDescriptor == -1)
    {
        (void)setupAndConnect();
    }
    else
    {
        // Check if connection is alive
        int error = 0;
        socklen_t len = sizeof(error);
        int result = getsockopt(g_fileDescriptor, SOL_SOCKET, SO_ERROR, &error, &len);
        if (result == 0)
        {
            stackPushInt(g_fileDescriptor);
            return;
        }
        else
        {
            // Try to re-connect
            (void)setupAndConnect();
        }
    }

    if (g_fileDescriptor == -1)
    {
        stackPushUndefined();
    }
    else
    {
        stackPushInt(g_fileDescriptor);
    }
}

void Gsc_Discord_GetEvent() // Check if there are events from Discord
{
    char buf[512] = {0};
    if (g_fileDescriptor != -1)
    {
        int result = read(g_fileDescriptor, buf, sizeof(buf));
        int error = errno;
        if (result > 0)
        {
            //Com_Printf(CON_CHANNEL_SYSTEM, "Discord event '%s' -> game\n", buf);
            stackPushString(buf);
            return;
        }
        else if ((result == 0) || ((result == -1) && (error != EAGAIN) && (error != EWOULDBLOCK)))
        {
            // Read returned EOF or another error
            // Connection lost. Try to re-connect
            (void)setupAndConnect();
        }
    }

    stackPushUndefined();
}

void Gsc_Discord_OnEvent() // Push an event to Discord
{
    // Only process game events if Discord socket is connected
    if (g_fileDescriptor == -1)
    {
        return;
    }

    if ((Scr_GetNumParam() < 1) || (stackGetParamType(0) != STACK_INT))
    {
        stackError("Expected at least 1 argument: eventType (int)");
        return;
    }

    int gameEventType = -1;
    stackGetParamInt(0, &gameEventType);

    char txBuf[512] = {0};
    switch (gameEventType)
    {
        case PLAYER_MESSAGE:
        {
            if ((Scr_GetNumParam() != 3) || (stackGetParamType(1) != STACK_STRING) || (stackGetParamType(2) != STACK_STRING))
            {
                stackError("Expected 3 arguments: eventType (int), playerName (string), message (string)");
                return;
            }

            char *playerName = NULL;
            stackGetParamString(1, &playerName);

            char *message = NULL;
            stackGetParamString(2, &message);

            snprintf(txBuf, sizeof(txBuf), "%d %s;%s\n", gameEventType, playerName, message);
        } break;

        case MAP_STARTED:
        {
            if ((Scr_GetNumParam() != 2) || (stackGetParamType(1) != STACK_STRING))
            {
                stackError("Expected 2 arguments: eventType (int), mapName (string)");
                return;
            }

            char *mapName = NULL;
            stackGetParamString(1, &mapName);

            snprintf(txBuf, sizeof(txBuf), "%d %s\n", gameEventType, mapName);
        } break;

        case PLAYER_COUNT_CHANGED:
        {
            if ((Scr_GetNumParam() != 2) || (stackGetParamType(1) != STACK_INT))
            {
                stackError("Expected 2 arguments: eventType (int), playerCount (int)");
                return;
            }

            int playerCount = 0;
            stackGetParamInt(1, &playerCount);

            snprintf(txBuf, sizeof(txBuf), "%d %d\n", gameEventType, playerCount);
        } break;

        case PLAYER_JOINED:
        case PLAYER_LEFT:
        {
            if ((Scr_GetNumParam() != 2) || (stackGetParamType(1) != STACK_STRING))
            {
                stackError("Expected 2 arguments: eventType (int), playerName (string)");
                return;
            }

            char *playerName = NULL;
            stackGetParamString(1, &playerName);

            if (playerName != NULL)
            {
                snprintf(txBuf, sizeof(txBuf), "%d %s\n", gameEventType, playerName);
            }
        } break;

        case RUN_FINISHED:
        {
            if ((Scr_GetNumParam() != 6) || (stackGetParamType(1) != STACK_STRING) ||
                                            (stackGetParamType(2) != STACK_INT) ||
                                            (stackGetParamType(3) != STACK_STRING) ||
                                            (stackGetParamType(4) != STACK_STRING) ||
                                            (stackGetParamType(5) != STACK_STRING))
            {
                stackError("Expected 6 arguments: event (int), name (string), runID (int), time (string), mapName (string), route (string)");
                return;
            }

            char *playerName = NULL;
            stackGetParamString(1, &playerName);
            if (playerName == NULL)
            {
                return;
            }

            int runID = -1;
            stackGetParamInt(2, &runID);
            if (runID == -1)
            {
                return;
            }

            char *timeStr = NULL;
            stackGetParamString(3, &timeStr);
            if (timeStr == NULL)
            {
                return;
            }

            char *mapName = NULL;
            stackGetParamString(4, &mapName);
            if (mapName == NULL)
            {
                return;
            }

            char *routeName = NULL;
            stackGetParamString(5, &routeName);
            if (routeName == NULL)
            {
                return;
            }

            printf("\nyes, event being sent\n");
            snprintf(txBuf, sizeof(txBuf), "%d %s;%d;%s;%s;%s\n", gameEventType, playerName, runID, timeStr, mapName, routeName);
        } break;
    }

    // Arriving here means some data is ready to be transmitted
    int result = write(g_fileDescriptor, txBuf, strlen(txBuf));
    int error = errno;
    if (result > 0)
    {
        //Com_Printf(CON_CHANNEL_SYSTEM, "Game event '%s' -> Discord\n", txBuf);
    }
    else if ((result == -1) && (error != EAGAIN) && (error != EWOULDBLOCK))
    {
        // Write returned an error
        // Connection lost. Try to re-connect
        (void)setupAndConnect();
    }
    else
    {
        Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not transmit event (full)\n");
    }
}