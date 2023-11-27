#include "../slogger/replicated_log.h"
#include "../slib/log.h"

using namespace std;
using namespace replicated_log;

int test_0() {
    ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp");
    unsigned int memory_size = 100;
    Replicated_Log* rl = new Replicated_Log(100);
    Log_Entry bs;
    char buf[1024];

    bs.entry_size = 10;
    bs.entry_type = 1;

    rl->Append_Log_Entry(bs,buf);
    rl->Print_All_Entries();
    bs.entry_size = 20;
    bs.entry_type = 2;
    rl->Append_Log_Entry(bs,buf);
    rl->Print_All_Entries();
    bs.entry_size = 30;
    bs.entry_type = 3;
    rl->Append_Log_Entry(bs,buf);
    rl->Print_All_Entries();
    bs.entry_size=1;
    bs.entry_type=4;
    rl->Append_Log_Entry(bs,buf);
    rl->Print_All_Entries();
}

int test_1() {
    ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp test_1 pointer chaising");
    unsigned int memory_size = 100;
    Replicated_Log* rl = new Replicated_Log(memory_size);
    char buf[1024];

    Log_Entry bs_0;
    bs_0.entry_size = 10;
    bs_0.entry_type = 1;
    rl->Append_Log_Entry(bs_0,buf);
    rl->Print_All_Entries();

    Log_Entry bs_1;
    bs_1.entry_size = 20;
    bs_1.entry_type = 2;
    rl->Append_Log_Entry(bs_1,buf);
    rl->Print_All_Entries();

    rl->Reset_Tail_Pointer();
    ALERT("TEST 1", "Should print nothing right now");
    rl->Print_All_Entries();
    ALERT("TEST 1", "Chaising, should now print all the entries");
    rl->Chase_Tail_Pointer();
    rl->Print_All_Entries();



}

int main() {
    test_0();
    test_1();
}