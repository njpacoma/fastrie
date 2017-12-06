#include "global.h"
#include <signal.h>

simpleList_t* simpleList_create(sint32 initialLimit)
{
	simpleList_t* simpleList = (simpleList_t*)malloc(sizeof(simpleList_t));
	RtlZeroMemory(simpleList, sizeof(simpleList_t));

	if( initialLimit == 0 ) initialLimit = 4;
	simpleList->objectLimit = initialLimit;
	simpleList->objects = (void**)malloc(sizeof(void*) * simpleList->objectLimit);
	simpleList->isPreallocated = false;
	simpleList->stepScaler = 1;
	return simpleList;
}

// does not automatically free the subitems
void simpleList_free(simpleList_t* simpleList)
{
	if( simpleList->doNotFreeRawData == false )
		free(simpleList->objects);
	if( simpleList->isPreallocated == false )
		free(simpleList);
}

void simpleList_add(simpleList_t* simpleList, void* object) // todo: Via define macro it would be possible to always return a casted object?
{
	if( simpleList->objectCount == simpleList->objectLimit )
	{
		simpleList->objectLimit += (simpleList->objectLimit/2+1)*simpleList->stepScaler;
		if( simpleList->doNotFreeRawData )
		{
			void* oldObjectPtr = simpleList->objects;
			simpleList->objects = (void**)malloc(sizeof(void*) * simpleList->objectLimit);
			RtlCopyMemory(simpleList->objects, oldObjectPtr, sizeof(void*) * simpleList->objectCount);
		}
		else
			simpleList->objects = (void**)realloc(simpleList->objects, sizeof(void*) * simpleList->objectLimit);
		simpleList->doNotFreeRawData = false;
	}
	simpleList->objects[simpleList->objectCount] = object;
	simpleList->objectCount++;
}

stream_t *stream_create(streamSettings_t *settings, void *object)
{
	stream_t *stream = (stream_t*)malloc(sizeof(stream_t));
	stream->settings = settings;
	stream->object = object;
	stream->bitIndex = 0;
	stream->bitReadIndex = 0;
	stream->bitReadBufferState = 0;
	if( stream->settings->initStream )
		stream->settings->initStream(object, stream);
	return stream;
}

void stream_destroy(stream_t *stream)
{
	if( stream->settings->destroyStream )
		stream->settings->destroyStream(stream->object, stream);
	free(stream);
}

/* writing */
void stream_writeU8(stream_t *stream, uint8 value)
{
	stream->settings->writeData(stream->object, (void*)&value, 1);
}

void stream_writeU16(stream_t *stream, uint16 value)
{
	stream->settings->writeData(stream->object, (void*)&value, 2);
}

void stream_writeU32(stream_t *stream, uint32 value)
{
	stream->settings->writeData(stream->object, (void*)&value, 4);
}

uint32 stream_writeData(stream_t *stream, void *data, int len)
{
	return stream->settings->writeData(stream->object, (void*)data, len);
}

char stream_readS8(stream_t *stream)
{
	char value;
	stream->settings->readData(stream->object, (void*)&value, 1);
	return value;
}

int stream_readS32(stream_t *stream)
{
	int value;
	stream->settings->readData(stream->object, (void*)&value, 4);
	return value;
}

float stream_readFloat(stream_t *stream)
{
	float value;
	stream->settings->readData(stream->object, (void*)&value, 4);
	return value;
}

uint32 stream_readData(stream_t *stream, void *data, int len)
{
	return stream->settings->readData(stream->object, data, len);
}

void stream_setSeek(stream_t *stream, uint32 seek)
{
	stream->settings->setSeek(stream->object, seek, false);
}

uint32 stream_getSize(stream_t *stream)
{
	if( stream->settings->getSize == NULL )
		return 0xFFFFFFFF;
	return stream->settings->getSize(stream->object);
}

/* memory streams */

