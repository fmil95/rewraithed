/*
 * PC entry point: bring up PsyCross (window, GPU, SPU, pad, CD image),
 * then hand control to the game's own PSX main().
 */
#include <SDL.h>

#include <io.h>

#include "PsyX/PsyX_public.h"
#include "settings.h"

extern void PsyX_CDFS_Init(const char *imageFileName, int track, int sectorSize);

/* soul-re's main() from MAINVM.c, renamed by port/overrides.txt */
extern int main_psx(void);

extern void port_hasm_init(void);
extern void port_map_psx_memory(void);
extern void port_gpu_init(void);
extern void port_cd_set_image(const char *path);
extern void port_install_crash_handler(void);
extern void port_irq_init(void);
extern void port_discord_init(void);

/* port/src/native/disc_folder_win32.c: the "disc" folder next to the exe */
extern void port_disc_folder(char *out, size_t size);
extern int port_find_disc(char *out, size_t size);

/* port/psycross/psyx_port.cpp */
extern int g_cfg_dithering;
extern int g_cfg_pillarbox;
extern void PsyX_PortSetBorderless(void);

/* PsyCross */
extern int g_cfg_swapInterval;
extern int g_cfg_bilinearFiltering;
extern int g_port_pgxp;     /* port/psycross: PGXP */
extern PsyXKeyboardMapping g_cfg_keyboardMapping;

/* rewraithed.ini, as edited by the launcher */
PortSettings port_settings;

static int port_argc;
static char **port_argv;

static const char *find_arg(int argc, char **argv, const char *name)
{
    int i;

    for (i = 1; i < argc - 1; i++)
    {
        if (SDL_strcasecmp(argv[i], name) == 0)
        {
            return argv[i + 1];
        }
    }

    return NULL;
}

/* Command-line access for the rest of the port. */
const char *port_arg(const char *name)
{
    return find_arg(port_argc, port_argv, name);
}

int port_has_flag(const char *name)
{
    int i;

    for (i = 1; i < port_argc; i++)
    {
        if (SDL_strcasecmp(port_argv[i], name) == 0)
        {
            return 1;
        }
    }

    return 0;
}

/* PsyCross reads the raw MODE2/2352 image. Accepts the image itself, or a
 * .cue whose first FILE entry is resolved relative to the cue's folder. */
static const char *resolve_disc_image(const char *path)
{
    static char image[1024];
    char line[1024];
    const char *ext;
    const char *slash;
    char *name;
    char *end;
    FILE *cue;
    size_t dirLen;

    ext = SDL_strrchr(path, '.');

    if ((ext == NULL) || (SDL_strcasecmp(ext, ".cue") != 0))
    {
        return path;
    }

    cue = fopen(path, "r");

    if (cue == NULL)
    {
        fprintf(stderr, "[rewraithed] cannot open cue sheet '%s'\n", path);
        return path;
    }

    image[0] = '\0';

    while (fgets(line, sizeof(line), cue) != NULL)
    {
        name = SDL_strstr(line, "FILE");

        if (name == NULL)
        {
            continue;
        }

        name = SDL_strchr(name, '"');

        if ((name == NULL) || ((end = SDL_strchr(name + 1, '"')) == NULL))
        {
            continue;
        }

        *end = '\0';
        name++;

        slash = SDL_strrchr(path, '\\');

        if ((slash == NULL) || (SDL_strrchr(path, '/') > slash))
        {
            slash = SDL_strrchr(path, '/');
        }

        dirLen = (slash != NULL) ? (size_t)(slash - path + 1) : 0;

        SDL_snprintf(image, sizeof(image), "%.*s%s", (int)dirLen, path, name);
        break;
    }

    fclose(cue);

    return (image[0] != '\0') ? image : path;
}

/* Logging: everything the game and the port print also goes to
 * rewraithed.log next to the exe. */
static void port_start_logging(void)
{
    char path[1024];
    char *dot;

    port_settings_path(path, sizeof(path));
    dot = SDL_strrchr(path, '.');

    if (dot != NULL)
    {
        SDL_strlcpy(dot, ".log", sizeof(path) - (dot - path));
    }

    if (freopen(path, "w", stdout) != NULL)
    {
        _dup2(_fileno(stdout), _fileno(stderr));
    }
}

