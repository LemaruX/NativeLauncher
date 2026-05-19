/*
 *  ========================================================================
 *  Project:     NativeLauncher
 *  Version:     1.1
 *  Date:        19/05/2026
 *  Author:      Leigh Russ
 *  Assistance:  Developed with Gemini (Google), reviewed with Claude (Anthropic)
 *  Platform:    AmigaOS 2.04+ (m68k)
 *  Description: Switch from RTG to Native video for PiStorm / FrameThrower.
 *               Opens a native chipset screen to trigger the RTG driver to
 *               hand off to native video output, then launches the target
 *               program.
 *  ------------------------------------------------------------------------
 *  Changes v1.1:
 *   - Use SystemTags() instead of Execute() for cleaner program launching
 *     with proper I/O handle passing and return code handling
 *   - Added bounds checking to commandLine buffer concatenation
 *   - AslBase now opened only when needed (no CLI argument given)
 *   - Individual library open error reporting
 *   - Endian assumption documented (m68k big-endian, hunk magic raw read)
 *  ========================================================================
 */

#define __NOLIBBASE__

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <graphics/gfxbase.h>
#include <libraries/asl.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <string.h>

static const char ver[] = "$VER: NativeLauncher 1.1 (19/05/2026)";

extern struct ExecBase *SysBase;
struct DOSBase       *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *AslBase;

/* Check if a file is a valid Amiga Executable (Hunk magic: 0x000003F3).
 * Note: relies on m68k big-endian byte order for raw 4-byte Read(). */
int IsExecutable(char *filePath) {
    BPTR file;
    ULONG header = 0;
    int result = 0;

    if ((file = Open(filePath, MODE_OLDFILE))) {
        if (Read(file, &header, 4) == 4) {
            if (header == 0x000003F3UL) {
                result = 1;
            } else {
                Write(Output(), "Error: File is not a valid Amiga executable.\n", 45);
            }
        } else {
            Write(Output(), "Error: Could not read file header.\n", 35);
        }
        Close(file);
    } else {
        Write(Output(), "Error: Program not found or could not be accessed.\n", 51);
    }
    return result;
}

int main(int argc, char **argv) {
    LONG rc = 0;

    /* 1. Open DOS library first */
    DOSBase = (struct DOSBase *)OpenLibrary("dos.library", 37);
    if (!DOSBase) return 20;

    /* 2. Help request */
    if (argc == 2 && strcmp(argv[1], "?") == 0) {
        Write(Output(), "NativeLauncher 1.1 (19/05/2026)\n", 32);
        Write(Output(), "Usage: NativeLauncher <ProgramPath>\n", 36);
        Write(Output(), "Note: If no program is specified, a file requester will open.\n", 62);
        CloseLibrary((struct Library *)DOSBase);
        return 0;
    }

    /* 3. Open required libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) {
        Write(Output(), "Error: Could not open intuition.library!\n", 41);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
    if (!GfxBase) {
        Write(Output(), "Error: Could not open graphics.library!\n", 40);
        CloseLibrary((struct Library *)IntuitionBase);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    /* AslBase only needed if no CLI argument was supplied */
    if (argc < 2) {
        AslBase = OpenLibrary("asl.library", 37);
        if (!AslBase) {
            Write(Output(), "Error: Could not open asl.library!\n", 35);
            CloseLibrary((struct Library *)GfxBase);
            CloseLibrary((struct Library *)IntuitionBase);
            CloseLibrary((struct Library *)DOSBase);
            return 20;
        }
    }

    {
        char commandLine[512] = "";
        int proceed = 0;

        if (argc >= 2) {
            /* Validate CLI argument and build command line with bounds checking */
            if (IsExecutable(argv[1])) {
                Write(Output(), "Native Launcher Started...\n", 27);
                int remaining = 511;
                for (int i = 1; i < argc; i++) {
                    int len = strlen(argv[i]);
                    if (len + 1 > remaining) {
                        Write(Output(), "Error: Command line too long.\n", 30);
                        proceed = 0;
                        break;
                    }
                    strncat(commandLine, argv[i], remaining);
                    remaining -= len;
                    if (remaining > 1) {
                        strcat(commandLine, " ");
                        remaining--;
                    }
                }
                if (commandLine[0] != '\0') proceed = 1;
            }
        } else {
            /* Open file requester (AslBase is guaranteed open here) */
            struct FileRequester *fr;
            if ((fr = AllocAslRequest(ASL_FileRequest, NULL))) {
                if (AslRequestTags(fr, ASLFR_TitleText, (Tag)"Select Native Program", TAG_DONE)) {
                    char tempPath[512] = "";
                    strcpy(tempPath, fr->fr_Drawer);
                    AddPart(tempPath, fr->fr_File, 512);

                    if (IsExecutable(tempPath)) {
                        Write(Output(), "Native Launcher Started...\n", 27);
                        strcpy(commandLine, tempPath);
                        proceed = 1;
                    }
                }
                FreeAslRequest(fr);
            }
        }

        if (proceed) {
            struct Screen *s;
            ULONG modeID;

            /* Detect display standard via GfxBase flags.
             * The screen open itself is what signals the RTG driver
             * (PiStorm/FrameThrower) to switch from RTG to native video. */
            if (GfxBase->DisplayFlags & NTSC) {
                modeID = NTSC_MONITOR_ID;
                Write(Output(), "Opening Native Screen (NTSC)...\n", 32);
            } else {
                modeID = PAL_MONITOR_ID;
                Write(Output(), "Opening Native Screen (PAL)...\n", 31);
            }

            s = OpenScreenTags(NULL,
                SA_DisplayID,  modeID,
                SA_Depth,      2,
                SA_Type,       CUSTOMSCREEN,
                SA_Quiet,      TRUE,
                SA_ShowTitle,  FALSE,
                TAG_DONE);

            if (s) {
                struct Window *w = OpenWindowTags(NULL,
                    WA_CustomScreen, (Tag)s,
                    WA_Backdrop,     TRUE,
                    WA_Borderless,   TRUE,
                    WA_Activate,     TRUE,
                    TAG_DONE);

                if (w) {
                    Delay(25);
                    Write(Output(), "Executing program...\n", 21);

                    /* SystemTags() is preferred over Execute(): launches the
                     * executable directly (no shell overhead), passes proper
                     * I/O handles, and returns a meaningful exit code. */
                    rc = SystemTags(commandLine,
                        SYS_Input,  Input(),
                        SYS_Output, Output(),
                        TAG_DONE);

                    if (rc < 0) {
                        /* SystemTags() returns -1 if the program could not
                         * be launched at all */
                        Write(Output(), "Error: Program failed to execute.\n", 34);
                        Delay(100);
                    }

                    Delay(10);
                    CloseWindow(w);
                }
                CloseScreen(s);
            } else {
                Write(Output(), "Error: Could not open native screen.\n", 37);
            }
        }
    }

    /* 4. Cleanup */
    if (AslBase)       CloseLibrary(AslBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase)       CloseLibrary((struct Library *)DOSBase);

    return (int)rc;
}