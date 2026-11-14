#ifndef BARNIX_PARTITION_H
#define BARNIX_PARTITION_H
#include "disk.h"
typedef struct { unsigned int start,size; unsigned char type,boot; } Partition;
typedef struct { unsigned char sector[512]; Partition entries[4]; unsigned int sectors; } PartitionTable;
int partition_read(PartitionTable *table);
int partition_validate(const PartitionTable *table);
int partition_add(PartitionTable *table,int slot,unsigned int start,unsigned int size,unsigned int type);
int partition_auto(PartitionTable *table,unsigned int size,unsigned int type);
int partition_write(PartitionTable *table);
#endif
