#include <stream.h>

#include <assert.h>

int64_t Stream::Read(void* buffer, size_t len){
    assert(!"Stream::Read called from base class");
    
    return -1; // We should not return but get the compiler to shut up
}

int64_t Stream::Write(void* buffer, size_t len){
    assert(!"Stream::Write called from base class");
    
    return -1; // We should not return but get the compiler to shut up
}

int64_t Stream::Empty(){
    return 1;
}

DataStream::DataStream(size_t bufSize){
    bufferSize = bufSize;
    bufferPos = 0;

    buffer = kmalloc(bufferSize);
}

DataStream::~DataStream(){
    kfree(buffer);
}

int64_t DataStream::Read(void* data, size_t len){
    if(len >= bufferPos) len = bufferPos;

    if(!len) return 0;

    acquireLock(&streamLock);

    memcpy(data, buffer, len);
    
    memcpy(buffer, buffer + len, bufferPos - len);
    bufferPos -= len;

    releaseLock(&streamLock);
    
    return len;
}

int64_t DataStream::Write(void* data, size_t len){
    acquireLock(&streamLock);
    if(bufferPos + len >= bufferPos){
        void* oldBuffer = buffer;

        bufferSize += (len + 512);
        void* buffer = kmalloc(bufferSize);

        memcpy(buffer, oldBuffer, bufferPos);

        kfree(oldBuffer);
    }

    memcpy(buffer + bufferPos, data, len);
    bufferPos += len;

    releaseLock(&streamLock);

    return len;
}

int64_t DataStream::Empty(){
    return !bufferPos;
}

int64_t PacketStream::Read(void* buffer, size_t len){
    if(packets.get_length() <= 0) return 0;

    stream_packet_t pkt = packets.remove_at(0);

    if(len > pkt.len) len = pkt.len;

    memcpy(buffer, pkt.data, len);

    kfree(pkt.data);

    return len;
}

int64_t PacketStream::Write(void* buffer, size_t len){
    stream_packet_t pkt;
    pkt.len = len;
    pkt.data = kmalloc(len);
    memcpy(pkt.data, buffer, len);

    packets.add_back(pkt);
}

int64_t PacketStream::Empty(){
    return !packets.get_length();
}