#include "partition.h"
#include "barnix.h"
static unsigned int part_u32(const unsigned char *p){return p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
int partition_validate(const PartitionTable *t) {
    for(int i=0;i<4;i++) {
        Partition a=t->entries[i];if(!a.type){if(a.start||a.size)return -1;continue;}
        if(a.type==0xee || a.type==5 || a.type==0x0f || a.type==0x85)return -1;
        if(!a.start||!a.size||a.start>=t->sectors||a.size>t->sectors-a.start)return -1;
        for(int j=0;j<i;j++){Partition b=t->entries[j];if(b.type&&a.start<b.start+b.size&&b.start<a.start+a.size)return -1;}
    }
    return 0;
}
int partition_read(PartitionTable *t) {
    memset(t,0,sizeof(*t));t->sectors=disk_selection().sectors;
    if(disk_selection().offset || disk_read(0,t->sector))return -1;
    if(t->sector[510]!=0x55 || t->sector[511]!=0xaa) {
        /* Never mistake an existing raw filesystem for unallocated space. */
        unsigned char super[512];if(!disk_read(2,super)&&super[56]==0x53&&super[57]==0xef)return -1;
        for(int i=0;i<512;i++)if(t->sector[i])return -1;
        return 0;
    }
    for(int i=0;i<4;i++) {
        unsigned char *p=t->sector+446+16*i;
        t->entries[i]=(Partition){part_u32(p+8),part_u32(p+12),p[4],p[0]};
    }
    return partition_validate(t);
}
int partition_add(PartitionTable *t,int slot,unsigned int start,unsigned int size,unsigned int type) {
    if(slot<0||slot>3||t->entries[slot].type||(type!=0x83&&type!=0xef)||start<2048||start%2048||!size)return -1;
    t->entries[slot]=(Partition){start,size,type,0};
    if(partition_validate(t)){memset(&t->entries[slot],0,sizeof(Partition));return -1;}return 0;
}
int partition_auto(PartitionTable *t,unsigned int size,unsigned int type) {
    int slot=-1;for(int i=0;i<4;i++)if(!t->entries[i].type){slot=i;break;}if(slot<0)return -1;
    unsigned int start=2048;
    for(int attempt=0;attempt<5;attempt++) {
        unsigned int end=t->sectors,next=start;
        for(int i=0;i<4;i++)if(t->entries[i].type) {
            Partition p=t->entries[i];
            if(p.start<=start&&p.start+p.size>start)next=p.start+p.size;
            else if(p.start>start&&p.start<end)end=p.start;
        }
        if(next!=start){if(next>0xfffff800U)return -1;start=(next+2047)&~2047U;continue;}
        unsigned int available=end>start?end-start:0,wanted=size?size:available;
        if(wanted&&available>=wanted&&!partition_add(t,slot,start,wanted,type))return slot;
        if(end==t->sectors||end>0xfffff800U)return -1;
        start=end;
    }
    return -1;
}
int partition_write(PartitionTable *t) {
    if(partition_validate(t))return -1;
    t->sector[510]=0x55;t->sector[511]=0xaa;
    for(int i=0;i<4;i++) {
        unsigned char *p=t->sector+446+16*i;Partition a=t->entries[i];
        if(!a.type){memset(p,0,16);continue;}
        if(p[4]!=a.type || part_u32(p+8)!=a.start || part_u32(p+12)!=a.size) {
            p[1]=p[5]=0xfe;p[2]=p[3]=p[6]=p[7]=0xff;
        }
        p[0]=a.boot;p[4]=a.type;
        for(int b=0;b<4;b++){p[8+b]=a.start>>(8*b);p[12+b]=a.size>>(8*b);}
    }
    unsigned char verify[512];
    return disk_write(0,t->sector)||disk_flush()||disk_read(0,verify)||memcmp(verify,t->sector,512)?-1:0;
}
