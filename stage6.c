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

// B-Tree Node Header Layout Constants using #define
#define NODE_TYPE_SIZE sizeof(uint8_t)
#define NODE_TYPE_OFFSET 0
#define IS_ROOT_SIZE sizeof(uint8_t)
#define IS_ROOT_OFFSET NODE_TYPE_SIZE
#define PARENT_POINTER_SIZE sizeof(uint32_t)
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

// Leaf Node Header Layout Constants
#define LEAF_NODE_NUM_CELLS_SIZE sizeof(uint32_t)
#define LEAF_NODE_NUM_CELLS_OFFSET COMMON_NODE_HEADER_SIZE
#define LEAF_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE)

// Leaf Node Body Layout Constants
#define LEAF_NODE_KEY_SIZE sizeof(uint32_t)
#define LEAF_NODE_KEY_OFFSET 0
#define LEAF_NODE_VALUE_SIZE ROW_SIZE
#define LEAF_NODE_VALUE_OFFSET (LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE)
#define LEAF_NODE_CELL_SIZE (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)

typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES];
} Table;

typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

// Leaf Node Accessor Helpers
uint32_t* leaf_node_num_cells(void* node) {
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void initialize_leaf_node(void* node) {
    *leaf_node_num_cells(node) = 0;
}

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
void* get_page(Table* table, uint32_t page_num);
Table* db_open(const char* filename);
void db_close(Table* table);
Cursor* table_start(Table* table);
Cursor* table_end(Table* table);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
Cursor* table_find(Table* table, uint32_t key);

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

void* get_page(Table* table, uint32_t page_num) {
    if (page_num > TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds. %d\n", page_num);
        exit(EXIT_FAILURE);
    }

    if (table->pages[page_num] == NULL) {
        void* page = malloc(PAGE_SIZE);
        uint32_t num_pages = table->file_length / PAGE_SIZE;

        if (page_num <= num_pages) {
            lseek(table->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            read(table->file_descriptor, page, PAGE_SIZE);
        }
        table->pages[page_num] = page;
    }

    return table->pages[page_num];
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

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        table->pages[i] = NULL;
    }

    // If file is brand new or empty, initialize root node cells to 0
    if (file_length == 0) {
        table->num_rows = 0;
        void* root_node = get_page(table, 0);
        initialize_leaf_node(root_node);
    } else {
        void* root_node = get_page(table, 0);
        // Safety check: ensure read cell count doesn't exceed maximum capacity
        uint32_t num_cells = *leaf_node_num_cells(root_node);
        if (num_cells > LEAF_NODE_MAX_CELLS) {
            initialize_leaf_node(root_node);
            table->num_rows = 0;
        } else {
            table->num_rows = num_cells;
        }
    }

    return table;
}

void db_close(Table* table) {
    if (table->pages[0] != NULL) {
        lseek(table->file_descriptor, 0, SEEK_SET);
        write(table->file_descriptor, table->pages[0], PAGE_SIZE);
        free(table->pages[0]);
        table->pages[0] = NULL;
    }

    close(table->file_descriptor);
    free(table);
}

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = 0;
    cursor->cell_num = 0;
    
    void* root_node = get_page(table, 0);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);
    return cursor;
}

Cursor* table_end(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = 0;

    void* root_node = get_page(table, 0);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->cell_num = num_cells;
    cursor->end_of_table = true;
    return cursor;
}

void* cursor_value(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* page = get_page(cursor->table, page_num);
    return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* node = get_page(cursor->table, page_num);

    cursor->cell_num++;
    if (cursor->cell_num >= *leaf_node_num_cells(node)) {
        cursor->end_of_table = true;
    }
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        printf("Need to implement splitting leaf node.\n");
        exit(EXIT_FAILURE);
    }

    if (cursor->cell_num < num_cells) {
        for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }

    *leaf_node_num_cells(node) += 1;
    *leaf_node_key(node, cursor->cell_num) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = get_page(table, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;

    while (one_past_max_index > min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);
        if (key == key_at_index) {
            cursor->cell_num = index;
            cursor->end_of_table = false;
            return cursor;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor->cell_num = min_index;
    cursor->end_of_table = false;
    return cursor;
}

Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_pageNum = 0;
    return leaf_node_find(table, root_pageNum, key);
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

ExecuteResult execute_insert(Statement* statement, Table* table) {
    void* node = get_page(table, 0);
    uint32_t num_cells = *leaf_node_num_cells(node);

    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TABLE_FULL;
    }

    Row* row_to_insert = &(statement->row_to_insert);
    Cursor* cursor = table_find(table, row_to_insert->id);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
        if (key_at_index == row_to_insert->id) {
            free(cursor);
            return EXECUTE_TABLE_FULL;
        }
    }

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
    table->num_rows++;

    free(cursor);
    return EXECUTE_SUCCESS;
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);
    Row row;
    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
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
                printf("Error: Table full or duplicate key.\n");
                break;
        }
    }
}