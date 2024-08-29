// Owen Harding 48007618 CSSE2310 A3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <csse2310a3.h>
#include <errno.h>

// Structure to hold program parameters - determined from command line or
// default values.
typedef struct {
    char* dirname;
    bool dirnameGiven;
    bool recurse;
    bool parallel;
    bool allfiles;
    bool stats;
    char* cmd;
    CommandPipeline* pipeline;
} CommandLineOptions;

// Structure to hold a scanned directory, including information about the files
// in said directory. Array is dynamically allocated.
typedef struct {
    char* dirname;
    struct dirent** namelist;
    int numDirent;
} Directory;

// Structure to hold a set of files in a directory (as a dynamically allocated
// array).
typedef struct {
    char** files;
    int numFiles;
} DirArray;

// Structure to hold a placeholder-subsituted copy of a CommandPipeline struct.
typedef struct {
    char* fileExecName;
    char* stdinFileName;
    char* stdoutFileName;
    int numCmds;
    char*** cmdArray;
} SubbedPipeline;

// Structure to hold data on a child process created to execute a set command
// on a set file.
typedef struct {
    char* file;
    char* cmd;
    pid_t pid;
} ChildWorker;

// Structure to hold an array of child processes created to execute a command
// pipeline.
typedef struct {
    ChildWorker* children;
    int numPids;
} PidArray;

// Structure to hold statistic data on the execution status of command pipelines
// on files.
typedef struct {
    int n1; /*Number files processed.*/
    int n2; /*Number files where every command exits with status of 0.*/
    int n3; /*Number files where all but one (or more) execute with status of
               0.*/
    int n4; /*Number files where some command exits due to being signalled.*/
    int n5; /*Number files where pipeline not executed.*/
} Statistics;

// Structure to hold a series of sets of childworkers created to execute a
// command pipeline. Each set of processes should correspond to the execution
// of a command pipeline on a single file.
typedef struct {
    PidArray* pidArrays;
    int numLoops;
} PidLog;

// Exit Status Values
typedef enum {
    USAGE_ERROR = 19,
    INVALID_DIR = 10,
    INVALID_CMD = 2,
    SUCCESS = 0,
    PROCESSING_FAILED = 4,
    SIG_INTERUPT = 5
} ExitStatus;

// Command Pipeline Execution Values
typedef enum {
    EXIT_ALL_ZERO = 71,
    EXIT_NON_ZERO = 72,
    EXIT_SIGNALLED = 73,
    CMD_NOT_EXECUTED = 99
} CommandExitStatus;

// Child Process Exit Status Values
typedef enum {
    FAILED_TO_EXECUTE = 4,
    READ_ERROR = 5,
    WRITE_ERROR = 6,
    UNEXPECTED_EXIT = 99
} ChildExitInfo;

// Command Line Option Arguments
const char* const directorySpecifier = "--directory";
const char* const recurseSpecifier = "--recurse";
const char* const parallelSpecifier = "--parallel";
const char* const allfilesSpecifier = "--allfiles";
const char* const statsSpecifier = "--stats";

// Error Messages
const char* const usageErrorMessage
        = "Usage: uqfindexec [--directory dirname] [--stats] [--parallel] "
          "[--recurse] [--allfiles] [cmd]\n";
const char* const invalidDirMessage
        = "uqfindexec: cannot access directory \"%s\"\n";
const char* const invalidCmdMessage = "uqfindexec: invalid command\n";
const char* const invalidReadMessage
        = "uqfindexec: cannot read \"%s\" when processing \"%s\"\n";
const char* const invalidWriteMessage
        = "uqfindexec: unable to write \"%s\" while processing \"%s\"\n";
const char* const invalidExecutionMessage
        = "uqfindexec: cannot execute \"%s\" when processing \"%s\"\n";

// Statistics Messages
const char* const n1Message = "Attempted to operate on a total of %i files\n";
const char* const n2Message = " - operations succeeded for %i files\n";
const char* const n3Message
        = " - non-zero exit status detected when processing %i files\n";
const char* const n4Message
        = " - processing terminated by signal for %i files\n";
const char* const n5Message
        = " - operations unable to be executed for %i files\n";

// Default Directory
const char* const defaultDir = ".";

