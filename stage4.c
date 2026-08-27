#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
} Row;

/* ---------- Row layout ---------- */

#define ROW_SIZE sizeof(Row)

#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

/* ---------- Pager ---------- */

typedef struct {
    int file_descriptor;
    uint32_t file_length;

    
    void* pages[TABLE_MAX_PAGES];

} Pager;

/* ---------- Table ---------- */

typedef struct {
    Pager* pager;
    uint32_t num_rows;
} Table;



void serialize_row(Row* source, void* destination) {
    memcpy(destination, source, ROW_SIZE);
}

void deserialize_row(void* source, Row* destination) {
    memcpy(destination, source, ROW_SIZE);
}



Pager* pager_open(const char* filename) {
    int fd = open(
        filename,
        O_RDWR | O_CREAT,
        0666
    );

    if (fd == -1) {
        printf("Unable to open file.\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    if (file_length == -1) {
        printf("Error determining file size.\n");
        exit(EXIT_FAILURE);
    }

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = (uint32_t)file_length;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    return pager;
}


void* get_page(Pager* pager, uint32_t page_num) {

    if (page_num >= TABLE_MAX_PAGES) {
        printf("Page number out of bounds: %d\n", page_num);
        exit(EXIT_FAILURE);
    }

    
    if (pager->pages[page_num] == NULL) {

        
        void* page = malloc(PAGE_SIZE);

        
        uint32_t num_pages =
            pager->file_length / PAGE_SIZE;

       
        if (pager->file_length % PAGE_SIZE != 0) {
            num_pages += 1;
        }

       
        if (page_num < num_pages) {

            lseek(
                pager->file_descriptor,
                page_num * PAGE_SIZE,
                SEEK_SET
            );

            ssize_t bytes_read =
                read(
                    pager->file_descriptor,
                    page,
                    PAGE_SIZE
                );

            if (bytes_read == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;
    }

    return pager->pages[page_num];
}



void* row_slot(Table* table, uint32_t row_num) {

  
    uint32_t page_num =
        row_num / ROWS_PER_PAGE;

    
    void* page =
        get_page(table->pager, page_num);

    
    uint32_t row_offset =
        row_num % ROWS_PER_PAGE;

    
    uint32_t byte_offset =
        row_offset * ROW_SIZE;

   
    return page + byte_offset;
}



void pager_flush(
    Pager* pager,
    uint32_t page_num,
    uint32_t size
) {

    if (pager->pages[page_num] == NULL) {
        printf("Cannot flush a NULL page.\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Move to the beginning of this page
     * inside the database file.
     */
    off_t offset =
        lseek(
            pager->file_descriptor,
            page_num * PAGE_SIZE,
            SEEK_SET
        );

    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    /*
     * Write the page from RAM to disk.
     */
    ssize_t bytes_written =
        write(
            pager->file_descriptor,
            pager->pages[page_num],
            size
        );

    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}


Table* db_open(const char* filename) {

    Pager* pager =
        pager_open(filename);

    Table* table =
        malloc(sizeof(Table));

    table->pager = pager;

    /*
     * Calculate how many rows already exist
     * based on the file size.
     */
    table->num_rows =
        pager->file_length / ROW_SIZE;

    return table;
}

/* =========================================================
   DATABASE CLOSE
   ========================================================= */

void db_close(Table* table) {

    Pager* pager =
        table->pager;

   
    uint32_t num_full_pages =
        table->num_rows / ROWS_PER_PAGE;

   
    for (uint32_t i = 0;
         i < num_full_pages;
         i++) {

        if (pager->pages[i] == NULL) {
            continue;
        }

        pager_flush(
            pager,
            i,
            PAGE_SIZE
        );

        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    /*
     * Check whether there is a partially
     * filled final page.
     */
    uint32_t num_additional_rows =
        table->num_rows % ROWS_PER_PAGE;

    if (num_additional_rows > 0) {

        uint32_t page_num =
            num_full_pages;

        if (pager->pages[page_num] != NULL) {

            /*
             * Only write the actual rows.
             *
             * Don't write the unused part
             * of the page.
             */
            pager_flush(
                pager,
                page_num,
                num_additional_rows * ROW_SIZE
            );

            free(pager->pages[page_num]);
            pager->pages[page_num] = NULL;
        }
    }

    /*
     * Close the database file.
     */
    int result =
        close(pager->file_descriptor);

    if (result == -1) {
        printf("Error closing database file.\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Free Pager and Table.
     */
    free(pager);
    free(table);
}

/* =========================================================
   INSERT
   ========================================================= */

void execute_insert(
    Table* table,
    Row* row
) {

    if (table->num_rows >= TABLE_MAX_ROWS) {
        printf("Error: Table full.\n");
        return;
    }

    /*
     * Find the RAM location where this row
     * should be stored.
     */
    void* destination =
        row_slot(
            table,
            table->num_rows
        );

    /*
     * Copy the row into that location.
     */
    serialize_row(
        row,
        destination
    );

    /*
     * Increase row count.
     */
    table->num_rows++;
}

/* =========================================================
   SELECT
   ========================================================= */

void execute_select(Table* table) {

    Row row;

    for (uint32_t i = 0;
         i < table->num_rows;
         i++) {

        /*
         * Find the row in RAM.
         * If necessary, get_page() loads
         * its page from disk.
         */
        void* source =
            row_slot(
                table,
                i
            );

        /*
         * Convert raw bytes back into Row.
         */
        deserialize_row(
            source,
            &row
        );

        printf(
            "(%d, %s, %s)\n",
            row.id,
            row.username,
            row.email
        );
    }
}

/* =========================================================
   MAIN / REPL
   ========================================================= */

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf("Must supply a database filename.\n");
        return 1;
    }

    /*
     * Open database file.
     */
    Table* table =
        db_open(argv[1]);

    char input[1024];

    while (1) {

        printf("db > ");

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        /*
         * Meta command: .exit
         */
        if (strncmp(input, ".exit", 5) == 0) {

            db_close(table);

            break;
        }

        /*
         * SQL command: select
         */
        if (strncmp(input, "select", 6) == 0) {

            execute_select(table);

            continue;
        }

        /*
         * SQL command: insert
         *
         * Expected:
         *
         * insert 1 user1 person@example.com
         */
        if (strncmp(input, "insert", 6) == 0) {

            Row row;

            char username[COLUMN_USERNAME_SIZE];
            char email[COLUMN_EMAIL_SIZE];

            int args_assigned =
                sscanf(
                    input,
                    "insert %d %31s %254s",
                    &row.id,
                    username,
                    email
                );

            if (args_assigned != 3) {
                printf("Syntax error.\n");
                continue;
            }

            strncpy(
                row.username,
                username,
                COLUMN_USERNAME_SIZE
            );

            strncpy(
                row.email,
                email,
                COLUMN_EMAIL_SIZE
            );

            execute_insert(
                table,
                &row
            );

            printf("Executed.\n");

            continue;
        }

        /*
         * Unknown command.
         */
        printf("Unrecognized command: %s", input);
    }

    return 0;
}