# SQLite Clone (in C)

A simplified relational database engine built from scratch in C, inspired by the
[cstack "Let's Build a Simple Database"](https://cstack.github.io/db_tutorial/) tutorial.
The goal of this project is to understand how a real database like SQLite works internally â€”
from the REPL and query parsing down to on-disk storage and B-Tree indexing.

## Overview

This project implements a lightweight database engine that supports a subset of SQL
(`INSERT` and `SELECT`) through an interactive command-line shell, with data persisted
to disk using fixed-size pages.

```
db > insert 1 alice alice@example.com
Executed.

db > select
(1, alice, alice@example.com)
Executed.

db > .exit
```

## Features Implemented So Far

- **REPL (Stage 1)** â€” Interactive shell that reads user input and distinguishes
  meta-commands (like `.exit`) from SQL statements.
- **In-Memory Storage (Stage 2)** â€” A `Row` structure to hold table data, with basic
  `insert` and `select` operations working entirely in memory.
- **Serialization (Stage 3)** â€” Converts `Row` structs into a compact binary format
  (and back) so data can be stored efficiently.
- **Persistence (Stage 4)** â€” Fixed-size pages and file-backed storage, so data
  survives after the program exits instead of living only in memory.
- **Cursor & Multi-Page Storage (Stage 5)** â€” Introduced `Table` and `Cursor`
  abstractions, lazy page allocation, and sequential traversal â€” so higher-level code
  doesn't need to manually calculate memory addresses to read/write rows.
- **B-Tree Foundation (Stage 6 â€” in progress)** â€” Currently extending the storage
  layer to use a B-Tree instead of simple sequential pages, to support efficient
  lookups and ordered storage as a real database would.

## Roadmap

This project follows the 13-stage structure of the original tutorial. Planned next steps:
- Complete B-Tree implementation (internal/leaf nodes, splitting)
- Support `WHERE` clauses and more query types (`UPDATE`, `DELETE`)
- Indexing and improved query execution

## Tech Stack

- **Language:** C
- **Concepts:** Manual memory management, binary serialization, file I/O, paging,
  B-Trees, cursor-based traversal
- **Tools:** Git & GitHub (staged, incremental commits per development step)

## How to Build & Run

```bash
gcc -o db db.c
./db mydb.db
```

## Learning Notes

Each stage was built by working through the logic step by step, using AI-assisted
tools to break down and understand low-level C concepts (pointer arithmetic, memory
layout, binary serialization) at each stage â€” with the goal of genuinely understanding
*why* each piece works, not just copying it.

## Acknowledgements

Based on the excellent [cstack SQLite clone tutorial](https://cstack.github.io/db_tutorial/).
