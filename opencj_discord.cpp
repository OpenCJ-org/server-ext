#include "shared.hpp"

#ifdef __WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define DISCORD_PORT 28961

#ifdef __WIN32
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define CLOSESOCK(fd) closesocket(fd)
#else
typedef int socket_t;
#define INVALID_SOCK (-1)
#define CLOSESOCK(fd) close(fd)
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

static socket_t g_fileDescriptor = INVALID_SOCK;

// Currently, just set latest Discord event, if one comes too quick, overwrite it.
// In future it may be an idea to queue them, but there'd have to be overflow guards
static bool g_hasLogged = false;

static void setNonBlocking(socket_t fd)
{
#ifdef __WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#endif
}

static void setupAndConnect()
{
    extern cvar_t *net_port;
    if (net_port->integer != 28960)
    {
        // Don't allow any other server than main to connect for now
        return;
    }

    // Check if we need to close the socket first
    if (g_fileDescriptor != INVALID_SOCK)
    {
        // Socket error, disconnect
        CLOSESOCK(g_fileDescriptor);
        g_fileDescriptor = INVALID_SOCK;
        Com_PrintError(CON_CHANNEL_ERROR, "Lost connection with Discord socket\n");
        g_hasLogged = false;
    }

    g_fileDescriptor = socket(AF_INET, SOCK_STREAM, 0);

    if (g_fileDescriptor == INVALID_SOCK)
    {
        Com_PrintError(CON_CHANNEL_ERROR, "Could not setup Discord socket\n");
        return;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(DISCORD_PORT);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(g_fileDescriptor, (sockaddr*)&server, sizeof(server)) >= 0)
    {
        setNonBlocking(g_fileDescriptor);
        Com_Printf(CON_CHANNEL_SYSTEM, "Successfully connected to Discord socket\n");
        g_hasLogged = false;
    }
    else
    {
        CLOSESOCK(g_fileDescriptor);
        g_fileDescriptor = INVALID_SOCK;
        if (!g_hasLogged)
        {
            Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not connect to Discord socket\n");
            g_hasLogged = true;
        }
    }
}

void Gsc_Discord_Connect()
{
    if (g_fileDescriptor == INVALID_SOCK)
    {
        setupAndConnect();
    }
    else
    {
        // Check if connection is alive
        int error = 0;
        int len = sizeof(error);
        int result = getsockopt(g_fileDescriptor, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
        if (result == 0)
        {
            stackPushInt(g_fileDescriptor);
            return;
        }
        else
        {
            // Try to re-connect
            setupAndConnect();
        }
    }

    if (g_fileDescriptor == INVALID_SOCK)
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
    if (g_fileDescriptor == INVALID_SOCK)
    {
        stackPushUndefined();
        return;
    }

    int result = recv(g_fileDescriptor, buf, sizeof(buf), 0);
    if (result > 0)
    {
        //Com_Printf(CON_CHANNEL_SYSTEM, "Discord event '%s' -> game\n", buf);
        stackPushString(buf);
        return;
    }

    if (result == 0 || (result == -1
        #ifdef __WIN32
            && WSAGetLastError() != WSAEWOULDBLOCK
        #else
            && errno != EAGAIN && errno != EWOULDBLOCK
        #endif
        ))
    {
        // Read returned EOF or another error
        // Connection lost. Try to re-connect
        setupAndConnect();
    }

    stackPushUndefined();
}

void Gsc_Discord_OnEvent() // Push an event to Discord
{
    // Only process game events if Discord socket is connected
    if (g_fileDescriptor == INVALID_SOCK)
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

            snprintf(txBuf, sizeof(txBuf), "%d %s;%d;%s;%s;%s\n", gameEventType, playerName, runID, timeStr, mapName, routeName);
        } break;
    }

    // Arriving here means some data is ready to be transmitted
    int result = send(g_fileDescriptor, txBuf, strlen(txBuf), 0);
    if (result > 0)
    {
        //Com_Printf(CON_CHANNEL_SYSTEM, "Game event '%s' -> Discord\n", txBuf);
    }
    else if (result == -1
        #ifdef __WIN32
            && WSAGetLastError() != WSAEWOULDBLOCK
        #else
            && errno != EAGAIN && errno != EWOULDBLOCK
        #endif
        )
    {
        // Write returned an error
        // Connection lost. Try to re-connect
        setupAndConnect();
    }
    else
    {
        Com_PrintWarning(CON_CHANNEL_SYSTEM, "Could not transmit event (full)\n");
    }
}
