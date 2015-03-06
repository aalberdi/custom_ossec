/* Copyright (C) 2009 Trend Micro Inc.
 * All right reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#include <stdio.h>
#include <string.h>

#include "md5_sha1_op.h"
#include "../md5/md5.h"
#include "../sha1/sha.h"
#include "headers/defs.h"
#include "../shared/prefilter.h"

int OS_MD5_SHA1_File(char *fname, const char *prefilter_cmd, os_md5 md5output, os_sha1 sha1output)
{
    size_t n;
    FILE *fp;
    unsigned char buf[2048 + 2];
    unsigned char sha1_digest[SHA_DIGEST_LENGTH];
    unsigned char md5_digest[16];

    SHA_CTX sha1_ctx;
    MD5_CTX md5_ctx;

    /* Clear the memory */
    md5output[0] = '\0';
    sha1output[0] = '\0';
    buf[2048 + 1] = '\0';

    /* Use prefilter_cmd if set */
    fp = (FILE *)prefilter(fname, prefilter_cmd);
	if(!fp)
		return(-1);

    /* Initialize both hashes */
    MD5Init(&md5_ctx);
    SHA1_Init(&sha1_ctx);

    /* Update for each one */
    while ((n = fread(buf, 1, 2048, fp)) > 0) {
        buf[n] = '\0';
        SHA1_Update(&sha1_ctx, buf, n);
        MD5Update(&md5_ctx, buf, (unsigned)n);
    }

    SHA1_Final(&(sha1_digest[0]), &sha1_ctx);
    MD5Final(md5_digest, &md5_ctx);

    /* Set output for MD5 */
    for (n = 0; n < 16; n++) {
        snprintf(md5output, 3, "%02x", md5_digest[n]);
        md5output += 2;
    }

    /* Set output for SHA-1 */
    for (n = 0; n < SHA_DIGEST_LENGTH; n++) {
        snprintf(sha1output, 3, "%02x", sha1_digest[n]);
        sha1output += 2;
    }

    /* Close it */
	prefilter_close(fp, prefilter_cmd);

    return (0);
}

