#ifndef PARTITIONCREATOR_H
#define PARTITIONCREATOR_H

#include <string>

class EventManager;

class PartitionCreator {
public:
    explicit PartitionCreator(EventManager *eventManager);
    ~PartitionCreator() = default;

    bool performDiskpartOperations(const std::string &format);
    bool verifyPartitionsCreated();

private:
    EventManager *eventManager;
};

#endif // PARTITIONCREATOR_H