typedef struct  
{
	//bool readMode;
	uint32 sizeLimit;
	uint8 *buffer;
	uint32 bufferPosition; /* seek */
	uint32 bufferSize;
	uint32 bufferLimit;
	bool disallowWrite;
	bool doNotFreeMemory;
}streamEx_dynamicMemoryRange_t;

uint32 streamEx_dynamicMemoryRange_readData(void *object, void *buffer, uint32 len)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	uint32 bytesToRead = std::min(len, memoryRangeObj->bufferSize - memoryRangeObj->bufferPosition);
	RtlCopyMemory(buffer, memoryRangeObj->buffer + memoryRangeObj->bufferPosition, bytesToRead);
	memoryRangeObj->bufferPosition += bytesToRead;
	return bytesToRead;
}

uint32 streamEx_dynamicMemoryRange_writeData(void *object, void *buffer, uint32 len)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	if( memoryRangeObj->disallowWrite )
		return 0;
	uint32 nLen = len;
	uint8* bBuffer = (uint8*)buffer;
	uint32 bytesToWrite;
	uint32 overwriteSize = memoryRangeObj->bufferSize - memoryRangeObj->bufferPosition; // amount of bytes that can be written without exceeding the buffer size
	if( overwriteSize )
	{
		bytesToWrite = std::min(overwriteSize, len);
		RtlCopyMemory(memoryRangeObj->buffer + memoryRangeObj->bufferPosition, bBuffer, bytesToWrite);
		memoryRangeObj->bufferPosition += bytesToWrite;
		nLen -= bytesToWrite;
		bBuffer += bytesToWrite;
	}
	if( nLen == 0 )
		return len;
	// bytes left that exceed the current buffer size
	uint32 bufferBytesLeft = memoryRangeObj->bufferLimit - memoryRangeObj->bufferPosition;
	// need to enlarge buffer?
	if( bufferBytesLeft < nLen )
	{
		uint32 enlargeSize = 0;
		uint32 minimalEnlargeSize = nLen - bufferBytesLeft;
		// calculate ideal enlargement size (important)
		uint32 idealEnlargeSize = memoryRangeObj->bufferLimit/4; // 25% of current buffer size
		if( idealEnlargeSize < minimalEnlargeSize ) // if idealSize < neededSize
			enlargeSize = idealEnlargeSize + minimalEnlargeSize; // take the sum of both
		else
			enlargeSize = idealEnlargeSize; // just use the idealSize
		// check with maximum size
		uint32 maxEnlargeSize = memoryRangeObj->sizeLimit - memoryRangeObj->bufferLimit;
		enlargeSize = std::min(maxEnlargeSize, enlargeSize);
		if( enlargeSize )
		{
			// enlarge
			uint32 newLimit = memoryRangeObj->bufferLimit + enlargeSize;
			uint8* newBuffer = (uint8*)malloc(newLimit);
			if( newBuffer ) // check if we could allocate the memory
			{
				// copy old buffer and free it
				RtlCopyMemory(newBuffer, memoryRangeObj->buffer, memoryRangeObj->bufferSize);
				free(memoryRangeObj->buffer);
				memoryRangeObj->buffer = newBuffer;
				// set new limit
				memoryRangeObj->bufferLimit = newLimit;
			}
		}
	}
	// write boundary data
	bufferBytesLeft = memoryRangeObj->bufferLimit - memoryRangeObj->bufferPosition;
	bytesToWrite = std::min(bufferBytesLeft, nLen);
	if( bytesToWrite )
	{
		RtlCopyMemory(memoryRangeObj->buffer + memoryRangeObj->bufferPosition, bBuffer, bytesToWrite);
		memoryRangeObj->bufferPosition += bytesToWrite;
		memoryRangeObj->bufferSize += bytesToWrite;
		nLen -= bytesToWrite;
		bBuffer += bytesToWrite;
	}
	// return amount of written bytes
	return len - nLen;
}

uint32 streamEx_dynamicMemoryRange_getSize(void *object)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	return memoryRangeObj->bufferSize;
}

