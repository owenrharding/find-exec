# find-exec
`uqfindexec` is a command-line tool developed to process files in a specified directory. It executes a given command or a pipeline of commands on each file found in that directory, similar to the -exec option of the Linux find command.

This program allows the user to specify commands that can include placeholders ({}) for file names, supports options for parallel execution, and can handle hidden files. It was designed for process management and command execution tasks in a Linux environment.

Features
-
- **File Processing**: Runs specified commands or pipelines on files in a directory.
- **Placeholder Substitution**: Replaces {} in the command with the file name being processed.
- **Directory Scanning**: Can work with specified directories or default to the current directory.
- **Optional Hidden File Inclusion**: Can process hidden files (`--allfiles` option).
- **Parallel Processing**: Supports parallel execution of commands for faster processing (`--parallel` option).
- **Statistics**: Outputs statistics about the processing of files (`--stats` option).
- **Error Handling**: Handles invalid directories, invalid commands, and missing files gracefully.

Usage
-
The program is run with the following options:
```C
./uqfindexec [--directory dirname] [--stats] [--parallel] [--recurse] [--allfiles] [cmd]
```
- `--directory dirname`: Specifies the directory to process (defaults to current directory).
- `--stats`: Outputs statistics after processing.
- `--parallel`: Runs the commands in parallel for faster execution.
- `--recurse`: Recursively processes files in subdirectories (CSSE7231 only).
- `--allfiles`: Includes hidden files in the processing.
- `cmd`: Command or pipeline of commands to run on each file (can include {} as a placeholder for the file name).

Example Commands
-
1. Run the command wc -l on every file in the /etc directory:
```C
./uqfindexec --directory /etc "wc -l {}"
```
2. Convert lowercase letters to uppercase in each file, saving the result in a new file:
```C
./uqfindexec --allfiles "tr a-z A-Z < {} > {}.out"
```
3. Output file modification dates for all files in /etc modified in 2023:
```C
./uqfindexec --directory /etc 'stat {} | grep "Change: 2023" | cut -d " " -f 2'
```
