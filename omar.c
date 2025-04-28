/*
 * Copyright (c) 2025 Ian Marco Moffett and the Osmora Team.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Hyra nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <assert.h>
#include <dirent.h>

static const char *inpath = NULL;
static const char *outpath = NULL;

/*
 * OMAR state machine.
 *
 * @outfd: Output file descriptor.
 * @inbuf: Input file buffer.
 * @pos: Position in input file.
 */
struct omar_state {
    int infd;
    char *inbuf;
    off_t pos;
};

/*
 * The OMAR file header, describes the basics
 * of a file.
 *
 * @name: Name of the file (*not* the full path)
 * @len: Length of the file
 * @next_hdr: Offset from start of archive to next header
 */
struct omar_hdr {
    char *name;
    size_t len;
    off_t next_hdr;
};

static inline void
help(void)
{
    printf("--------------------------------------\n");
    printf("The OSMORA archive format\n");
    printf("Usage: omar -i [input_dir] -o [output]\n");
    printf("-h      Show this help screen\n");
    printf("--------------------------------------\n");
}

static void
state_destroy(struct omar_state *stp)
{
    close(stp->infd);
    if (stp->inbuf != NULL) {
        free(stp->inbuf);
    }
}

static int
state_init(struct omar_state *res)
{
    struct stat sb;
    int retval = 0;

    assert(res != NULL);
    res->infd = open(inpath, O_RDONLY);
    if (res->infd < 0) {
        perror("open");
        fprintf(stderr, "omar: failed to open input file\n");
        return res->infd;
    }

    if ((retval = fstat(res->infd, &sb)) < 0) {
        perror("fstat");
        fprintf(stderr, "omar: failed to stat input file\n");
        close(res->infd);
        return retval;
    }

    if (!S_ISDIR(sb.st_mode)) {
        fprintf(stderr, "omar: input file is not a directory\n");
        close(res->infd);
        return -ENOTDIR;
    }

    res->inbuf = malloc(sb.st_size);
    if (res->inbuf == NULL) {
        close(res->infd);
        fprintf(stderr, "omar: failed to allocate inbuf\n");
        return -ENOMEM;
    }

    return 0;
}

/*
 * Start creating an archive from the
 * basepath of a directory.
 */
static int
archive_create(const char *base)
{
    DIR *dp;
    struct dirent *ent;
    struct omar_hdr hdr;
    char pathbuf[256];

    dp = opendir(base);
    if (dp == NULL) {
        return -EIO;
    }

    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base, ent->d_name);
        if (ent->d_type == DT_DIR) {
            archive_create(pathbuf);
        }
        printf("%s\n", pathbuf);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    struct omar_state st = {0};
    int optc, retval;
    int error;

    if (argc < 2) {
        help();
        return -1;
    }

    while ((optc = getopt(argc, argv, "hi:o:")) != -1) {
        switch (optc) {
        case 'i':
            inpath = optarg;
            break;
        case 'o':
            outpath = optarg;
            break;
        case 'h':
            help();
            return 0;
        default:
            help();
            return -1;
        }
    }

    if (inpath == NULL) {
        fprintf(stderr, "omar: no input path\n");
        help();
        return -1;
    }
    if (outpath == NULL) {
        fprintf(stderr, "omar: no output path\n");
        help();
        return -1;
    }

    if ((error = state_init(&st)) != 0) {
        return error;
    }

    retval = archive_create(inpath);
    state_destroy(&st);
    return retval;
}
