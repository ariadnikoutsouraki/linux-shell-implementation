# Custom Unix Shell Implementation (C)

This repository contains the progressive development of a custom Unix/Linux shell, written entirely in C. The project was developed in three distinct phases, adding more complex Operating System functionalities at each step.

## Project Structure

### 📁 [Part 1: Basic Shell & Execution](./Part1_Basic_Shell)
The foundational stage of the shell. It features:
* A custom command prompt (`tiny_shell>`).
* Execution of basic external commands using `fork()`, `execvp()`, and `waitpid()`.
* Built-in commands like `cd` and `exit`.

### 📁 [Part 2: Pipelines & I/O Redirection](./Part2_Pipes_Redirection)
Advanced input/output management. It introduces:
* **I/O Redirection:** Support for `<`, `>`, `>>`, `2>`, and `2>&1`.
* **Multi-level Pipelines:** Connecting multiple commands using `|` (e.g., `ls -l | grep .c | wc -l`), allowing data to flow seamlessly between child processes.

### 📁 [Part 3: Job Control & Signal Handling](./Part3_Job_Control)
The final and most complex phase, mimicking real process management:
* **Background Execution:** Running processes in the background using `&`.
* **Job Control Commands:** Implementation of `jobs`, `fg`, and `bg` to manage running or stopped processes.
* **Signal Handling:** Safely catching and processing signals like `SIGINT` (Ctrl+C) and `SIGTSTP` (Ctrl+Z) without crashing the main shell.

## Compilation & Usage
Each directory contains its own source code and a `Makefile`. To test a specific version, navigate to the respective folder, run `make`, and execute the generated binary. Detailed instructions are provided in the `README.md` of each part.