////////////////////////////////////////////////////////////////////////////////

// Function Prototypes

// Command Line Parsing & Checking
CommandLineOptions parse_command_line(int argc, char** argv);
CommandLineOptions check_dir_and_cmd(CommandLineOptions options);

// Directory Scanning & Filtering
Directory get_directory(char* dirname);
DirArray generate_filtered_directory(
        Directory dir, bool allfiles, bool dirnameGiven);
char* get_true_file_name(char* dName, char* dirname, bool dirnameGiven);
DirArray add_file_to_dir_array(DirArray dir, char* filename);

// Placeholder Substitution
SubbedPipeline sub_placeholders(CommandPipeline* pipeline, char* file);
char** sub_placeholder_in_array(char** cmdArray, char* replacement);
char* sub_placeholder_in_string(char* original, char* replacement);

// Command Execution
PidArray execute_command_pipeline(SubbedPipeline pipeline);
void redirect_io_files(SubbedPipeline pipeline, int* in, int* out, bool* ioOk);
void populate_pipe_array(int pipeArray[][2], int numPipes);
void execute_child(
        SubbedPipeline pipeline, int j, int pipeArray[][2], int in, int out);
void close_pipe_fds(int pipeArray[][2], int numPipes);
PidArray add_child_to_array(PidArray pidArray, ChildWorker newChild);
int wait_on_child_array(PidArray pidArray);

// File Iteration
PidLog iterate_files(DirArray cleanDir, CommandLineOptions checkedOptions,
        Statistics* stats, bool* allFilesProcessed);

// Statistics Functionality
Statistics update_stats(Statistics stats, int waitInfo);
void print_statistics(Statistics stats);

// Parallel Functionality
PidLog add_pidarray_to_log(PidLog log, PidArray pArr);
void wait_on_pid_log(PidLog log, Statistics* stats);

// Memory Cleaning
void free_dir_array(DirArray dir);
void free_subbed_pipeline(SubbedPipeline subbedPipeline);
void free_childworker(ChildWorker cw);
void free_pidarray(PidArray pidArray);

// Signal Handling
void set_up_signal_handlers(void);
void notice(int s);

// Exit Status Logic
int compute_exit_status(
        Statistics stats, bool parallel, bool allFilesProcessed);

// Error Message Functions
void usage_error(void);
void invalid_dir_error(char* invalidDir);
void invalid_cmd_error(void);
void read_error(char* filename1, char* filename2);
void write_error(char* filename1, char* filename2);
void execution_error(char* cmd, char* filename);

////////////////////////////////////////////////////////////////////////////////

// Global Variables
bool sigInterupt = false;

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    // Signal Handling.
    set_up_signal_handlers();

    // Command Line Parsing & Checking.
    CommandLineOptions options = parse_command_line(argc, argv);
    CommandLineOptions checkedOptions = check_dir_and_cmd(options);

    // Directory Scanning & Filtering.
    Directory dir = get_directory(checkedOptions.dirname);
    DirArray cleanDir = generate_filtered_directory(
            dir, checkedOptions.allfiles, checkedOptions.dirnameGiven);

    // Setup for parallel and statistic modes.
    Statistics stats = {0, 0, 0, 0, 0};
    // PidLog log = {NULL, 0};
    bool allFilesProcessed = false;

    // Iterate over valid files in the directory and execute given command.
    PidLog log = iterate_files(
            cleanDir, checkedOptions, &stats, &allFilesProcessed);

    if (checkedOptions.parallel) {
        wait_on_pid_log(log, &stats);
    }

    // Statistics printing.
    if (checkedOptions.stats) {
        print_statistics(stats);
    }

    free_dir_array(cleanDir);
    free_pipeline(checkedOptions.pipeline);

    // Exit Status Logic.
    return compute_exit_status(
            stats, checkedOptions.parallel, allFilesProcessed);
}

////////////////////////////////////////////////////////////////////////////////

// Command Line Parsing & Checking

////////////////////////////////////////////////////////////////////////////////

