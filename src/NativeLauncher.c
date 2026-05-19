/*
 *  ========================================================================
 *  Project:     NativeLauncher
 *  Version:     1.0
 *  Date:        19/05/2026
 *  Author:      Leigh Russ
 *  Assistance:  Developed with Gemini (Google)
 *  Platform:    AmigaOS 2.04+ (m68k)
 *  Description: Switch from RTG to Native video for PiStorm / FrameThrower.
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
#include <string.h>

static const char ver[] = "$VER: NativeLauncher 1.0 (19/05/2026)";

extern struct ExecBase *SysBase;
struct DOSBase       *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *AslBase;

/* Check if a file is a valid Amiga Executable (Hunk magic: 0x000003F3) */
int IsExecutable(char *filePath) {
    BPTR file;
    ULONG header = 0;
    int result = 0;

    if ((file = Open(filePath, MODE_OLDFILE))) {
        if (Read(file, &header, 4) == 4) {
            if (header == 0x000003F3) {
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
    /* 1. Open DOS library first */
    DOSBase = (struct DOSBase *)OpenLibrary("dos.library", 37);
    if (!DOSBase) return 20;

    /* 2. Help Request */
    if (argc == 2 && strcmp(argv[1], "?") == 0) {
        Write(Output(), "NativeLauncher 1.0 (19/05/2026)\n", 32);
        Write(Output(), "Usage: NativeLauncher <ProgramPath>\n", 36);
        Write(Output(), "Note: If no program is specified, a file requester will open.\n", 62);
        CloseLibrary((struct Library *)DOSBase);
        return 0;
    }

    /* 3. Open other libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    GfxBase       = (struct GfxBase *)OpenLibrary("graphics.library", 37);
    AslBase       = OpenLibrary("asl.library", 37);

    if (IntuitionBase && GfxBase && AslBase) {
        char commandLine[512] = "";
        int proceed = 0;

        if (argc >= 2) {
            /* Validate CLI argument */
            if (IsExecutable(argv[1])) {
                Write(Output(), "Native Launcher Started...\n", 27);
                for(int i = 1; i < argc; i++) {
                    strcat(commandLine, argv[i]);
                    strcat(commandLine, " ");
                }
                proceed = 1;
            }
        } else {
            /* Open File Requester */
            struct FileRequester *fr;
            if ((fr = AllocAslRequest(ASL_FileRequest, NULL))) {
                if (AslRequestTags(fr, ASLFR_TitleText, (Tag)"Select Native Program", TAG_DONE)) {
                    char tempPath[512] = "";
                    strcpy(tempPath, fr->fr_Drawer);
                    AddPart(tempPath, fr->fr_File, 512);

                    /* Validate Requester choice */
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
            ULONG modeID = PAL_MONITOR_ID;

            if (GfxBase->DisplayFlags & NTSC) {
                modeID = NTSC_MONITOR_ID;
                Write(Output(), "Opening Native Screen (NTSC)...\n", 32);
            } else {
                modeID = PAL_MONITOR_ID;
                Write(Output(), "Opening Native Screen (PAL)...\n", 31);
            }

            s = OpenScreenTags(NULL,
                SA_DisplayID, modeID,
                SA_Depth, 2,
                SA_Type, CUSTOMSCREEN,
                SA_Quiet, TRUE,
                SA_ShowTitle, FALSE,
                TAG_DONE);

            if (s) {
                struct Window *w = OpenWindowTags(NULL,
                    WA_CustomScreen, (Tag)s,
                    WA_Backdrop, TRUE,
                    WA_Borderless, TRUE,
                    WA_Activate, TRUE,
                    TAG_DONE);

                if (w) {
                    Delay(25);
                    Write(Output(), "Executing program...\n", 21);
                    if (!Execute(commandLine, 0, 0)) {
                        Write(Output(), "Error: Program failed to execute.\n", 34);
                        Delay(100);
                    }
                    Delay(10);
                    CloseWindow(w);
                }
                CloseScreen(s);
            }
        }
    } else {
        Write(Output(), "Error: Could not open all libraries!\n", 37);
    }

    /* 4. Cleanup with explicit casts to prevent warnings */
    if (AslBase)       CloseLibrary(AslBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase)       CloseLibrary((struct Library *)DOSBase);

    return 0;
}