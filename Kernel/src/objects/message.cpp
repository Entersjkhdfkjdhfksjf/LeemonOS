#include <objects/message.h>

#include <errno.h>
#include <logging.h>

MessageEndpoint::MessageEndpoint(uint16_t maxSize){
    maxMessageSize = maxSize;

    if(maxMessageSize > maxMessageSizeLimit){
        maxMessageSize = maxMessageSizeLimit;
    }

    messageQueueLimit = 0x300000 / maxMessageSize; // No more than 3MB

    queueAvailablilitySemaphore.SetValue(messageQueueLimit);

    if(debugLevelMessageEndpoint >= DebugLevelNormal){
        Log::Info("[MessageEndpoint] new endpoint with message size of %u (Queue limit: %u)", maxMessageSize, messageQueueLimit);
    }
}

MessageEndpoint::~MessageEndpoint(){
    Destroy();
}

void MessageEndpoint::Destroy(){
    if(peer.get()){
        peer->peer = nullptr;
    }
}

int64_t MessageEndpoint::Read(uint64_t* id, uint16_t* size, uint8_t* data){
    assert(id);
    assert(size);
    assert(data);

    if(queue.Empty()){
        if(!peer.get()){
            return -ENOTCONN;
        }
        
        return 0;
    }

    acquireLock(&queueLock);

    queue.Dequeue(reinterpret_cast<uint8_t*>(id), sizeof(uint64_t));
    queue.Dequeue(reinterpret_cast<uint8_t*>(size), sizeof(uint16_t));

    if(*size){
        size_t read = queue.Dequeue(data, *size);
        if(read < *size){
            Log::Warning("[MessageEndpoint] Draining message queue (expected %u bytes, only got %u)!", *size, read);
            queue.Drain(); // Not all data has been written, drain the buffer

            releaseLock(&queueLock);
            return 0;
        }
    }

    releaseLock(&queueLock);

    queueAvailablilitySemaphore.Signal();

    if(debugLevelMessageEndpoint >= DebugLevelVerbose){
        Log::Info("[MessageEndpoint] Receiving message (ID: %u, Size: %u)", *id, *size);
    }

    return 1;
}

int64_t MessageEndpoint::Call(uint64_t id, uint16_t size, uint64_t data, uint64_t rID, uint16_t* rSize, uint8_t* rData, int64_t timeout){
    if(!peer.get()){
        return -ENOTCONN;
    }

    if(size > maxMessageSize){
        return -EINVAL;
    }

    assert(rSize);
    assert(rData);
    
    Semaphore s = Semaphore(0);
    acquireLock(&waitingResponseLock);
    
    uint8_t* buffer = nullptr;
    uint16_t returnSize; // We cannot use rSize as it lies within the process' address space, not the kernel's
    waitingResponse.add_back({&s, {.id = rID, .size = &returnSize, .buffer = &buffer}});

    releaseLock(&waitingResponseLock);

    Write(id, size, data); // Send message

    // TODO: timeout
    if(s.Wait()){ // Await response
        if(buffer){
            delete buffer;
        }

        return -EINTR; // Interrupted
    }
    
    if(buffer){
        memcpy(rData, buffer, returnSize);
        *rSize = returnSize;

        delete buffer;
    }
    return 0;
}

int64_t MessageEndpoint::Write(uint64_t id, uint16_t size, uint64_t data){
    if(!peer.get()){
        return -ENOTCONN;
    }

    if(size > maxMessageSize){
        return -EINVAL;
    }

    acquireLock(&peer->waitingResponseLock);
    for(auto it = peer->waitingResponse.begin(); it != peer->waitingResponse.end(); it++){
        if(it->item2.id == id){
            Response& response = it->item2;

            if(size){
                *it->item2.buffer = new uint8_t[size];

                memcpy(*it->item2.buffer, reinterpret_cast<uint8_t*>(data), size);
            }

            *response.size = size;

            it->item1->Signal();

            if(debugLevelMessageEndpoint >= DebugLevelVerbose){
                Log::Info("[MessageEndpoint] Sending response (ID: %u, Size: %u) to peer", id, size);
            }

            peer->waitingResponse.remove(it);
            releaseLock(&peer->waitingResponseLock);
            return 0; // Skip queue entirely
        }
    }
    releaseLock(&peer->waitingResponseLock);

    if(queueAvailablilitySemaphore.Wait()){
        return -EINTR;
    }

    acquireLock(&peer->queueLock);
    if(size) {
        peer->queue.EnqueueObjects(id, size, Pair((uint8_t*)data, size));
    } else {
        peer->queue.EnqueueObjects(id, size);
    }

    acquireLock(&peer->waitingLock);
    while(peer->waiting.get_length() > 0){
        peer->waiting.remove_at(0)->Signal();
    }
    releaseLock(&peer->waitingLock);

    if(debugLevelMessageEndpoint >= DebugLevelVerbose){
        Log::Info("[MessageEndpoint] Sending message (ID: %u, Size: %u) to peer", id, size);
    }

    releaseLock(&peer->queueLock);
    return 0;
}