// parse_command_line()
//
// REF: Structure of this function inspired HEAVILY by CSSE2310 2024 Sem 1 A1
// REF: Sample Solution written by Peter Sutton, lines 195-236.
// Parses command line and extracts any appropriate options and/or flags.
// Returns an initialised struct CommandLineOptions if command line is valid,
// else prints appropriate error message.
CommandLineOptions parse_command_line(int argc, char** argv)
{
    // Initialise default values.
    CommandLineOptions options
            = {NULL, false, false, false, false, false, NULL, NULL};

    // Skip program name.
    argc--;
    argv++;

    // Iterate over arguments while there is at least one argument remaining
    // beginning with "--".
    while ((argc >= 1) && (strncmp(argv[0], "--", 2) == 0)) {
        if (!strcmp(argv[0], directorySpecifier) && !options.dirname
                && argc >= 2 && strlen(argv[1]) > 0) {
            // dirname is still NULL and a non-empty arg follows.
            options.dirname = argv[1];
            options.dirnameGiven = true;
            argc -= 2;
            argv += 2;
        } else if (!strcmp(argv[0], recurseSpecifier) && !options.recurse) {
            options.recurse = true;
            argc--;
            argv++;
        } else if (!strcmp(argv[0], parallelSpecifier) && !options.parallel) {
            options.parallel = true;
            argc--;
            argv++;
        } else if (!strcmp(argv[0], allfilesSpecifier) && !options.allfiles) {
            options.allfiles = true;
            argc--;
            argv++;
        } else if (!strcmp(argv[0], statsSpecifier) && !options.stats) {
            options.stats = true;
            argc--;
            argv++;
        } else {
            usage_error();
        }
    }
    if (argc == 1 && strlen(argv[0]) && !options.cmd) {
        // There is only one non-empty arg remaining which doesn't contain "--".
        options.cmd = argv[0];
        argc--;
        argv++;
    }
    if (argc != 0) {
        // There still remains arguments despite everything being checked.
        usage_error();
    }
    return options;
}

// check_dir_and_cmd()
//
// Checks validity of directory and command options. Throws appropriate errors
// based on relevant error cases.
CommandLineOptions check_dir_and_cmd(CommandLineOptions options)
{
    // Set default dir and cmd if not already set.
    if (!options.dirname) {
        options.dirname = ".";
    }
    if (!options.cmd) {
        options.cmd = "echo {}";
    }

    // Check directory is valid
    DIR* testDirOpen = opendir(options.dirname);
    if (testDirOpen == NULL) {
        // Error with dir
        closedir(testDirOpen);
        invalid_dir_error(options.dirname);
    }
    closedir(testDirOpen);

    // Check cmd is valid
    CommandPipeline* testCmdPipeline = parse_pipeline_string(options.cmd);
    if (testCmdPipeline == NULL) {
        // Error with cmd
        invalid_cmd_error();
    }
    options.pipeline = testCmdPipeline;

    return options;
}

////////////////////////////////////////////////////////////////////////////////

// Directory Scanning & Filtering

////////////////////////////////////////////////////////////////////////////////

// get_directory()
//
// REF: Use of scandir in this function inspired by scandir(3) man page.
// Returns a struct containing all files in a given directory.
Directory get_directory(char* dirname)
{
    Directory dir = {NULL, NULL, 0};

    // Set directory name.
    dir.dirname = dirname;

    setlocale(LC_COLLATE, "en_AU");

    // Get all files in directory.
    dir.numDirent = scandir(dirname, &(dir.namelist), NULL, alphasort);
    if (dir.numDirent == -1) {
        perror("No Files in Directory.");
        exit(EXIT_FAILURE);
    }

    return dir;
}

