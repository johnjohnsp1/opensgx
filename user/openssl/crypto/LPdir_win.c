/*
 * Copyright (c) 2004, Richard Levitte <richard@levitte.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <windows.h>
#include <tchar.h>
#ifndef LPDIR_H
# include "LPdir.h"
#endif

#include "sgx.h"

/*
 * We're most likely overcautious here, but let's reserve for broken WinCE
 * headers and explicitly opt for UNICODE call. Keep in mind that our WinCE
 * builds are compiled with -DUNICODE [as well as -D_UNICODE].
 */
#if defined(LP_SYS_WINCE) && !defined(FindFirstFile)
# define FindFirstFile FindFirstFileW
#endif
#if defined(LP_SYS_WINCE) && !defined(FindNextFile)
# define FindNextFile FindNextFileW
#endif

#ifndef NAME_MAX
# define NAME_MAX 255
#endif

struct LP_dir_context_st {
    WIN32_FIND_DATA ctx;
    HANDLE handle;
    char entry_name[NAME_MAX + 1];
};

const char *LP_find_file(LP_DIR_CTX **ctx, const char *directory)
{
    if (ctx == NULL || directory == NULL) {
        errno = EINVAL;
        return 0;
    }

    errno = 0;
    if (*ctx == NULL) {
        const char *extdir = directory;
        char *extdirbuf = NULL;
        size_t dirlen = sgx_strlen(directory);

        if (dirlen == 0) {
            errno = ENOENT;
            return 0;
        }

        *ctx = (LP_DIR_CTX *)sgx_malloc(sizeof(LP_DIR_CTX));
        if (*ctx == NULL) {
            errno = ENOMEM;
            return 0;
        }
        sgx_memset(*ctx, '\0', sizeof(LP_DIR_CTX));

        if (directory[dirlen - 1] != '*') {
            extdirbuf = (char *)sgx_malloc(dirlen + 3);
            if (extdirbuf == NULL) {
                sgx_free(*ctx);
                *ctx = NULL;
                errno = ENOMEM;
                return 0;
            }
            if (directory[dirlen - 1] != '/' && directory[dirlen - 1] != '\\')
                extdir = sgx_strcat(sgx_strcpy(extdirbuf, directory), "/*");
            else
                extdir = sgx_strcat(sgx_strcpy(extdirbuf, directory), "*");
        }

        if (sizeof(TCHAR) != sizeof(char)) {
            TCHAR *wdir = NULL;
            /* len_0 denotes string length *with* trailing 0 */
            size_t index = 0, len_0 = sgx_strlen(extdir) + 1;

            wdir = (TCHAR *)calloc(len_0, sizeof(TCHAR));
            if (wdir == NULL) {
                if (extdirbuf != NULL) {
                    sgx_free(extdirbuf);
                }
                sgx_free(*ctx);
                *ctx = NULL;
                errno = ENOMEM;
                return 0;
            }
#ifdef LP_MULTIBYTE_AVAILABLE
            if (!MultiByteToWideChar
                (CP_ACP, 0, extdir, len_0, (WCHAR *)wdir, len_0))
#endif
                for (index = 0; index < len_0; index++)
                    wdir[index] = (TCHAR)extdir[index];

            (*ctx)->handle = FindFirstFile(wdir, &(*ctx)->ctx);

            sgx_free(wdir);
        } else {
            (*ctx)->handle = FindFirstFile((TCHAR *)extdir, &(*ctx)->ctx);
        }
        if (extdirbuf != NULL) {
            sgx_free(extdirbuf);
        }

        if ((*ctx)->handle == INVALID_HANDLE_VALUE) {
            sgx_free(*ctx);
            *ctx = NULL;
            errno = EINVAL;
            return 0;
        }
    } else {
        if (FindNextFile((*ctx)->handle, &(*ctx)->ctx) == FALSE) {
            return 0;
        }
    }
    if (sizeof(TCHAR) != sizeof(char)) {
        TCHAR *wdir = (*ctx)->ctx.cFileName;
        size_t index, len_0 = 0;

        while (wdir[len_0] && len_0 < (sizeof((*ctx)->entry_name) - 1))
            len_0++;
        len_0++;

#ifdef LP_MULTIBYTE_AVAILABLE
        if (!WideCharToMultiByte
            (CP_ACP, 0, (WCHAR *)wdir, len_0, (*ctx)->entry_name,
             sizeof((*ctx)->entry_name), NULL, 0))
#endif
            for (index = 0; index < len_0; index++)
                (*ctx)->entry_name[index] = (char)wdir[index];
    } else
        sgx_strncpy((*ctx)->entry_name, (const char *)(*ctx)->ctx.cFileName,
                sizeof((*ctx)->entry_name) - 1);

    (*ctx)->entry_name[sizeof((*ctx)->entry_name) - 1] = '\0';

    return (*ctx)->entry_name;
}

int LP_find_file_end(LP_DIR_CTX **ctx)
{
    if (ctx != NULL && *ctx != NULL) {
        FindClose((*ctx)->handle);
        sgx_free(*ctx);
        *ctx = NULL;
        return 1;
    }
    errno = EINVAL;
    return 0;
}
