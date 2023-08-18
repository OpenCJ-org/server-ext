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
} eGameEvent_t;

// These events are from Discord to the game
typedef enum
{
    DISCORD_MESSAGE         = 0,
} eDiscordEvent_t;


static int g_fileDescriptor = -1;

// Currently, just set latest Discord event, if one comes too quick, overwrite it.
// In future it may be an idea to queue them, but there'd have to be overflow guards


void Gsc_Discord_Connect()
{
    if (g_fileDescriptor == -1)
    {
        g_fileDescriptor = socket(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (g_fileDescriptor >= 0)
        {
            struct sockaddr_un server;
            server.sun_family = AF_UNIX;
            strcpy(server.sun_path, SOCKET_PATH);

            if (connect(g_fileDescriptor, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) >= 0)
            {
                stackPushInt(g_fileDescriptor);
                Com_Printf(CON_CHANNEL_SYSTEM, "Successfully connected to Discord socket\n");
                return;
            }
            else
            {
                close(g_fileDescriptor);
                g_fileDescriptor = -1;
                Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not connect to Discord socket\n");
            }
        }
        else
        {
            g_fileDescriptor = -1;
            Com_PrintError(CON_CHANNEL_ERROR, "Could not setup Discord socket\n");
        }
    }
    else
    {
        stackPushInt(g_fileDescriptor);
        return;
    }

    stackPushUndefined();
}

void Gsc_Discord_GetEvent() // Check if there are events from Discord
{
    char buf[512] = {0};
    if (g_fileDescriptor != -1)
    {
        int result = read(g_fileDescriptor, buf, sizeof(buf));
        if (result > 0)
        {
            stackPushString(buf);
            return;
        }
        else if ((result != EAGAIN) && (result != EWOULDBLOCK))
        {
            // Socket error, disconnect
            close(g_fileDescriptor);
            g_fileDescriptor = -1;
            Com_PrintError(CON_CHANNEL_ERROR, "Lost connection with Discord socket");
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

            snprintf(txBuf, sizeof(txBuf), "%d %s;%s", gameEventType, playerName, message);
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

            snprintf(txBuf, sizeof(txBuf), "%d %s", gameEventType, mapName);
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

            snprintf(txBuf, sizeof(txBuf), "%d %d", gameEventType, playerCount);
        } break;
    }

    // Arriving here means some data is ready to be transmitted
    int result = write(g_fileDescriptor, txBuf, strlen(txBuf));
    if (result > 0)
    {
        Com_Printf(CON_CHANNEL_SYSTEM, "Game event '%s' -> Discord\n", txBuf);
    }
    else if ((result == EWOULDBLOCK) || (result == EAGAIN))
    {
        Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not transmit event (full)\n");
    }
}