// generate_filtered_directory()
//
// REF: Use of stat in this function inspired by stat(2) man page.
// Generates and returns a new struct DirArray, which contains an array of all
// valid files, as well as a count of all valid files, in a given scanned
// directory.
DirArray generate_filtered_directory(
        Directory dir, bool allfiles, bool dirnameGiven)
{
    DirArray cleanDir = {NULL, 0};

    for (int n = 0; n < dir.numDirent; n++) {
        // Only let through Regular Files or Symbolic Links.
        if (dir.namelist[n]->d_type != DT_REG
                && dir.namelist[n]->d_type != DT_LNK) {
            free(dir.namelist[n]);
            continue;
        }
        // Only let through hidden files when allfiles is on.
        if (!allfiles && dir.namelist[n]->d_name[0] == '.') {
            free(dir.namelist[n]);
            continue;
        }
        // File is valid (except for Sym links to directories).
        char* trueName = get_true_file_name(
                dir.namelist[n]->d_name, dir.dirname, dirnameGiven);

        // Don't let through Sym links to directories.
        struct stat sb;
        if (stat(trueName, &sb) == -1) {
            if (errno != EACCES) {
                printf("%s\n", trueName);
                perror("stat");
                exit(UNEXPECTED_EXIT);
            } else {
                free(trueName);
                free(dir.namelist[n]);
                continue;
            }
        }
        if (dir.namelist[n]->d_type == DT_LNK && S_ISDIR(sb.st_mode)) {
            free(trueName);
            free(dir.namelist[n]);
            continue;
        }

        cleanDir = add_file_to_dir_array(cleanDir, trueName);

        free(dir.namelist[n]);
    }

    free(dir.namelist);

    return cleanDir;
}

// get_true_file_name()
//
// Returns PATH representation of given filename, concatenating it with a given
// directory name and '/' separators if necessary.
char* get_true_file_name(char* dName, char* dirname, bool dirnameGiven)
{
    char* trueName;

    if (strcmp(dirname, ".") != 0
            || (strcmp(dirname, ".") == 0 && dirnameGiven)) {
        // Directory is not current directory so path needs to be added.
        trueName = strdup(dirname);

        if (trueName[strlen(trueName) - 1] != '/') {
            // dirname doesn't end with '/' so it needs to be added.
            trueName = realloc(trueName, (strlen(trueName) + 1) * sizeof(char));
            strcat(trueName, "/");
        }

        // Add file name to path.
        // +1 in realloc for null terminator. Must realloc as size allocated for
        // trueName is only enough for strdup up until this point.
        trueName = realloc(trueName,
                (strlen(trueName) + strlen(dName) + 1) * sizeof(char));
        strcat(trueName, dName);
    } else {
        // Current directory - no pathway needed for file name.
        trueName = strdup(dName);
    }

    return trueName;
}

// add_file_to_dir_array()
//
// REF: Structure of this function from a1 uqunscramble sample solution
// REF: (add_word_to_set).
// Adds a file to struct DirArray, handling memory allocation resizing.
DirArray add_file_to_dir_array(DirArray dir, char* filename)
{
    // Grow the array of files to hold one more.
    dir.numFiles++;
    dir.files = realloc(dir.files, dir.numFiles * sizeof(char*));
    dir.files[dir.numFiles - 1] = filename;
    return dir;
}

////////////////////////////////////////////////////////////////////////////////

// Placeholder Substitution

////////////////////////////////////////////////////////////////////////////////

// sub_placeholders()
//
// Creates a copy of a given CommandPipeline, however with any instance of the
// placeholder "{}" replaced with the name of a given file. Handles this within
// I/O file names as well as cmdArrays.
SubbedPipeline sub_placeholders(CommandPipeline* pipeline, char* file)
{
    SubbedPipeline subPipeline = {NULL, NULL, NULL, 0, NULL};

    // Set file name to be executed on.
    subPipeline.fileExecName = strdup(file);
    // Copy stdinFileName over.
    if (pipeline->stdinFileName != NULL) {
        subPipeline.stdinFileName
                = sub_placeholder_in_string(pipeline->stdinFileName, file);
    }
    // Copy stdoutFileName over.
    if (pipeline->stdoutFileName != NULL) {
        subPipeline.stdoutFileName
                = sub_placeholder_in_string(pipeline->stdoutFileName, file);
    }
    // Copy numCmds over.
    subPipeline.numCmds += pipeline->numCmds;
    // Copy cmdArray over.
    subPipeline.cmdArray = malloc(sizeof(char**) * pipeline->numCmds);
    for (int i = 0; i < pipeline->numCmds; i++) {
        subPipeline.cmdArray[i]
                = sub_placeholder_in_array(pipeline->cmdArray[i], file);
    }

    return subPipeline;
}

