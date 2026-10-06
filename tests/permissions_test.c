#define main original_main
#include "fs_test.c"
#undef main
int main(int argc,char **argv) {
    CHECK(argc==2);void *file=fopen(argv[1],"rb");CHECK(file);
    CHECK(fread(test_disk,1,sizeof(test_disk),file)==sizeof(test_disk));fclose(file);
    CHECK(!fs_init());
    unsigned char digest[32];sha256((const unsigned char *)"abc",3,digest);
    static const unsigned char expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    CHECK(!memcmp(digest,expected,32));
    CHECK(!account_create("root","rootpw"));CHECK(!account_create("alice","alicepw"));CHECK(!account_create("bob","bobpw"));
    CHECK(account_create("../bad","pw"));CHECK(account_create("alice","pw"));
    CHECK(!authenticate_user("alice","alicepw"));CHECK(authenticate_user("alice","wrong"));
    char data[4096];int n=fs_read("/etc/userpasswd.cfg",0,data,4095);CHECK(n>0);data[n]=0;
    CHECK(strstr(data,"$bx1$")!=NULL);CHECK(strstr(data,"alicepw")==NULL);
    CHECK(!fs_mkdir("/bin"));CHECK(!fs_write("/bin/test","test",4));
    CHECK(!set_file_permissions("/bin/test","alice=x",""));
    CHECK(!fs_write("/home/alice/locked","secret",6));
    CHECK(!set_file_permissions("/home/alice/locked","alice=r","unlock"));
    CHECK(!switch_user("alice",""));CHECK(!permissions_root());CHECK(permissions_uid()==1000);
    CHECK(!fs_write("mine","hello",5));CHECK(fs_size("/home/alice/mine")==5);
    CHECK(fs_write("/etc/userpasswd.cfg","x",1)==-13);
    CHECK(fs_append("/etc/userpasswd.cfg","x",1)==-13);
    CHECK(fs_read("/etc/userpasswd.cfg",0,data,10)==-13);
    CHECK(fs_write("../../etc/userpasswd.cfg","x",1)==-13);
    CHECK(fs_write("/home/bob/x","x",1)==-13);
    CHECK(fs_write("/home/alice2","x",1)==-13);
    CHECK(fs_read("/bin/test",0,data,4)==-13);
    CHECK(fs_load_executable("/bin/test",data,sizeof(data))==4);
    CHECK(!memcmp(data,"test",4));
    CHECK(fs_write("locked","x",1)==-13);
    CHECK(set_file_permissions("locked","alice=rw","wrong"));
    CHECK(!set_file_permissions("locked","alice=rw","unlock"));
    CHECK(!fs_write("locked","ok",2));
    CHECK(!fs_mv("locked","renamed"));
    CHECK(set_file_permissions("renamed","alice=r","wrong"));
    CHECK(!set_file_permissions("renamed","alice=rw","unlock"));
    CHECK(!fs_mv("renamed","locked"));
    CHECK(set_file_permissions("/etc/userpasswd.cfg","alice=rw","rootpw"));
    CHECK(fs_cp("mine","/etc/copied")==-13);CHECK(fs_mv("mine","/etc/moved")==-13);
    CHECK(fs_mkdir("/etc/new")==-13);CHECK(fs_rm("/etc/userpasswd.cfg")==-13);
    CHECK(fs_mount("disk2","/home/alice")==-13);CHECK(fs_format("disk2")==-13);
    CHECK(switch_user("root","wrong"));CHECK(!strcmp(get_current_user(),"alice"));
    CHECK(!switch_user("bob","bobpw"));CHECK(permissions_uid()==1001);CHECK(fs_size("/home/alice/mine")==-13);
    CHECK(!switch_user("root","rootpw"));CHECK(!fs_write("/etc/root-write","yes",3));
    CHECK(!fs_mkdir("/home/alice/private"));CHECK(!fs_write("/home/alice/private/file","x",1));
    CHECK(!set_file_permissions("/home/alice/private/file","alice=r","lock"));
    CHECK(!switch_user("alice",""));CHECK(fs_mv("private","bypass"));
    CHECK(!switch_user("root","rootpw"));
    CHECK(!set_file_permissions("/bin","alice=",""));
    CHECK(!set_file_permissions("/bin/test","alice=",""));
    CHECK(!switch_user("alice",""));CHECK(fs_load_executable("/bin/test",data,sizeof(data))==-13);
    CHECK(!switch_user("root","rootpw"));CHECK(!fs_init());CHECK(!switch_user("alice",""));
    CHECK(fs_load_executable("/bin/test",data,sizeof(data))==-13);CHECK(!fs_write("locked","persisted",9));
    CHECK(!switch_user("root","rootpw"));
    /* Partition planner rejects overlap, overflow, GPT, full tables. */
    PartitionTable t;memset(&t,0,sizeof(t));t.sectors=65536;
    CHECK(!partition_add(&t,0,2048,8192,0x83));CHECK(partition_add(&t,1,4096,8192,0x83));
    CHECK(partition_add(&t,1,64512,4096,0x83));CHECK(partition_auto(&t,4096,0x83)==1);
    CHECK(t.entries[1].start==10240);CHECK(partition_auto(&t,0,0x83)==2);CHECK(partition_auto(&t,2048,0x83)<0);
    t.entries[0].type=0xee;CHECK(partition_validate(&t));
    puts("Permissions, password hashes, execute-only loading, isolation and partition planner passed");return 0;
}
