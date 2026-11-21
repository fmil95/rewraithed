/*
 * rewraithed.ini: a plain "key=value" file in sections, read and written by
 * both the game and the launcher. Unknown keys are ignored, missing ones
 * keep their defaults.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

const char *const port_bind_names[BIND_COUNT] = {
    "Up", "Down", "Left", "Right",
    "Cross", "Circle", "Square", "Triangle",
    "L1", "L2", "R1", "R2",
    "Select", "Start",
};

/* Keyboard defaults, as SDL scancode names: WASD (or the arrows) to move,
 * Space jumps (Cross), F feeds (Circle), V attacks (Square), R is
 * telekinesis (Triangle), C crouches (L1), Q and E look around (L2, R2),
 * Left Shift is stealth (R1), Tab the glyph menu (Select), Escape pauses
 * (Start). Aiming is L2 + R2 + Triangle; the Ctrl keys are left out, as many
 * keyboards cannot report three keys with both of them. */
static const char *const default_keys[BIND_COUNT] = {
    "W", "S", "A", "D",
    "Space", "F", "V", "R",
    "C", "Q", "Left Shift", "E",
    "Tab", "Escape",
};

static const char *const default_keys2[BIND_COUNT] = {
    "Up", "Down", "Left", "Right",
    "", "", "", "",
    "", "", "", "",
    "", "",
};

/* joystick defaults: the PlayStation pad's layout on an Xbox-style pad */
static const char *const default_joy[BIND_COUNT] = {
    "dpup", "dpdown", "dpleft", "dpright",
    "a", "b", "x", "y",
    "leftshoulder", "lefttrigger", "rightshoulder", "righttrigger",
    "back", "start",
};

const char *const port_joy_button_names[JOY_BUTTON_COUNT] = {
    "none", "a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger",
    "leftstick", "rightstick", "back", "start", "dpup", "dpdown", "dpleft", "dpright",
};

const char *const port_mouse_action_names[MOUSE_COUNT] = { "Attack", "Telekinesis", "Aim" };
const char *const port_mouse_button_names[MOUSE_BUTTON_COUNT] = { "None", "Left", "Middle", "Right", "X1", "X2" };

static const char *const display_names[] = { "Windowed", "Fullscreen", "Borderless" };

void port_settings_defaults(PortSettings *s)
{
    int i;

    memset(s, 0, sizeof(*s));

    s->display = DISPLAY_WINDOWED;
    s->width = 1280;
    s->height = 960;
    s->refresh = 0;
    s->scaleLinear = 1;
    s->vsync = 1;
    s->pillarbox = 1;
    s->filtering = 0;
    s->pgxp = 0;
    s->antialiasing = 0;

    s->skipIntros = SKIP_NONE;
    s->dithering = 1;
    s->preloadChunks = 0;
    s->debugMenu = 0;
    s->logging = 0;
    s->fpsLimit = 30;
    s->discordPresence = 1;

    for (i = 0; i < BIND_COUNT; i++)
    {
        strncpy(s->keys[i], default_keys[i], sizeof(s->keys[i]) - 1);
        strncpy(s->keys2[i], default_keys2[i], sizeof(s->keys2[i]) - 1);
    }

    s->mouse[MOUSE_ATTACK] = MOUSE_LEFT;
    s->mouse[MOUSE_TELEKINESIS] = MOUSE_MIDDLE;
    s->mouse[MOUSE_AIM] = MOUSE_RIGHT;

    s->joystick = 1;

    for (i = 0; i < BIND_COUNT; i++)
    {
        strncpy(s->joy[i], default_joy[i], sizeof(s->joy[i]) - 1);
    }
}

void port_settings_path(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, "rewraithed.ini");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        slash[1] = '\0';
    }

    snprintf(out, size, "%srewraithed.ini", exe);
}