void streamEx_dynamicMemoryRange_setSize(void *object, uint32 size)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	if( memoryRangeObj->disallowWrite )
		return;
	if( size < memoryRangeObj->bufferSize )
		memoryRangeObj->bufferSize = size;
}

uint32 streamEx_dynamicMemoryRange_getSeek(void *object)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	return memoryRangeObj->bufferPosition;
}

void streamEx_dynamicMemoryRange_setSeek(void *object, sint32 seek, bool relative)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	if (seek < 0) seek = 0;
	memoryRangeObj->bufferPosition = seek;
	if( memoryRangeObj->bufferPosition > memoryRangeObj->bufferSize ) memoryRangeObj->bufferPosition = memoryRangeObj->bufferSize;
}

void streamEx_dynamicMemoryRange_initStream(void *object, stream_t *stream)
{
	// all init is already done in streamCreate function
}

void streamEx_dynamicMemoryRange_destroyStream(void *object, stream_t *stream)
{
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)object;
	if( memoryRangeObj->doNotFreeMemory == false )
		free(memoryRangeObj->buffer);
	free(object);
}

streamSettings_t streamEx_dynamicMemoryRange_settings =
{
	streamEx_dynamicMemoryRange_readData,
	streamEx_dynamicMemoryRange_writeData,
	streamEx_dynamicMemoryRange_getSize,
	streamEx_dynamicMemoryRange_setSize,
	streamEx_dynamicMemoryRange_getSeek,
	streamEx_dynamicMemoryRange_setSeek,
	streamEx_dynamicMemoryRange_initStream,
	streamEx_dynamicMemoryRange_destroyStream,
	// general settings
	true//bool allowCaching;
};



stream_t* streamEx_fromMemoryRange(void *mem, uint32 memoryLimit)
{
	stream_t *stream = (stream_t*)malloc(sizeof(stream_t));
	RtlZeroMemory(stream, sizeof(stream_t));
	stream->settings = &streamEx_dynamicMemoryRange_settings;
	// init object
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)malloc(sizeof(streamEx_dynamicMemoryRange_t));
	RtlZeroMemory(memoryRangeObj, sizeof(streamEx_dynamicMemoryRange_t));
	stream->object = memoryRangeObj;
	// init object
	memoryRangeObj->bufferSize = memoryLimit;
	memoryRangeObj->buffer = (uint8*)mem;
	memoryRangeObj->bufferPosition = 0;
	memoryRangeObj->disallowWrite = true;
	memoryRangeObj->doNotFreeMemory = true;
	// call subinit
	if( stream->settings->initStream )
		stream->settings->initStream(memoryRangeObj, stream);
	return stream;
}

/*
 * Creates a new dynamically sized memory buffer.
 * @memoryLimit - The maximum size of the dynamic buffer (set to 0xFFFFFFFF to ignore limit)
 * When reading from the buffer you can only read what has already been written. The memory buffer behaves like a virtual file.
 */
stream_t* streamEx_fromDynamicMemoryRange(uint32 memoryLimit)
{
	stream_t *stream = (stream_t*)malloc(sizeof(stream_t));
	RtlZeroMemory(stream, sizeof(stream_t));
	stream->settings = &streamEx_dynamicMemoryRange_settings;
	// init object
	streamEx_dynamicMemoryRange_t* memoryRangeObj = (streamEx_dynamicMemoryRange_t*)malloc(sizeof(streamEx_dynamicMemoryRange_t));
	RtlZeroMemory(memoryRangeObj, sizeof(streamEx_dynamicMemoryRange_t));
	stream->object = memoryRangeObj;
	// alloc 1KB and setup memoryRangeObj
	memoryRangeObj->sizeLimit = memoryLimit;
	if( memoryLimit < 1024 )
		memoryRangeObj->bufferLimit = memoryLimit;
	else
		memoryRangeObj->bufferLimit = 1024;
	memoryRangeObj->bufferSize = 0;
	memoryRangeObj->buffer = (uint8*)malloc(memoryRangeObj->bufferLimit);
	memoryRangeObj->bufferPosition = 0;
	memoryRangeObj->disallowWrite = false;
	// call subinit
	if( stream->settings->initStream )
		stream->settings->initStream(memoryRangeObj, stream);
	return stream;
}