/* Keyboard bindings from the settings (SDL scancode names). */
static void port_apply_keys(void)
{
    int *const slots[BIND_COUNT] = {
        &g_cfg_keyboardMapping.kc_dpad_up, &g_cfg_keyboardMapping.kc_dpad_down,
        &g_cfg_keyboardMapping.kc_dpad_left, &g_cfg_keyboardMapping.kc_dpad_right,
        &g_cfg_keyboardMapping.kc_cross, &g_cfg_keyboardMapping.kc_circle,
        &g_cfg_keyboardMapping.kc_square, &g_cfg_keyboardMapping.kc_triangle,
        &g_cfg_keyboardMapping.kc_l1, &g_cfg_keyboardMapping.kc_l2,
        &g_cfg_keyboardMapping.kc_r1, &g_cfg_keyboardMapping.kc_r2,
        &g_cfg_keyboardMapping.kc_select, &g_cfg_keyboardMapping.kc_start,
    };
    int i;

    for (i = 0; i < BIND_COUNT; i++)
    {
        SDL_Scancode code = SDL_GetScancodeFromName(port_settings.keys[i]);

        if (code != SDL_SCANCODE_UNKNOWN)
        {
            *slots[i] = code;
        }
    }
}

int main(int argc, char **argv)
{
    static char found_disc[1024];
    const char *cue;

    port_argc = argc;
    port_argv = argv;

    {
        char path[1024];

        /* -ini <file> uses another settings file (e.g. for tests) */
        port_settings_defaults(&port_settings);
        port_settings_path(path, sizeof(path));
        port_settings_load(&port_settings, (port_arg("-ini") != NULL) ? port_arg("-ini") : path);
    }

    if (port_settings.logging || port_has_flag("-log"))
    {
        port_start_logging();
    }

    port_map_psx_memory();

    SDL_SetMainReady();

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _MSC_VER
    /* assertions go to stderr instead of a modal dialog */
    _set_error_mode(_OUT_TO_STDERR);
#endif

    /* -cd <image> on the command line, else the disc folder next to the exe */
    cue = find_arg(argc, argv, "-cd");

    if ((cue == NULL) && port_find_disc(found_disc, sizeof(found_disc)))
    {
        cue = found_disc;
    }

    if (cue == NULL)
    {
        char folder[1024];
        char message[1400];

        port_disc_folder(folder, sizeof(folder));

        SDL_snprintf(message, sizeof(message),
            "No Soul Reaver disc found.\n\n"
            "Put the disc image (the .cue sheet with its .bin/.img) in this folder:\n%s\n\n"
            "or start the game with -cd <path to the .cue or .bin/.img>.", folder);
        fprintf(stderr, "[rewraithed] %s\n", message);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rewraithed", message, NULL);
        return 1;
    }

    cue = resolve_disc_image(cue);

    {
        FILE *probe = fopen(cue, "rb");

        if (probe == NULL)
        {
            char message[1200];

            SDL_snprintf(message, sizeof(message),
                "Cannot open the Soul Reaver disc image:\n%s\n\n"
                "Put the disc image in the \"disc\" folder next to rewraithed.exe,\n"
                "or start the game with -cd <path to the .cue or .bin/.img>.", cue);
            fprintf(stderr, "[rewraithed] %s\n", message);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rewraithed", message, NULL);
            return 1;
        }

        fclose(probe);
    }

    /* PsyCross serves the ISO 9660 lookups (CdSearchFile), port/cd_drive.c
     * the sector streaming. */
    PsyX_CDFS_Init(cue, 0, 0);
    port_cd_set_image(cue);

    /* renderer options PsyCross reads while it creates the window and
     * compiles its shaders */
    g_cfg_swapInterval = port_settings.vsync;
    g_cfg_bilinearFiltering = port_settings.filtering;
    g_port_pgxp = port_settings.pgxp;
    g_cfg_dithering = port_settings.dithering;
    g_cfg_pillarbox = port_settings.pillarbox;

    if (port_settings.antialiasing > 1)
    {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, port_settings.antialiasing);
    }

    PsyX_Initialise("Rewraithed", port_settings.width, port_settings.height,
        port_settings.display == DISPLAY_FULLSCREEN);

    if (port_settings.display == DISPLAY_BORDERLESS)
    {
        PsyX_PortSetBorderless();
    }

    port_apply_keys();

    /* A US console runs NTSC without being told; the game never calls
     * SetVideoMode, and PsyCross otherwise counts vblanks at the PAL rate
     * (VSync waits and counts, and the CD drive's real-time position). */
    SetVideoMode(MODE_NTSC);

    /* after PsyX_Initialise, which installs its own (dialog-only) handler */
    port_install_crash_handler();

    port_irq_init();
    port_hasm_init();
    port_gpu_init();
    port_discord_init();

    main_psx();

    PsyX_Shutdown();

    return 0;
}
