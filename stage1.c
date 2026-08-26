#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct{
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
}InputBuffer;

typedef enum{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNISED_COMMAND
}MetaCommandResult;

InputBuffer* new_input_buffer(){
    InputBuffer* input_buffer=(InputBuffer*)malloc(sizeof(InputBuffer));
    input_buffer->buffer=NULL;
    input_buffer->buffer_length=0;
    input_buffer->input_length=0;
    return input_buffer;
}

void print_prompt(){
    printf("db >");
    fflush(stdout);
}

void read_input(InputBuffer* input_buffer){
    ssize_t bytes_read=getline(&(input_buffer->buffer),&(input_buffer->buffer_length),stdin);

    if(bytes_read <=0){
        printf("Error reading input \n");
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length=bytes_read-1;
    input_buffer->buffer[bytes_read-1]='\0';
}

void close_input_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

 MetaCommandResult do_meta_command(InputBuffer* input_buffer ){
    if(strcmp(input_buffer->buffer,".exit")==0){
        close_input_buffer(input_buffer);
        exit(EXIT_SUCCESS);
        }else{
            return META_COMMAND_UNRECOGNISED_COMMAND;
        }
}

int main(int argc, char* argv[]){
    InputBuffer* input_buffer=new_input_buffer();

    while(true){
        print_prompt();
        read_input(input_buffer);
        if(input_buffer->buffer[0] =='.'){
            switch(do_meta_command(input_buffer)){
                case(META_COMMAND_SUCCESS):
                   break;
                case(META_COMMAND_UNRECOGNISED_COMMAND):
                    printf("Unrecognised command '%s'\n",input_buffer->buffer);
                    continue;
            }      
          }else{
            printf("Unrecognised keyword at start of '%s'.\n",input_buffer->buffer);
          }
    }
    return 0;
}
