/*
 * Discord Rich Presence over the local Discord client's IPC pipe
 * (\\.\pipe\discord-ipc-0 .. 9), with no Discord library to ship.
 *
 * Everything runs on a thread of its own: every second it asks the game
 * for what to show (port/src/discord_presence.c), and sends it when it
 * changes. If Discord is not running, it tries again every 15 seconds;
 * nothing here ever waits on the game thread or stops it.
 *
 * The application id is REWRAITHED_DISCORD_APP_ID, set in CMakeLists.txt.
 * Image keys are those of the art assets uploaded to that application.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef REWRAITHED_DISCORD_APP_ID
#define REWRAITHED_DISCORD_APP_ID ""
#endif

#ifndef REWRAITHED_VERSION_TEXT
#define REWRAITHED_VERSION_TEXT ""
#endif

/* art asset keys in the Discord application */
/* the large image: the icon in the repository (art/shiftglyph.png), by URL,
 * which Discord shows without an uploaded art asset */
#define DISCORD_IMAGE_LOGO      "https://raw.githubusercontent.com/fmil95/rewraithed/main/art/shiftglyph.png"
#define DISCORD_IMAGE_MATERIAL  "material"
#define DISCORD_IMAGE_SPECTRAL  "spectral"

enum { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

/* port/src/discord_presence.c: what to show now (realm 0 none, 1 material,
 * 2 spectral) */
void port_discord_snapshot(char *details, size_t detailsSize, char *state, size_t stateSize, int *realm);

static HANDLE pipe = INVALID_HANDLE_VALUE;
static long long start_time;

static int pipe_write(const void *data, DWORD size)
{
    DWORD done;

    while (size > 0)
    {
        if (!WriteFile(pipe, data, size, &done, NULL) || (done == 0))
        {
            return 0;
        }

        data = (const char *)data + done;
        size -= done;
    }

    return 1;
}

static int send_frame(unsigned int op, const char *json)
{
    unsigned int header[2];

    header[0] = op;
    header[1] = (unsigned int)strlen(json);

    return pipe_write(header, sizeof(header)) && pipe_write(json, header[1]);
}

static void disconnect(void)
{
    if (pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
}

/* Reads whatever Discord has sent without waiting for more: answers pings,
 * drops the rest. Returns 0 once the connection is gone. */
static int drain(void)
{
    static char buffer[16384];

    for (;;)
    {
        DWORD avail = 0;
        DWORD got;
        unsigned int header[2];

        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL))
        {
            return 0;
        }

        if (avail < sizeof(header))
        {
            return 1;
        }

        if (!ReadFile(pipe, header, sizeof(header), &got, NULL) || (got != sizeof(header)))
        {
            return 0;
        }

        if (header[1] >= sizeof(buffer))
        {
            return 0;
        }

        if ((header[1] > 0) && (!ReadFile(pipe, buffer, header[1], &got, NULL) || (got != header[1])))
        {
            return 0;
        }

        buffer[header[1]] = '\0';

        if (header[0] == OP_CLOSE)
        {
            return 0;
        }

        if ((header[0] == OP_PING) && !send_frame(OP_PONG, buffer))
        {
            return 0;
        }
    }
}

static int connect_discord(void)
{
    char json[128];
    int i;

    for (i = 0; i < 10; i++)
    {
        char name[64];

        snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        if (pipe != INVALID_HANDLE_VALUE)
        {
            break;
        }
    }

    if (pipe == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    snprintf(json, sizeof(json), "{\"v\":1,\"client_id\":\"%s\"}", REWRAITHED_DISCORD_APP_ID);

    if (!send_frame(OP_HANDSHAKE, json))
    {
        disconnect();
        return 0;
    }

    printf("[rewraithed] connected to Discord\n");

    return 1;
}

/* text as a JSON string body, cut to Discord's 128 characters */
static void json_text(char *out, size_t size, const char *in)
{
    size_t n = 0;
    int chars = 0;

    while ((*in != '\0') && (chars < 127) && (n + 7 < size))
    {
        unsigned char c = (unsigned char)*in++;

        if ((c == '"') || (c == '\\'))
        {
            out[n++] = '\\';
            out[n++] = (char)c;
        }
        else if (c < 0x20)
        {
            n += snprintf(out + n, size - n, "\\u%04x", c);
        }
        else
        {
            out[n++] = (char)c;
        }

        chars++;
    }

    out[n] = '\0';
}

static int send_activity(const char *details, const char *state, int realm)
{
    static unsigned int nonce;
    char json[1024];
    char d[300];
    char s[300];
    char assets[256];

    json_text(d, sizeof(d), details);
    json_text(s, sizeof(s), state);

    snprintf(assets, sizeof(assets),
        "\"large_image\":\"" DISCORD_IMAGE_LOGO "\",\"large_text\":\"Rewraithed " REWRAITHED_VERSION_TEXT "\"%s",
        (realm == 1) ? ",\"small_image\":\"" DISCORD_IMAGE_MATERIAL "\",\"small_text\":\"Material Realm\"" :
        (realm == 2) ? ",\"small_image\":\"" DISCORD_IMAGE_SPECTRAL "\",\"small_text\":\"Spectral Realm\"" : "");

    /* Discord turns down texts shorter than 2 characters: leave them out */
    snprintf(json, sizeof(json),
        "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu,\"activity\":{"
        "%s%s%s%s%s%s"
        "\"timestamps\":{\"start\":%lld},\"assets\":{%s}}}}",
        ++nonce, (unsigned long)GetCurrentProcessId(),
        (strlen(d) >= 2) ? "\"details\":\"" : "", (strlen(d) >= 2) ? d : "", (strlen(d) >= 2) ? "\"," : "",
        (strlen(s) >= 2) ? "\"state\":\"" : "", (strlen(s) >= 2) ? s : "", (strlen(s) >= 2) ? "\"," : "",
        start_time, assets);

    return send_frame(OP_FRAME, json);
}

static DWORD WINAPI discord_main(LPVOID unused)
{
    char sentDetails[128] = "";
    char sentState[128] = "";
    int sentRealm = -1;
    DWORD lastTry = 0;
    int tried = 0;

    (void)unused;

    for (;;)
    {
        char details[128];
        char state[128];
        int realm = 0;

        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (tried && (GetTickCount() - lastTry < 15000))
            {
                Sleep(1000);
                continue;
            }

            tried = 1;
            lastTry = GetTickCount();

            if (!connect_discord())
            {
                continue;
            }

            sentRealm = -1;     /* send the activity again */
        }

        if (!drain())
        {
            disconnect();
            continue;
        }

        port_discord_snapshot(details, sizeof(details), state, sizeof(state), &realm);

        if ((realm != sentRealm) || (strcmp(details, sentDetails) != 0) || (strcmp(state, sentState) != 0))
        {
            if (!send_activity(details, state, realm))
            {
                disconnect();
                continue;
            }

            snprintf(sentDetails, sizeof(sentDetails), "%s", details);
            snprintf(sentState, sizeof(sentState), "%s", state);
            sentRealm = realm;
        }

        Sleep(1000);
    }
}

/* Starts the presence thread. Closing the game closes the pipe, and
 * Discord clears the activity by itself. */
void port_discord_start(void)
{
    HANDLE thread;

    if (REWRAITHED_DISCORD_APP_ID[0] == '\0')
    {
        printf("[rewraithed] Discord Rich Presence: no application id in this build\n");
        return;
    }

    start_time = (long long)time(NULL);
    thread = CreateThread(NULL, 0, discord_main, NULL, 0, NULL);

    if (thread != NULL)
    {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    }
}