// sub_placeholder_in_array()
//
// Replaces any instances of placeholder "{}" with given replacement in an
// array.
char** sub_placeholder_in_array(char** cmdArray, char* replacement)
{
    int numEl = 0;
    while (cmdArray[numEl] != NULL) {
        numEl++;
    }
    numEl++; // Increment numEl to accomodate NULL terminating pointer.

    // Create new array, which replaces any instances of {} with filename.
    char** newCmdArray = malloc(numEl * sizeof(char*));

    for (int j = 0; j < (numEl - 1); j++) {
        if (!strcmp("{}", cmdArray[j])) {
            newCmdArray[j] = strdup(replacement);
        } else {
            newCmdArray[j] = strdup(cmdArray[j]);
        }
    }
    // NULL terminate exec array.
    newCmdArray[numEl - 1] = NULL;

    return newCmdArray;
}

// sub_placeholder_in_string()
//
// REF: Heard tutor mention "memcpy" function to another student, and looked
// REF: man pages to find it's use. Hence, use of "memcpy" function was inspired
// REF: by tutors.
// Substitutes placeholder "{}" in original string with given replacement
// string.
char* sub_placeholder_in_string(char* original, char* replacement)
{
    size_t lenOriginal = strlen(original);
    size_t lenReplacement = strlen(replacement);
    size_t lenPlaceholder = 2; // Length of "{}".

    // Find number of placeholders in original.
    size_t placeholderCount = 0;
    for (size_t i = 0; i < lenOriginal - 1; i++) {
        if (original[i] == '{' && original[i + 1] == '}') {
            placeholderCount++;
            i++; // Move past "{}"
        }
    }

    // Allocate memory for new subbed string.
    size_t newLen = lenOriginal
            + (placeholderCount * (lenReplacement - lenPlaceholder));
    char* subbed
            = malloc(sizeof(char) * (newLen + 1)); // +1 for null terminator.

    size_t i = 0; // Index for traversal of original.
    size_t j = 0; // Index for traversal of subbed with placeholders.

    while (i < lenOriginal) {
        if (original[i] == '{' && original[i + 1] == '}') {
            // Copy replacement string to subbed.
            memcpy(subbed + j, replacement, lenReplacement);
            j += lenReplacement; // Move j forward by length of replacement.
            i += 2; // Move past "{}".
        } else {
            // Copy current character from original to subbed.
            subbed[j] = original[i];
            i++;
            j++;
        }
    }

    // Null-terminate the new string.
    subbed[j] = '\0';

    return subbed;
}

////////////////////////////////////////////////////////////////////////////////

// Command Execution

////////////////////////////////////////////////////////////////////////////////

// execute_command_pipeline()
//
// Executes a given command pipeline. Handles I/O redirection. It is assumed
// that the placeholder substitution occurs before calling this function (as it
// takes a SubbedPipeline as opposed to a CommandPipeline).
PidArray execute_command_pipeline(SubbedPipeline pipeline)
{
    PidArray pidArray = {NULL, 0};
    bool ioOk = true;
    int in = 0;
    int out = 1;

    redirect_io_files(pipeline, &in, &out, &ioOk);

    int pipeArray[pipeline.numCmds - 1][2];

    populate_pipe_array(pipeArray, pipeline.numCmds - 1);

    if (ioOk) {
        // Iterate through commands and execute with redirected input and
        // output.
        for (int j = 0; j < pipeline.numCmds; j++) {

            // Initialise new child process.
            ChildWorker newChild = {NULL, NULL, fork()};
            newChild.file = strdup(pipeline.fileExecName);
            newChild.cmd = strdup(pipeline.cmdArray[j][0]);

            pidArray = add_child_to_array(pidArray, newChild);

            if (pidArray.children[j].pid == 0) {

                // Child.
                // Execute command within child process.
                execute_child(pipeline, j, pipeArray, in, out);
            }
        }
    }

    // Parent
    // Close all file descriptors from the parent's end.
    close_pipe_fds(pipeArray, pipeline.numCmds - 1);

    return pidArray;
}