/* substreams */

typedef struct  
{
	stream_t* stream;
	sint32 currentOffset;
	// substream settings
	sint32 baseOffset;
	sint32 size;
}streamEx_substream_t;

uint32 streamEx_substream_readData(void *object, void *buffer, uint32 len)
{
	streamEx_substream_t* substream = (streamEx_substream_t*)object;
	uint32 readLimit = substream->size - substream->currentOffset;
	if( readLimit < len ) len = readLimit;
	// set seek (in case we share the stream object)
	stream_setSeek(substream->stream, substream->baseOffset + substream->currentOffset);
	uint32 realRead = stream_readData(substream->stream, buffer, len);
	substream->currentOffset += realRead;
	return realRead;
}

uint32 streamEx_substream_writeData(void *object, void *buffer, uint32 len)
{	
	__debugbreak(); // no write access for substreams?
	return 0;
}

uint32 streamEx_substream_getSize(void *object)
{
	streamEx_substream_t* substream = (streamEx_substream_t*)object;
	return substream->size;
}

void streamEx_substream_setSize(void *object, uint32 size)
{
	__debugbreak(); // not implemented 
}

uint32 streamEx_substream_getSeek(void *object)
{
	streamEx_substream_t* substream = (streamEx_substream_t*)object;
	return substream->currentOffset;
}

void streamEx_substream_setSeek(void *object, sint32 seek, bool relative)
{
	streamEx_substream_t* substream = (streamEx_substream_t*)object;
	substream->currentOffset = seek;
	if( substream->currentOffset < 0 ) substream->currentOffset = 0;
	if( substream->currentOffset > substream->size ) substream->currentOffset = substream->size;
}

void streamEx_substream_initStream(void *object, stream_t *stream)
{

}

void streamEx_substream_destroyStream(void *object, stream_t *stream)
{
	free(object);
}

streamSettings_t streamEx_substream_settings =
{
	streamEx_substream_readData,
	streamEx_substream_writeData,
	streamEx_substream_getSize,
	streamEx_substream_setSize,
	streamEx_substream_getSeek,
	streamEx_substream_setSeek,
	streamEx_substream_initStream,
	streamEx_substream_destroyStream,
	// general settings
	true//bool allowCaching;
};

/*
 * Creates a read-only substream object.
 * The substream can only access data inside the given startOffset + size range.
 */
stream_t* streamEx_createSubstream(stream_t* mainstream, sint32 startOffset, sint32 size)
{
	stream_t *stream = (stream_t*)malloc(sizeof(stream_t));
	RtlZeroMemory(stream, sizeof(stream_t));
	stream->settings = &streamEx_substream_settings;
	// init object
	streamEx_substream_t* substream = (streamEx_substream_t*)malloc(sizeof(streamEx_dynamicMemoryRange_t));
	RtlZeroMemory(substream, sizeof(streamEx_dynamicMemoryRange_t));
	stream->object = substream;
	// setup substream
	substream->baseOffset = startOffset;
	substream->currentOffset = 0;
	substream->size = size;
	substream->stream = mainstream;
	// done
	return stream;
}

/*
  Other useful stuff
 */

// copies the contents of the stream to a memory buffer
void* streamEx_map(stream_t* stream, sint32* size)
{
	stream_setSeek(stream, 0);
	sint32 rSize = stream_getSize(stream);
	*size = rSize;
	if( rSize == 0 )
		return (void*)""; // return any valid memory address 
	void* mem = malloc(rSize);
	stream_readData(stream, (void*)mem, rSize);
	return mem;
}