static char *trim(char *s)
{
    char *end;

    while ((*s == ' ') || (*s == '\t'))
    {
        s++;
    }

    end = s + strlen(s);

    while ((end > s) && ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') || (end[-1] == '\n')))
    {
        *--end = '\0';
    }

    return s;
}

static int yes(const char *v)
{
    return (_stricmp(v, "yes") == 0) || (_stricmp(v, "on") == 0) || (_stricmp(v, "true") == 0) || (atoi(v) != 0);
}

int port_settings_load(PortSettings *s, const char *path)
{
    char line[256];
    char section[32] = "";
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *p = trim(line);
        char *eq;
        char *key;
        char *value;
        int i;

        if ((*p == '\0') || (*p == ';') || (*p == '#'))
        {
            continue;
        }

        if (*p == '[')
        {
            char *close = strchr(p, ']');

            if (close != NULL)
            {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", p + 1);
            }

            continue;
        }

        eq = strchr(p, '=');

        if (eq == NULL)
        {
            continue;
        }

        *eq = '\0';
        key = trim(p);
        value = trim(eq + 1);

        if (_stricmp(section, "Alternate Keys") == 0)
        {
            for (i = 0; i < BIND_COUNT; i++)
            {
                if (_stricmp(key, port_bind_names[i]) == 0)
                {
                    snprintf(s->keys2[i], sizeof(s->keys2[i]), "%s", value);
                }
            }

            continue;
        }

        if (_stricmp(section, "Mouse") == 0)
        {
            int b;

            for (i = 0; i < MOUSE_COUNT; i++)
            {
                if (_stricmp(key, port_mouse_action_names[i]) != 0)
                {
                    continue;
                }

                for (b = 0; b < MOUSE_BUTTON_COUNT; b++)
                {
                    if (_stricmp(value, port_mouse_button_names[b]) == 0)
                    {
                        s->mouse[i] = b;
                    }
                }
            }

            continue;
        }

        if (_stricmp(section, "Joystick") == 0)
        {
            if (_stricmp(key, "Enabled") == 0)
            {
                s->joystick = yes(value);
            }

            for (i = 0; i < BIND_COUNT; i++)
            {
                if ((_stricmp(key, port_bind_names[i]) == 0) && (*value != '\0'))
                {
                    snprintf(s->joy[i], sizeof(s->joy[i]), "%s", value);
                }
            }

            continue;
        }

        if (_stricmp(section, "Keys") == 0)
        {
            for (i = 0; i < BIND_COUNT; i++)
            {
                if ((_stricmp(key, port_bind_names[i]) == 0) && (*value != '\0'))
                {
                    snprintf(s->keys[i], sizeof(s->keys[i]), "%s", value);
                }
            }

            continue;
        }

        if (_stricmp(key, "Display") == 0)
        {
            for (i = 0; i < 3; i++)
            {
                if (_stricmp(value, display_names[i]) == 0)
                {
                    s->display = i;
                }
            }
        }
        else if (_stricmp(key, "Resolution") == 0)
        {
            int w;
            int h;
            int r = 0;

            /* "1920x1080", or with the fullscreen refresh rate "1920x1080@75" */
            if ((sscanf(value, "%dx%d@%d", &w, &h, &r) >= 2) && (w >= 320) && (h >= 240) && (w <= 7680) && (h <= 4320))
            {
                s->width = w;
                s->height = h;
                s->refresh = ((r > 0) && (r <= 1000)) ? r : 0;
            }
        }
        else if (_stricmp(key, "Scaling") == 0)         s->scaleLinear = _stricmp(value, "Nearest") != 0;
        else if (_stricmp(key, "VSync") == 0)           s->vsync = yes(value);
        else if (_stricmp(key, "Pillarboxing") == 0)    s->pillarbox = yes(value);
        else if (_stricmp(key, "Filtering") == 0)       s->filtering = _stricmp(value, "Off") != 0;
        else if (_stricmp(key, "PGXP") == 0)            s->pgxp = yes(value);
        else if (_stricmp(key, "Antialiasing") == 0)    s->antialiasing = atoi(value);
        else if (_stricmp(key, "Area") == 0)            snprintf(s->area, sizeof(s->area), "%s", value);
        else if (_stricmp(key, "SkipIntros") == 0)
        {
            s->skipIntros = (_stricmp(value, "Menu") == 0) ? SKIP_TO_MENU : ((_stricmp(value, "Game") == 0) ? SKIP_TO_GAME : SKIP_NONE);
        }
        else if (_stricmp(key, "Dithering") == 0)       s->dithering = yes(value);
        else if (_stricmp(key, "PreloadChunks") == 0)   s->preloadChunks = yes(value);
        else if (_stricmp(key, "DebugMenu") == 0)       s->debugMenu = yes(value);
        else if (_stricmp(key, "Logging") == 0)         s->logging = yes(value);
        else if (_stricmp(key, "FPSLimit") == 0)        s->fpsLimit = atoi(value);
        else if (_stricmp(key, "DiscordPresence") == 0) s->discordPresence = yes(value);
        else if (_stricmp(key, "DreamcastModels") == 0) s->dcModels = yes(value);
        else if (_stricmp(key, "ProtoAssets") == 0)     s->protoAssets = yes(value);
        else if (_stricmp(key, "Parameters") == 0)      snprintf(s->parameters, sizeof(s->parameters), "%s", value);
    }

    fclose(f);

    if ((s->antialiasing != 0) && (s->antialiasing != 2) && (s->antialiasing != 4) && (s->antialiasing != 8))
    {
        s->antialiasing = 0;
    }

    if (s->fpsLimit < 0)
    {
        s->fpsLimit = 30;
    }

    return 1;
}

