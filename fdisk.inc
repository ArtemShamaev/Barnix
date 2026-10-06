static int part_number(const char *s,unsigned int *out) {
    unsigned int n=0;if(!*s)return -1;
    for(;*s;s++){if(*s<'0'||*s>'9'||n>(0xffffffffU-(*s-'0'))/10)return -1;n=n*10+*s-'0';}
    *out=n;return 0;
}
int fs_fdisk(int argc,const char *const *argv) {
    if(!permissions_root()){println(RED,"fdisk requires root");return -13;}
    if(argc<3){println(WHITE,"fdisk DEVICE list | new SLOT START_MiB SIZE_MiB | auto SIZE_MiB | delete SLOT | wipe DEVICE");return -1;}
    namespace_start();DiskSelection previous=disk_selection();PartitionTable table;int result=-1;
    if(disk_select_raw(argv[1])){disk_restore(previous);return -1;}
    DiskSelection target=disk_selection();
    if(target.offset)goto finish;
    if(!strcmp(argv[2],"list")) {
        if(argc!=3||partition_read(&table))goto finish;
        println(WHITE,"SLOT START_LBA SECTORS TYPE");
        for(int i=0;i<4;i++)if(table.entries[i].type){print_int(WHITE,i+1);print(WHITE," ");print_uint(WHITE,table.entries[i].start);print(WHITE," ");print_uint(WHITE,table.entries[i].size);print(WHITE," ");print_uint(WHITE,table.entries[i].type);println(WHITE,"");}
        result=0;goto finish;
    }
    for(int i=0;i<MOUNT_COUNT;i++)if(mounts[i].used&&mounts[i].disk.id==target.id){println(RED,"Unmount all partitions of this disk first");goto finish;}
    if(!strcmp(argv[2],"wipe")) {
        if(argc!=4||strcmp(argv[1],argv[3]))goto finish;
        memset(&table,0,sizeof(table));table.sectors=target.sectors;
    } else {
        if(partition_read(&table)){println(RED,"Invalid/unsupported partition table (GPT and extended partitions are not supported)");goto finish;}
        unsigned int slot,start,size;
        if(!strcmp(argv[2],"new")) {
            if(argc!=6||part_number(argv[3],&slot)||part_number(argv[4],&start)||part_number(argv[5],&size)||start>0x1fffff||size>0x1fffff||partition_add(&table,slot-1,start*2048,size*2048,0x83))goto finish;
        } else if(!strcmp(argv[2],"auto")) {
            if(argc!=4||part_number(argv[3],&size)||!size||size>0x1fffff||partition_auto(&table,size*2048,0x83)<0)goto finish;
        } else if(!strcmp(argv[2],"delete")) {
            if(argc!=4||part_number(argv[3],&slot)||slot<1||slot>4)goto finish;
            memset(&table.entries[slot-1],0,sizeof(Partition));
        } else goto finish;
    }
    result=partition_write(&table);
finish:
    disk_restore(previous);return result;
}
