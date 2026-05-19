/*
 *  ========================================================================
 *  Project:     NativeLauncher
 *  Version:     1.2
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
 *  ------------------------------------------------------------------------
 *  Changes v1.2:
 *   - Suppresses messages when running from icon
 *   - Added message popup on error when running from icon
 *   - EasyStruct initialised in a single step (msg passed directly)
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

static const char ver[] = "$VER: NativeLauncher 1.2 (19/05/2026)";

extern struct ExecBase *SysBase;
struct DOSBase       *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *AslBase;

/* Helper to report errors via Shell output or EasyRequest depending on
 * how the program was launched. IntuitionBase must be open for Workbench
 * mode — callers that fail before IntuitionBase opens must handle output
 * themselves. */
void ReportError(char *msg, int isWorkbench) {
    if (isWorkbench && IntuitionBase) {
        /* msg passed directly in initialiser — no separate assignment needed */
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, "NativeLauncher Error", msg, "OK" };
        EasyRequestArgs(NULL, &es, NULL, NULL);
    } else if (DOSBase) {
        Write(Output(), msg, strlen(msg));
    }
}

/* Check if a file is a valid Amiga Executable by testing the hunk magic
 * number (0x000003F3). Relies on m68k big-endian byte order for the raw
 * 4-byte Read(). */
int IsExecutable(char *filePath, int isWorkbench) {
    BPTR file;
    ULONG header = 0;
    int result = 0;

    if ((file = Open(filePath, MODE_OLDFILE))) {
        if (Read(file, &header, 4) == 4) {
            if (header == 0x000003F3UL) {
                result = 1;
            } else {
                ReportError("Error: File is not a valid Amiga executable.\n", isWorkbench);
            }
        } else {
            ReportError("Error: Could not read file header.\n", isWorkbench);
        }
        Close(file);
    } else {
        ReportError("Error: Program not found or could not be accessed.\n", isWorkbench);
    }
    return result;
}

int main(int argc, char **argv) {
    LONG rc = 0;
    int isWorkbench = (argc == 0);

    /* 1. Open DOS library first */
    DOSBase = (struct DOSBase *)OpenLibrary("dos.library", 37);
    if (!DOSBase) return 20;

    /* 2. Help request (CLI only) */
    if (!isWorkbench && argc == 2 && strcmp(argv[1], "?") == 0) {
        Write(Output(), "NativeLauncher 1.2 (19/05/2026)\n", 32);
        Write(Output(), "Usage: NativeLauncher <ProgramPath>\n", 36);
        Write(Output(), "Note: If no program is specified, a file requester will open.\n", 62);
        CloseLibrary((struct Library *)DOSBase);
        return 0;
    }

    /* 3. Open required libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) {
        /* IntuitionBase not open so ReportError cannot use EasyRequest;
         * no feedback possible on Workbench at this point */
        if (!isWorkbench) Write(Output(), "Error: Could not open intuition.library!\n", 41);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
    if (!GfxBase) {
        ReportError("Error: Could not open graphics.library!\n", isWorkbench);
        CloseLibrary((struct Library *)IntuitionBase);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    /* AslBase only needed when no CLI argument is supplied (file requester
     * path), so defer opening it to avoid unnecessary resource use */
    if (isWorkbench || argc < 2) {
        AslBase = OpenLibrary("asl.library", 37);
        if (!AslBase) {
            ReportError("Error: Could not open asl.library!\n", isWorkbench);
            CloseLibrary((struct Library *)GfxBase);
            CloseLibrary((struct Library *)IntuitionBase);
            CloseLibrary((struct Library *)DOSBase);
            return 20;
        }
    }

    {
        char commandLine[512] = "";
        int proceed = 0;

        if (!isWorkbench && argc >= 2) {
            /* CLI mode — validate argument and build command line with
             * bounds checking to guard against buffer overflow */
            if (IsExecutable(argv[1], isWorkbench)) {
                Write(Output(), "Native Launcher Started...\n", 27);
                int remaining = 511;
                for (int i = 1; i < argc; i++) {
                    int len = (int)strlen(argv[i]);
                    if (len + 1 > remaining) {
                        Write(Output(), "Error: Command line too long.\n", 30);
                        proceed = 0;
                        break;
                    }
                    strncat(commandLine, argv[i], (size_t)remaining);
                    remaining -= len;
                    if (remaining > 1) {
                        strcat(commandLine, " ");
                        remaining--;
                    }
                }
                if (commandLine[0] != '\0') proceed = 1;
            }
        } else {
            /* Workbench or CLI no-arg mode — open file requester */
            struct FileRequester *fr;
            if ((fr = AllocAslRequest(ASL_FileRequest, NULL))) {
                if (AslRequestTags(fr, ASLFR_TitleText, (Tag)"Select Native Program", TAG_DONE)) {
                    char tempPath[512] = "";
                    strcpy(tempPath, fr->fr_Drawer);
                    AddPart(tempPath, fr->fr_File, 512);

                    if (IsExecutable(tempPath, isWorkbench)) {
                        if (!isWorkbench) Write(Output(), "Native Launcher Started...\n", 27);
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

            /* Detect display standard via GfxBase flags. Opening this native
             * chipset screen is what signals the RTG driver (PiStorm /
             * FrameThrower) to hand off from RTG to native video output. */
            if (GfxBase->DisplayFlags & NTSC) {
                modeID = NTSC_MONITOR_ID;
                if (!isWorkbench) Write(Output(), "Opening Native Screen (NTSC)...\n", 32);
            } else {
                modeID = PAL_MONITOR_ID;
                if (!isWorkbench) Write(Output(), "Opening Native Screen (PAL)...\n", 31);
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
                    /* Brief delay to allow the RTG driver to complete the
                     * video switch before launching the target program */
                    Delay(25);
                    if (!isWorkbench) Write(Output(), "Executing program...\n", 21);

                    /* SystemTags() launches the executable directly (no shell
                     * overhead). NULL I/O handles are correct for Workbench
                     * launches — the target program manages its own I/O.
                     * Returns -1 if the program could not be launched at all,
                     * otherwise returns the program's own exit code. */
                    rc = SystemTags(commandLine,
                        SYS_Input,  (isWorkbench ? (BPTR)NULL : Input()),
                        SYS_Output, (isWorkbench ? (BPTR)NULL : Output()),
                        TAG_DONE);

                    if (rc < 0) {
                        ReportError("Error: Program failed to execute.\n", isWorkbench);
                        /* Pause so the user can read the error before the
                         * screen closes (CLI only; EasyRequest blocks
                         * automatically in Workbench mode) */
                        if (!isWorkbench) Delay(100);
                    }

                    /* Brief delay before closing screen to avoid a race
                     * between this cleanup and the launched program's own
                     * screen setup */
                    Delay(10);
                    CloseWindow(w);
                }
                CloseScreen(s);
            } else {
                ReportError("Error: Could not open native screen.\n", isWorkbench);
            }
        }
    }

    /* 4. Cleanup libraries */
    if (AslBase)       CloseLibrary(AslBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase)       CloseLibrary((struct Library *)DOSBase);

    return (int)rc;
}