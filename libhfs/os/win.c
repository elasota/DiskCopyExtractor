/*
 * Copyright (C) 2022 Eric Lasota
 * Based on libhfs
 * 
 * libhfs - library for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996-1998 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>

#include "../libhfs.h"

/*
 * NAME:	os->open()
 * DESCRIPTION:	open and lock a new descriptor from the given path and mode
 */
int os_open(void **priv, const char *path, int mode)
{
    const char *privs = "rb";

    switch (mode)
    {
    case HFS_MODE_RDONLY:
        privs = "rb";
        break;

    case HFS_MODE_RDWR:
    default:
        privs = "rb+";
        break;
    }

    FILE *f = fopen(path, privs);

    if (!f)
        return -1;

    *priv = f;
    return 0;
}

/*
 * NAME:	os->close()
 * DESCRIPTION:	close an open descriptor
 */
int os_close(void **priv)
{
    FILE *f = (FILE *)*priv;
    if (f)
        fclose(f);

    *priv = NULL;

    return 0;
}

/*
 * NAME:	os->same()
 * DESCRIPTION:	return 1 iff path is same as the open descriptor
 */
int os_same(void **priv, const char *path)
{
    return 0;
}

/*
 * NAME:	os->seek()
 * DESCRIPTION:	set a descriptor's seek pointer (offset in blocks)
 */
unsigned long os_seek(void **priv, unsigned long offset)
{
    FILE *f = (FILE *)*priv;
    int result;

    /* offset == -1 special; seek to last block of device */

    if (offset == (unsigned long)-1)
        result = fseek(f, 0, SEEK_END);
    else
        result = fseek(f, offset << HFS_BLOCKSZ_BITS, SEEK_SET);

    if (result < 0)
        return result;

    return (unsigned long)ftell(f) >> HFS_BLOCKSZ_BITS;
}

/*
 * NAME:	os->read()
 * DESCRIPTION:	read blocks from an open descriptor
 */
unsigned long os_read(void **priv, void *buf, unsigned long len)
{
    FILE *f = (FILE *)*priv;

    size_t result = fread(buf, 1, len << HFS_BLOCKSZ_BITS, f);

    return (unsigned long)result >> HFS_BLOCKSZ_BITS;
}

/*
 * NAME:	os->write()
 * DESCRIPTION:	write blocks to an open descriptor
 */
unsigned long os_write(void **priv, const void *buf, unsigned long len)
{
    FILE *f = (FILE *)*priv;

    size_t result = fwrite(buf, 1, len << HFS_BLOCKSZ_BITS, f);

    return (unsigned long)result >> HFS_BLOCKSZ_BITS;
}
