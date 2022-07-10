#include <stdio.h>
#include <string.h>

static const int kHFSBlockSize = 512;

int freadAll(unsigned char *buffer, size_t size, FILE *f)
{
	if (fread(buffer, 1, size, f) == size)
		return 0;
	return -1;
}

int fwriteAll(const unsigned char *buffer, size_t size, FILE *f)
{
	if (fwrite(buffer, 1, size, f) == size)
		return 0;
	return -1;
}

static const size_t kWindowSize = 1 << 16;

void insertSL(unsigned char *sl, size_t &slPosRef, const unsigned char *bytesToInsert, size_t size)
{
	size_t slPos = slPosRef;

	size_t available = kWindowSize - slPos;
	if (available < size)
	{
		memcpy(sl + slPos, bytesToInsert, available);
		slPos = 0;
		insertSL(sl, slPos, bytesToInsert + available, size - available);
	}
	else
	{
		memcpy(sl + slPos, bytesToInsert, size);
		slPos += size;
	}

	slPosRef = slPos;
}

void readSL(const unsigned char *sl, size_t pos, unsigned char *outBuf, size_t size)
{
	size_t available = kWindowSize - pos;
	if (available < size)
	{
		memcpy(outBuf, sl + pos, available);
		readSL(sl, 0, outBuf + available, size - available);
	}
	else
		memcpy(outBuf, sl + pos, size);
}

void readLZ(const unsigned char *sl, size_t slPos, unsigned char *outBuf, size_t codedOffset, size_t length)
{
	size_t actualOffset = codedOffset + 1;

	const size_t readPos = (slPos + kWindowSize - actualOffset) % kWindowSize;

	// Repeating sequence
	while (actualOffset < length)
	{
		readSL(sl, readPos, outBuf, actualOffset);
		outBuf += actualOffset;
		length -= actualOffset;
	}

	// Copy
	readSL(sl, readPos, outBuf, length);
}

int decompress(FILE *inF, FILE *outF, size_t decompressSize)
{
	unsigned char sl[kWindowSize];
	unsigned char lzBytes[128];
	size_t slPos = 0;
	size_t chunkSize = 0;

	while (decompressSize > 0)
	{
		unsigned char codeBytes[3];
		if (freadAll(codeBytes, 1, inF))
			return -1;

		decompressSize--;

		if (codeBytes[0] & 0x80)
		{
			// Literal
			chunkSize = (codeBytes[0] & 0x7f) + 1;

			if (chunkSize > decompressSize)
				return -1;

			if (freadAll(lzBytes, chunkSize, inF))
				return -1;

			decompressSize -= chunkSize;
		}
		else if (codeBytes[0] & 0x40)
		{
			// Large offset
			if (decompressSize < 2)
				return -1;

			if (freadAll(codeBytes + 1, 2, inF))
				return -1;

			decompressSize -= 2;

			chunkSize = (codeBytes[0] & 0x3f) + 4;
			size_t offset = (codeBytes[1] << 8) + codeBytes[2];

			readLZ(sl, slPos, lzBytes, offset, chunkSize);
		}
		else
		{
			// Small offset
			if (decompressSize < 1)
				return -1;

			if (freadAll(codeBytes + 1, 1, inF))
				return -1;

			decompressSize -= 1;

			chunkSize = ((codeBytes[0] & 0x3c) >> 2) + 3;
			size_t offset = ((codeBytes[0] & 0x3) << 8) + codeBytes[1];

			readLZ(sl, slPos, lzBytes, offset, chunkSize);
		}

		if (fwriteAll(lzBytes, chunkSize, outF))
			return -1;

		insertSL(sl, slPos, lzBytes, chunkSize);
	}

	return 0;
}

int bulkCopy(FILE *inF, FILE *outF, size_t amount)
{
	unsigned char buffer[4096];
	while (amount)
	{
		size_t blockAmount = sizeof(buffer);
		if (blockAmount > amount)
			blockAmount = amount;

		if (freadAll(buffer, blockAmount, inF))
			return -1;

		if (fwriteAll(buffer, blockAmount, outF))
			return -1;

		amount -= blockAmount;
	}

	return 0;
}

int main(int argc, const char **argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "Usage: DecompressImage <input> <output>");
		return -1;
	}

	unsigned char blockBuffer[kHFSBlockSize];

	FILE *inF = fopen(argv[1], "rb");
	if (!inF)
	{
		fprintf(stderr, "Failed to open input file");
		return -1;
	}

	if (fseek(inF, -kHFSBlockSize, SEEK_END))
	{
		fprintf(stderr, "Couldn't seek to MDB");
		return -1;
	}

	long altMDBLoc = ftell(inF);
	if (freadAll(blockBuffer, kHFSBlockSize, inF))
	{
		fprintf(stderr, "Couldn't read MDB");
		return -1;
	}

	if (blockBuffer[0] != 'B' || blockBuffer[1] != 'D')
	{
		fprintf(stderr, "This doesn't look like a disk image");
		return -1;
	}

	unsigned int numAllocationBlocks = (blockBuffer[18] << 8) + blockBuffer[19];
	size_t allocationBlockSize = (blockBuffer[20] << 24) + (blockBuffer[21] << 16) + (blockBuffer[22] << 8) + blockBuffer[23];
	unsigned int firstAllocationBlock = (blockBuffer[28] << 8) + blockBuffer[29];

	size_t compressedDataStart = firstAllocationBlock * allocationBlockSize;
	size_t compressedDataEnd = altMDBLoc;	// ?

	if (fseek(inF, 0, SEEK_SET))
	{
		fprintf(stderr, "Couldn't seek to start");
		return -1;
	}

	FILE *outF = fopen(argv[2], "wb");
	if (!outF)
	{
		fprintf(stderr, "Failed to open output file");
		return -1;
	}

	if (bulkCopy(inF, outF, compressedDataStart))
	{
		fprintf(stderr, "Failed to bulk copy start");
		return -1;
	}

	size_t compressedAmount = compressedDataEnd - compressedDataStart;
	if (decompress(inF, outF, compressedAmount))
	{
		fprintf(stderr, "Failed to decompress data");
		return -1;
	}

	if (fseek(inF, altMDBLoc, SEEK_SET))
	{
		fprintf(stderr, "Failed to seek to alt MDB");
		return -1;
	}

	if (bulkCopy(inF, outF, kHFSBlockSize))
	{
		fprintf(stderr, "Failed to bulk copy alt MDB");
		return -1;
	}

	fclose(inF);
	fclose(outF);

	return 0;
}