// redirect_io_files()
//
// Checks if given input and output files are valid. i.e. can be opened. If yes,
// then this function opens them, setting an appropriate file descriptor.
void redirect_io_files(SubbedPipeline pipeline, int* in, int* out, bool* ioOk)
{
    // Don't redirect stdin/out in parent, just check they can open.
    if (pipeline.stdinFileName != NULL) {
        // stdin should be redirected from infile.
        *in = open(pipeline.stdinFileName, O_RDONLY);
        if (*in < 0) {
            read_error(pipeline.stdinFileName, pipeline.fileExecName);
            *ioOk = false;
        }
    }
    if (pipeline.stdoutFileName != NULL) {
        // stdout should be redirected from outfile.
        *out = open(
                pipeline.stdoutFileName, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
        if (*out < 0) {
            write_error(pipeline.stdoutFileName, pipeline.fileExecName);
            *ioOk = false;
        }
    }
}

// populate_pipe_array()
//
// Populates a given pipe array with given numPipes amount of pipes.
void populate_pipe_array(int pipeArray[][2], int numPipes)
{
    // If there is more than one command, create pipe(s) to connect them.
    for (int i = 0; i < numPipes; i++) {
        // Populate pipe file descriptors and automatically add to array.
        if (pipe(pipeArray[i]) < 0) {
            perror("Failed to create pipe");
            exit(FAILED_TO_EXECUTE);
        }
    }
}

// execute_child()
//
// The entirety of this function is executed in a child process created to
// execute a command. Handles I/O redirection, file descriptor closing, and
// command execution.
void execute_child(
        SubbedPipeline pipeline, int j, int pipeArray[][2], int in, int out)
{
    // Handle input redirection for the first command or for stdin redirection.
    if (j == 0) {
        if (pipeline.stdinFileName) {
            dup2(in, STDIN_FILENO);
            close(in);
        }
    } else {
        dup2(pipeArray[j - 1][0], STDIN_FILENO);
    }

    // Handle output redirection for the last command or for stdout redirection.
    if (j == (pipeline.numCmds - 1)) {
        if (pipeline.stdoutFileName) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }
    } else {
        dup2(pipeArray[j][1], STDOUT_FILENO);
    }

    // Close all file descriptors from child's end.
    close_pipe_fds(pipeArray, pipeline.numCmds - 1);

    // Execute the command.
    execvp(pipeline.cmdArray[j][0], pipeline.cmdArray[j]);

    // If execution failed, exit the child process.
    exit(UNEXPECTED_EXIT);
}

// close_pipe_fds()
//
// Closes both file descriptors in each pipe in a given array of pipes.
void close_pipe_fds(int pipeArray[][2], int numPipes)
{
    for (int i = 0; i < numPipes; i++) {
        close(pipeArray[i][0]); // Close read end.
        close(pipeArray[i][1]); // Close write end.
    }
}

// add_child_to_array()
//
// Add the given ChildWorker to given PidArray and return the updated PidArray.
PidArray add_child_to_array(PidArray pidArray, ChildWorker newChild)
{
    // Grow the array of files to hold one more.
    pidArray.numPids++;
    pidArray.children = realloc(
            pidArray.children, pidArray.numPids * sizeof(ChildWorker));
    pidArray.children[pidArray.numPids - 1] = newChild;
    return pidArray;
}