int port_settings_save(const PortSettings *s, const char *path)
{
    static const char *const skip_names[] = { "None", "Menu", "Game" };
    FILE *f = fopen(path, "w");
    int i;

    if (f == NULL)
    {
        return 0;
    }

    fprintf(f, "; Rewraithed settings, written by the launcher\n\n");

    fprintf(f, "[Display]\n");
    fprintf(f, "Display=%s\n", display_names[s->display]);
    if (s->refresh > 0)
    {
        fprintf(f, "Resolution=%dx%d@%d\n", s->width, s->height, s->refresh);
    }
    else
    {
        fprintf(f, "Resolution=%dx%d\n", s->width, s->height);
    }
    fprintf(f, "Scaling=%s\n", s->scaleLinear ? "Bilinear" : "Nearest");
    fprintf(f, "Renderer=OpenGL\n");
    fprintf(f, "VSync=%s\n", s->vsync ? "Yes" : "No");
    fprintf(f, "Pillarboxing=%s\n", s->pillarbox ? "Yes" : "No");
    fprintf(f, "Filtering=%s\n", s->filtering ? "Bilinear" : "Off");
    fprintf(f, "PGXP=%s\n", s->pgxp ? "Yes" : "No");
    fprintf(f, "Antialiasing=%d\n", s->antialiasing);

    fprintf(f, "\n[Game]\n");
    fprintf(f, "Area=%s\n", s->area);
    fprintf(f, "SkipIntros=%s\n", skip_names[s->skipIntros]);
    fprintf(f, "Dithering=%s\n", s->dithering ? "On" : "Off");
    fprintf(f, "PreloadChunks=%s\n", s->preloadChunks ? "Yes" : "No");
    fprintf(f, "DebugMenu=%s\n", s->debugMenu ? "Yes" : "No");
    fprintf(f, "Logging=%s\n", s->logging ? "Yes" : "No");
    fprintf(f, "FPSLimit=%d\n", s->fpsLimit);
    fprintf(f, "DiscordPresence=%s\n", s->discordPresence ? "Yes" : "No");
    fprintf(f, "DreamcastModels=%s\n", s->dcModels ? "Yes" : "No");
    fprintf(f, "ProtoAssets=%s\n", s->protoAssets ? "Yes" : "No");
    fprintf(f, "Parameters=%s\n", s->parameters);

    fprintf(f, "\n[Keys]\n");

    for (i = 0; i < BIND_COUNT; i++)
    {
        fprintf(f, "%s=%s\n", port_bind_names[i], s->keys[i]);
    }

    fprintf(f, "\n[Alternate Keys]\n");

    for (i = 0; i < BIND_COUNT; i++)
    {
        fprintf(f, "%s=%s\n", port_bind_names[i], s->keys2[i]);
    }

    fprintf(f, "\n[Mouse]\n");

    for (i = 0; i < MOUSE_COUNT; i++)
    {
        fprintf(f, "%s=%s\n", port_mouse_action_names[i], port_mouse_button_names[s->mouse[i]]);
    }

    fprintf(f, "\n[Joystick]\n");
    fprintf(f, "Enabled=%s\n", s->joystick ? "Yes" : "No");

    for (i = 0; i < BIND_COUNT; i++)
    {
        fprintf(f, "%s=%s\n", port_bind_names[i], s->joy[i]);
    }

    fclose(f);

    return 1;
}
