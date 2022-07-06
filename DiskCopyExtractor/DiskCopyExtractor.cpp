#include <stdio.h>

#include <string>

extern "C"
{
#include "libhfs.h"
}

#include "XModemCRC.h"

void PutWordBE(unsigned char *bytes, short dword)
{
	bytes[0] = ((dword >> 8) & 0xff);
	bytes[1] = (dword & 0xff);
}

void PutLongBE(unsigned char *bytes, int dword)
{
	bytes[0] = ((dword >> 24) & 0xff);
	bytes[1] = ((dword >> 16) & 0xff);
	bytes[2] = ((dword >> 8) & 0xff);
	bytes[3] = (dword & 0xff);
}

int main(int argc, const char **argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "Usage: DiskCopyExtractor <volume file> <file path in HFS volume> <output>");
		return -1;
	}
	hfsvol *vol = hfs_mount(argv[1], 0, HFS_MODE_RDONLY);

	if (!vol)
	{
		fprintf(stderr, "Couldn't mount HFS volume");
		return -1;
	}

	const char *filePath = argv[2];

	hfsdirent hfsDirEnt;
	if (hfs_stat(vol, filePath, &hfsDirEnt))
	{
		fprintf(stderr, "Couldn't read HFS file stat");
		return -1;
	}

	if (hfsDirEnt.flags & HFS_ISDIR)
	{
		fprintf(stderr, "HFS entity was not a file");
		return -1;
	}
	
	unsigned char mb2Header[128];
	memset(mb2Header, 0, sizeof(mb2Header));

	size_t nameLength = strlen(hfsDirEnt.name);
	mb2Header[1] = nameLength;
	memcpy(mb2Header + 2, hfsDirEnt.name, nameLength);

	memcpy(mb2Header + 65, hfsDirEnt.u.file.type, 4);
	memcpy(mb2Header + 69, hfsDirEnt.u.file.creator, 4);
	mb2Header[73] = ((hfsDirEnt.fdflags >> 8) & 0xff);
	PutWordBE(mb2Header + 75, hfsDirEnt.fdlocation.v);
	PutWordBE(mb2Header + 77, hfsDirEnt.fdlocation.h);
	PutLongBE(mb2Header + 83, hfsDirEnt.u.file.dsize);
	PutLongBE(mb2Header + 87, hfsDirEnt.u.file.rsize);
	PutLongBE(mb2Header + 91, hfsDirEnt.crdatehfs);
	PutLongBE(mb2Header + 95, hfsDirEnt.mddatehfs);
	mb2Header[101] = (hfsDirEnt.fdflags & 0xff);
	mb2Header[122] = 129;
	mb2Header[123] = 129;

	PutLongBE(mb2Header + 124, XModemCRC(mb2Header, 124, 0));

	FILE *outF = fopen(argv[3], "wb");
	if (!outF)
	{
		fprintf(stderr, "Failed to open output file");
		return -1;
	}

	if (fwrite(mb2Header, 1, 128, outF) != 128)
	{
		fprintf(stderr, "Failure writing block");
			return -1;
	}

	unsigned char padding[128];
	memset(padding, 0, sizeof(padding));

	hfsfile *hfsF = hfs_open(vol, filePath);
	int forkSizes[2] = { hfsDirEnt.u.file.dsize, hfsDirEnt.u.file.rsize };
	for (int i = 0; i < 2; i++)
	{
		if (forkSizes[i] == 0)
			continue;

		if (hfs_setfork(hfsF, i))
		{
			fprintf(stderr, "Failed to select fork %i", i);
			return -1;
		}

		unsigned char buffer[1024];
		size_t copySize = forkSizes[i];

		hfs_seek(hfsF, 0, HFS_SEEK_SET);

		while (copySize > 0)
		{
			size_t blockSize = sizeof(buffer);
			if (blockSize > copySize)
				blockSize = copySize;

			if (hfs_read(hfsF, buffer, blockSize) != static_cast<long>(blockSize))
			{
				fprintf(stderr, "Failure copying block");
				return -1;
			}

			if (fwrite(buffer, 1, blockSize, outF) != blockSize)
			{
				fprintf(stderr, "Failure writing block");
				return -1;
			}

			copySize -= blockSize;
		}

		if (forkSizes[i] % 128 != 0)
		{
			size_t paddingRequired = 128 - (forkSizes[i] % 128);

			if (fwrite(padding, 1, paddingRequired, outF) != paddingRequired)
			{
				fprintf(stderr, "Failure writing padding");
				return -1;
			}

		}
	}

	hfs_close(hfsF);

	fclose(outF);

	hfs_umount(vol);

	return 0;
}
