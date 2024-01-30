#include "../slogger/replicated_log.h"
#include "../slib/log.h"

using namespace std;
using namespace replicated_log;

int test_0() {
    ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp");
    unsigned int memory_size = 128;
    unsigned int entry_size = 8;
    Replicated_Log* rl = new Replicated_Log(memory_size, entry_size);
    char buf[1024];

    int entries_per_epoch = memory_size / entry_size;

    int base = 0;
    int total = entries_per_epoch*3 + 3;
    int i = 0;

    for (int i=base;i<base+total;i++) {
        ALERT("TEST 0", "appending %d", i);
        rl->Append_Log_Entry(&i,sizeof(i));
    }
    rl->Chase_Locally_Synced_Tail_Pointer();
    rl->Print_All_Entries();
}

// int test_1() {
//     ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp test_1 pointer chaising");
//     unsigned int memory_size = 100;
//     Replicated_Log* rl = new Replicated_Log(memory_size);
//     char buf[1024];

//     Log_Entry bs_0;
//     bs_0.size = 10;
//     bs_0.type = 1;
//     rl->Append_Log_Entry(bs_0,buf);
//     rl->Print_All_Entries();

//     Log_Entry bs_1;
//     bs_1.size = 20;
//     bs_1.type = 2;
//     rl->Append_Log_Entry(bs_1,buf);
//     rl->Print_All_Entries();

//     rl->Reset_Tail_Pointer();
//     ALERT("TEST 1", "Should print nothing right now");
//     rl->Print_All_Entries();
//     ALERT("TEST 1", "Chaising, should now print all the entries");
//     rl->Chase_Tail_Pointer();
//     rl->Print_All_Entries();



// }

int main() {
    test_0();
    // test_1();
}