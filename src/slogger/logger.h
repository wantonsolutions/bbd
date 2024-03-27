#ifndef LOGGER_H
#define LOGGER_H

#define MAX_BATCH_SIZE 256

class logger_interface {
    public:
        bool Write_Operation(void* op, int size);
        bool Write_Operations(void ** ops, unsigned int * sizes, unsigned int num_ops);
        void * Next_Operation();
        void * Peek_Next_Operation();
        void Sync_To_Last_Write();
};

#endif