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
#include <string.h>
#include <libgen.h>

/* OMAR type flags (TODO: rwx) */
#define OMAR_DIR    (1 << 0)

#define ALIGN_UP(value, align)        (((value) + (align)-1) & ~((align)-1))
#define BLOCK_SIZE 512

static int outfd;
static const char *inpath = NULL;
static const char *outpath = NULL;

/*
 * The OMAR file header, describes the basics
 * of a file.
 *
 * @magic: Header magic ("OMAR")
 * @nextptr: Offset from start of archive to next header
 * @len: Length of the file
 * @namelen: Length of the filename
 */
struct omar_hdr {
    char magic[4];
    uint16_t type;
    uint32_t nextptr;
    uint32_t len;
    uint8_t namelen;
} __attribute__((packed));

static inline void
help(void)
{
    printf("--------------------------------------\n");
    printf("The OSMORA archive format\n");
    printf("Usage: omar -i [input_dir] -o [output]\n");
    printf("-h      Show this help screen\n");
    printf("--------------------------------------\n");
}

/*
 * Push a file into the archive output
 *
 * @pathname: Full path name of file
 * @name: Name of file
 */
static int
file_push(const char *pathname, const char *name)
{
    struct omar_hdr hdr;
    struct stat sb;
    int infd, rem, error;
    int pad_len;
    char *buf;

    /* Attempt to open the input file */
    if ((infd = open(pathname, O_RDONLY)) < 0) {
        perror("open");
        return infd;
    }

    if ((error = fstat(infd, &sb)) < 0) {
        return error;
    }

    /* Create and write the header */
    hdr.len = sb.st_size;
    hdr.namelen = strlen(name);
    memcpy(hdr.magic, "OMAR", sizeof(hdr.magic));
    hdr.nextptr = sizeof(hdr) + ALIGN_UP(hdr.len, BLOCK_SIZE);
    write(outfd, &hdr, sizeof(hdr));
    write(outfd, name, hdr.namelen);

    /* If this is a dir, our work is done here */
    if (S_ISDIR(sb.st_mode)) {
        hdr.type |= OMAR_DIR;
        close(infd);
        return 0;
    }

    /* We need the file data now */
    buf = malloc(hdr.len);
    if (buf == NULL) {
        printf("out of memory\n");
        close(infd);
        return -ENOMEM;
    }
    if (read(infd, buf, hdr.len) <= 0) {
        perror("read");
        close(infd);
        return -EIO;
    }

    /*
     * Write the actual file contents, if
     * the file length is not a multiple
     * of the block size, we'll need to
     * pad out the rest to zero
     */
    write(outfd, buf, hdr.len);
    rem = hdr.len & (BLOCK_SIZE - 1);
    if (rem != 0) {
        /* Compute the padding length */
        pad_len = BLOCK_SIZE - rem;

        buf = realloc(buf, pad_len);
        memset(buf, 0, pad_len);
        write(outfd, buf, pad_len);
    }
    close(infd);
    free(buf);
    return 0;
}

/*
 * Start creating an archive from the
 * basepath of a directory.
 */
static int
archive_create(const char *base, const char *dirname)
{
    DIR *dp;
    struct dirent *ent;
    struct omar_hdr hdr;
    char pathbuf[256];
    char namebuf[256];

    dp = opendir(base);
    if (dp == NULL) {
        perror("opendir");
        return -ENOENT;
    }

    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base, ent->d_name);
        snprintf(namebuf, sizeof(namebuf), "%s/%s", dirname, ent->d_name);
        if (ent->d_type == DT_DIR) {
            printf("%s [d]\n", namebuf);
            file_push(pathbuf, ent->d_name);
            archive_create(pathbuf, basename(pathbuf));
        } else if (ent->d_type == DT_REG) {
            printf("%s [f]\n", namebuf);
            file_push(pathbuf, ent->d_name);
        }
    }

    return 0;
}

int
main(int argc, char **argv)
{
    int optc, retval;
    int error, flags;

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

    flags = S_IRUSR | S_IWUSR;
    outfd = open(outpath, O_WRONLY | O_CREAT, flags);
    if (outfd < 0) {
        printf("omar: failed to open output file\n");
        return outfd;
    }

    retval = archive_create(inpath, basename((char *)inpath));
    close(outfd);
    return retval;
}
