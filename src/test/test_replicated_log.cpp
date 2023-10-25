#include "../slogger/replicated_log.h"
#include "../slib/log.h"

using namespace std;
using namespace replicated_log;

int test_0() {
    ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp");
    unsigned int memory_size = 100;
    Replicated_Log* rl = new Replicated_Log(100);
    Basic_Entry bs;
    bs.entry_size = 10;
    bs.entry_type = 1;
    bs.repeating_value = '1';
    rl->Append_Basic_Entry(bs);
    rl->Print_All_Entries();
    bs.entry_size = 20;
    bs.entry_type = 2;
    bs.repeating_value = '2';
    rl->Append_Basic_Entry(bs);
    rl->Print_All_Entries();
    bs.entry_size = 30;
    bs.entry_type = 3;
    bs.repeating_value = '3';
    rl->Append_Basic_Entry(bs);
    rl->Print_All_Entries();
    bs.entry_size=1;
    bs.entry_type=4;
    bs.repeating_value='4';
    rl->Append_Basic_Entry(bs);
    rl->Print_All_Entries();
}

int test_1() {
    ALERT("SLOGGER_TEST", "starting test_replicated_log.cpp test_1 pointer chaising");
    unsigned int memory_size = 100;
    Replicated_Log* rl = new Replicated_Log(memory_size);

    Basic_Entry bs_0;
    bs_0.entry_size = 10;
    bs_0.entry_type = 1;
    bs_0.repeating_value = '1';
    rl->Append_Basic_Entry(bs_0);
    rl->Print_All_Entries();

    Basic_Entry bs_1;
    bs_1.entry_size = 20;
    bs_1.entry_type = 2;
    bs_1.repeating_value = '2';
    rl->Append_Basic_Entry(bs_1);
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