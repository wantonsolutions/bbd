#include "../../src/slogger/local_stub_logger.h"
// #include <local_stub_logger.h>
#include "stdio.h"
using namespace slogger;

//We will serialize the operations into this number
static int number;

//Enum for the type of operation we will put on the log
enum ops {add_op,subtract_op};

//Description of the operations
static const char *ops_str[] = {"add","subtract"};

//Tightly packed data structure for the operations
typedef struct __attribute__ ((packed)) op_entry {
    enum ops type;
    int number;
    string toString() {
        printf("op_entry...\n");
        return "op_entry: type: " + string(ops_str[type]) + " number: " + to_string(number); 
    }
} op_entry;

//Functions to add and subtract numbers
void add_number(int n){
    number += n;
}

void subtract_number(int n){
    number -= n;
}

void Apply_Ops(Local_Stub_Logger &log) {

    //There should always be one on the log, fail if not
    assert(log.Peek_Next_Operation() != NULL);
    //Get the next operation
    op_entry *op = (op_entry*) log.Next_Operation();
    //Loop through all of the operations
    //Stop when there is only one element left
    while (log.Peek_Next_Operation() != NULL) {
        printf("Applying operation: %s\n", op->toString().c_str());
        if (op->type == add_op) {
            printf("Adding %d\n", op->number);
            add_number(op->number);
        } else if (op->type == subtract_op) {
            printf("Subtracting %d\n", op->number);
            subtract_number(op->number);
        }
        op = (op_entry*) log.Next_Operation();
    }
}

void Execute(Local_Stub_Logger &log, op_entry op) {
    log.Write_Operation(&op, sizeof(op));
    log.Sync_To_Last_Write();
    Apply_Ops(log);
}

void log_add(Local_Stub_Logger &log, int n) {
    //Create a serialized request object
    op_entry op;
    op.type = add_op;
    op.number = n;
    //execute the operation on the log
    Execute(log, op);
    //Apply the local operations
    add_number(n);
}

void log_subtract(Local_Stub_Logger &log, int n) {
    //Create a serialized request object
    op_entry op;
    op.type = subtract_op;
    op.number = n;
    //execute the operation on the log
    Execute(log, op);
    //Apply the local operations
    subtract_number(n);
}

int main() {
    Local_Stub_Logger log;
    number = 0;
    log_add(log, 5);
    log_add(log, 10);
    log_subtract(log, 3);
    log_add(log, 2);
    printf("Final number: %d\n", number);

    return 0;
}