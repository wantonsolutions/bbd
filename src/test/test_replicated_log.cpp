#include "../slogger/replicated_log.h"
#include "../slib/log.h"

using namespace std;
using namespace replicated_log;
int main() {
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