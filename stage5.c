#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
} Row;

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

#define ID_SIZE size_of_attribute(Row, id)
#define USERNAME_SIZE size_of_attribute(Row, username)
#define EMAIL_SIZE size_of_attribute(Row, email)

#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100
#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES];
} Table;

// --- STAGE 5: CURSOR ABSTRACTION ---
typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table; // Indicates a position one past the last element
} Cursor;

// Function Declarations
InputBuffer* new_input_buffer();
void print_prompt();
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement);
ExecuteResult execute_statement(Statement* statement, Table* table);
void serialize_row(const Row* source, void* destination);
void deserialize_row(const void* source, Row* destination);
void* row_slot(Table* table, uint32_t row_num);
Table* db_open(const char* filename);
void db_close(Table* table);
Cursor* table_start(Table* table);
Cursor* table_end(Table* table);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);

InputBuffer* new_input_buffer() {
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;
    return input_buffer;
}

void print_prompt() { printf("db > "); }

void read_input(InputBuffer* input_buffer) {
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }
    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

void serialize_row(const Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(const void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void* row_slot(Table* table, uint32_t row_num) {
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void* page = table->pages[page_num];
    if (page == NULL) {
        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
}

Table* db_open(const char* filename) {
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
    if (fd == -1) {
        printf("Error: Could not open file.\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);
    Table* table = malloc(sizeof(Table));
    table->file_descriptor = fd;
    table->file_length = file_length;
    table->num_rows = file_length / ROW_SIZE;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        table->pages[i] = NULL;
    }

    // Only load pages that contain actual existing rows from disk
    if (table->num_rows > 0) {
        uint32_t num_pages = (table->num_rows + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
        for (uint32_t i = 0; i < num_pages; i++) {
            void* page = malloc(PAGE_SIZE);
            lseek(fd, i * PAGE_SIZE, SEEK_SET);
            read(fd, page, PAGE_SIZE);
            table->pages[i] = page;
        }
    }

    return table;
}

void db_close(Table* table) {
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;
    
    // Flush fully filled pages
    for (uint32_t i = 0; i < num_full_pages; i++) {
        if (table->pages[i] != NULL) {
            lseek(table->file_descriptor, i * PAGE_SIZE, SEEK_SET);
            write(table->file_descriptor, table->pages[i], PAGE_SIZE);
            free(table->pages[i]);
        }
    }

    // Flush any remaining partial page
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {
        uint32_t page_num = num_full_pages;
        if (table->pages[page_num] != NULL) {
            lseek(table->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            write(table->file_descriptor, table->pages[page_num], num_additional_rows * ROW_SIZE);
            free(table->pages[page_num]);
        }
    }

    close(table->file_descriptor);
    free(table);
}
// --- STAGE 5: CURSOR IMPLEMENTATION METHODS ---

// Returns a cursor pointing to the beginning of the table
Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = 0;
    cursor->cell_num = 0;
    cursor->end_of_table = (table->num_rows == 0);
    return cursor;
}

// Returns a cursor pointing to one past the last element
Cursor* table_end(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->num_rows / ROWS_PER_PAGE;
    cursor->cell_num = table->num_rows % ROWS_PER_PAGE;
    cursor->end_of_table = true;
    return cursor;
}

// Dereference the cursor to get a memory pointer to the row
void* cursor_value(Cursor* cursor) {
    uint32_t row_num = cursor->page_num * ROWS_PER_PAGE + cursor->cell_num;
    return row_slot(cursor->table, row_num);
}

// Advance the cursor to the next row
void cursor_advance(Cursor* cursor) {
    uint32_t row_num=cursor->page_num * ROWS_PER_PAGE + cursor->cell_num;
    row_num++;
    if (row_num >= cursor->table->num_rows) {
       cursor->end_of_table=true;
    }else{
        cursor->page_num=row_num/ROWS_PER_PAGE;
        cursor->cell_num=row_num%ROWS_PER_PAGE;
    }
    
    
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    } else {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;
        int argsassigned = sscanf(
            input_buffer->buffer, "insert %d %s %s",
            &(statement->row_to_insert.id),
            statement->row_to_insert.username,
            statement->row_to_insert.email
        );
        if (argsassigned < 3) {
            return PREPARE_SYNTAX_ERROR;
        }
        if (statement->row_to_insert.id < 0) {
            return PREPARE_NEGATIVE_ID;
        }
        if (strlen(statement->row_to_insert.username) > COLUMN_USERNAME_SIZE ||
            strlen(statement->row_to_insert.email) > COLUMN_EMAIL_SIZE) {
            return PREPARE_STRING_TOO_LONG;
        }
        return PREPARE_SUCCESS;
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// --- UPDATED EXECUTION USING CURSORS ---

ExecuteResult execute_insert(Statement* statement, Table* table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }

    Row* row_to_insert = &(statement->row_to_insert);
    Cursor* cursor = table_end(table); // Get cursor pointing to the end

    serialize_row(row_to_insert, cursor_value(cursor));
    table->num_rows++;

    free(cursor);
    return EXECUTE_SUCCESS;
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table); // Get cursor pointing to the start
    Row row;
    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor); // Move cursor forward
    }
    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
    switch (statement->type) {
        case STATEMENT_INSERT:
            return execute_insert(statement, table);
        case STATEMENT_SELECT:
            return execute_select(statement, table);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);

    InputBuffer* input_buffer = new_input_buffer();
    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case META_COMMAND_SUCCESS:
                    continue;
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecognized command '%s'\n", input_buffer->buffer);
                    continue;
            }
        }

        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_NEGATIVE_ID:
                printf("ID must be positive.\n");
                continue;
            case PREPARE_STRING_TOO_LONG:
                printf("String is too long.\n");
                continue;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement.\n");
                continue;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table full.\n");
                break;
        }
    }
}