// wait_on_child_array()
//
// Waits on each child process in given PidArray and returns information about
// the execution of the set of children.
int wait_on_child_array(PidArray pidArray)
{
    bool nonZeroExitStatus = false;
    bool exitDueToSignal = false;
    bool failedToExecute = false;

    int status;
    for (int i = 0; i < pidArray.numPids; i++) {
        waitpid(pidArray.children[i].pid, &status, 0);

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == UNEXPECTED_EXIT) {
                execution_error(
                        pidArray.children[i].cmd, pidArray.children[i].file);
                failedToExecute = true;
            }
            if (WEXITSTATUS(status) != 0
                    && WEXITSTATUS(status) != UNEXPECTED_EXIT) {
                // Regular but non-zero exit.
                nonZeroExitStatus = true;
            }
        } else if (WIFSIGNALED(status)) {
            if (status != CMD_NOT_EXECUTED) {
                exitDueToSignal = true;
            }
        }
    }
    if (pidArray.numPids == 0) /*No children were created - io problem*/ {
        failedToExecute = true;
    }

    int ret = EXIT_ALL_ZERO;

    if (nonZeroExitStatus) {
        ret = EXIT_NON_ZERO;
    }
    if (exitDueToSignal) {
        ret = EXIT_SIGNALLED;
    }
    if (failedToExecute) {
        ret = CMD_NOT_EXECUTED;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////

// File Iteration

////////////////////////////////////////////////////////////////////////////////

// iterate_files()
//
// Iterates over files in given DirArray and executes given command on each.
// Returns a PidLog that will be initialised to NULL if in sequential mode, but
// if in parallel mode will be initialised with all pids that need to be waited
// on.
PidLog iterate_files(DirArray cleanDir, CommandLineOptions checkedOptions,
        Statistics* stats, bool* allFilesProcessed)
{
    PidLog log = {NULL, 0};

    for (int i = 0; i < cleanDir.numFiles; i++) {

        // Placeholder substitution.
        SubbedPipeline subP
                = sub_placeholders(checkedOptions.pipeline, cleanDir.files[i]);

        // Command Execution.
        PidArray pidArray = execute_command_pipeline(subP);

        if (!(checkedOptions.parallel)) {
            // Wait on each file sequentially if not in parallel mode.
            int result = wait_on_child_array(pidArray);

            free_pidarray(pidArray);

            *stats = update_stats(*stats, result);

        } else {
            // If in parallel, add PidArray to continuously growing log.
            log = add_pidarray_to_log(log, pidArray);
        }

        free_subbed_pipeline(subP);

        if (i == (cleanDir.numFiles - 1)) {
            *allFilesProcessed = true;
        }

        if (!(checkedOptions.parallel) && sigInterupt) {
            // Break after processing finished if sig interupted.
            break;
        }
    }

    return log;
}

////////////////////////////////////////////////////////////////////////////////

// Statistics Functionality

////////////////////////////////////////////////////////////////////////////////

// update_stats()
//
// Updates given struct Statistics based on information about execution of
// command pipeline.
Statistics update_stats(Statistics stats, int waitInfo)
{
    stats.n1++;

    switch (waitInfo) {
    case EXIT_ALL_ZERO:
        ++(stats.n2);
        break;
    case EXIT_NON_ZERO:
        ++(stats.n3);
        break;
    case EXIT_SIGNALLED:
        ++(stats.n4);
        break;
    case CMD_NOT_EXECUTED:
        ++(stats.n5);
        break;
    }

    return stats;
}

// print_statistics()
//
// Prints statistics messages with stats from given Statistics struct.
void print_statistics(Statistics stats)
{
    fprintf(stderr, n1Message, stats.n1);
    fprintf(stderr, n2Message, stats.n2);
    fprintf(stderr, n3Message, stats.n3);
    fprintf(stderr, n4Message, stats.n4);
    fprintf(stderr, n5Message, stats.n5);
}

////////////////////////////////////////////////////////////////////////////////

// Parallel Functionality

////////////////////////////////////////////////////////////////////////////////

// add_pidarray_to_log()
//
// Add the given array of ChildWorkers (PidArray) to the given PidLog and return
// the updated PidLog.
PidLog add_pidarray_to_log(PidLog log, PidArray pArr)
{
    // Grow the array of PidArrays to hold one more.
    log.numLoops++;
    log.pidArrays = realloc(log.pidArrays, log.numLoops * sizeof(PidArray));
    log.pidArrays[log.numLoops - 1] = pArr;
    return log;
}

// wait_on_pid_log()
//
// Waits on all PidArrays in given PidLog. Also updates given Statistics.
void wait_on_pid_log(PidLog log, Statistics* stats)
{
    if (log.numLoops != 0) {
        // Wait on all files after command executed on all files if in parallel.
        for (int i = 0; i < log.numLoops; i++) {
            int result = wait_on_child_array(log.pidArrays[i]);

            free_pidarray(log.pidArrays[i]);

            *stats = update_stats(*stats, result);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// Memory Cleaning

////////////////////////////////////////////////////////////////////////////////

// free_dir_array()
//
// Frees malloc-ed memory stored in given DirArray (char** files).
void free_dir_array(DirArray dir)
{
    for (int i = 0; i < dir.numFiles; i++) {
        free(dir.files[i]);
    }
    free(dir.files);
}

// free_subbed_pipeline()
//
// Frees malloc-ed memory stored in given SubbedPipeline.
void free_subbed_pipeline(SubbedPipeline subbedPipeline)
{
    // Free fileExecName.
    if (subbedPipeline.fileExecName != NULL) {
        free(subbedPipeline.fileExecName);
    }
    // Free stdinFileName.
    if (subbedPipeline.stdinFileName != NULL) {
        free(subbedPipeline.stdinFileName);
    }
    // Free stdoutFileName.
    if (subbedPipeline.stdoutFileName != NULL) {
        free(subbedPipeline.stdoutFileName);
    }
    // numCmds not malloc-ed, so no need for free.

    // Free cmdArray.
    if (subbedPipeline.cmdArray != NULL) {
        for (int i = 0; i < subbedPipeline.numCmds; i++) {
            // Free each char* in cmdArray[i]
            int n = 0;
            while (subbedPipeline.cmdArray[i][n] != NULL) {
                free(subbedPipeline.cmdArray[i][n]);
                n++;
            }
            free(subbedPipeline.cmdArray[i]);
        }
        free(subbedPipeline.cmdArray);
    }
}

// free_childworker()
//
// Frees the dynamically allocated memory associated with the given ChildWorker.
void free_childworker(ChildWorker cw)
{
    if (cw.file != NULL) {
        free(cw.file);
    }
    if (cw.cmd != NULL) {
        free(cw.cmd);
    }
}

// free_pidarray()
//
// Frees the dynamically allocated memory associated with the given array of
// processes.
void free_pidarray(PidArray pidArray)
{
    for (int i = 0; i < pidArray.numPids; i++) {
        free_childworker(pidArray.children[i]);
    }
    free(pidArray.children);
}

////////////////////////////////////////////////////////////////////////////////

// Signal Handling

////////////////////////////////////////////////////////////////////////////////

// set_up_signal_handlers()
//
// Sets up variables and structs required for signal handling.
void set_up_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = notice;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, 0);
}

// notice()
//
// Signal handler function. Sets global signal handler interupt flag to true.
void notice(int s)
{
    s = s;
    sigInterupt = true;
}

////////////////////////////////////////////////////////////////////////////////

// Exit Status Logic

////////////////////////////////////////////////////////////////////////////////

// compute_exit_status()
//
// Determines the appropriate exit status of the program based on relevant
// information.
int compute_exit_status(Statistics stats, bool parallel, bool allFilesProcessed)
{
    int ret = SUCCESS;

    if (stats.n5 > 0) {
        ret = PROCESSING_FAILED;
    }
    if (!parallel && sigInterupt && !allFilesProcessed) {
        ret = SIG_INTERUPT;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////

// Error Message Functions

////////////////////////////////////////////////////////////////////////////////

// usage_error()
//
// Print a usage error message and exit with the appropriate code.
void usage_error(void)
{
    fprintf(stderr, usageErrorMessage);
    exit(USAGE_ERROR);
}

// invalid_dir_error()
//
// Print an error message about an invalid directory provided as an argument
// and exit with the appropriate code.
void invalid_dir_error(char* invalidDir)
{
    fprintf(stderr, invalidDirMessage, invalidDir);
    exit(INVALID_DIR);
}

// invalid_cmd_error()
//
// Print an error message about an invalid command provided as a program
// argument and exit with the appropriate code.
void invalid_cmd_error(void)
{
    fprintf(stderr, invalidCmdMessage);
    exit(INVALID_CMD);
}

// read_error()
//
// Print an error message about a file that cannot be read while processing
// another given file.
void read_error(char* filename1, char* filename2)
{
    fprintf(stderr, invalidReadMessage, filename1, filename2);
}

// write_error()
//
// Print an error message about a file that cannot be written to while
// processing another given file.
void write_error(char* filename1, char* filename2)
{
    fprintf(stderr, invalidWriteMessage, filename1, filename2);
}

// execution_error()
//
// Print an error message about a command that could not be executed on a given
// file.
void execution_error(char* cmd, char* filename)
{
    fprintf(stderr, invalidExecutionMessage, cmd, filename);
}
