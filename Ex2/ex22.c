#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <dirent.h>
#include <time.h>

/*
 * David Bogoslavsky
 */

#define USER_PROG_TIME_LIMIT        5

void readfileline(int fd, char *buf, size_t buf_len)
{
    int offset = 0;
    if (!buf || buf_len == 0)
        return;
    while (offset + 1 < buf_len) {
        size_t bytes_read = read(fd, buf + offset, 1);
        if (bytes_read == 0)
            break;
        if ((buf[offset] == '\n') || (buf[offset] == EOF))
            break;	// the character will later be replaced by 0
        offset++;
    }
    buf[offset] = 0;
}

int find_first_cfile(DIR *user_dir, char *cfile_name)
{
    int found = 0;
    struct dirent *cur_file;
    while ((cur_file = readdir(user_dir)) != NULL) {
        if (cur_file->d_type == DT_REG) {
            size_t name_len = strlen(cur_file->d_name);
            if (name_len > 2) {
                if ((cur_file->d_name[name_len - 2] == '.') &&
                    (cur_file->d_name[name_len - 1] == 'c')) {
                    strcpy(cfile_name, cur_file->d_name);
                    found = 1;
                    break;
                }
            }
        }
    }
    return found;
}

void log_grade(int grades_fd, const char *student_name, char *grade, char *reason)
{
    char gradestring[200];
    strcpy(gradestring, student_name);
    strcat(gradestring, ",");
    strcat(gradestring, grade);
    strcat(gradestring, ",");
    strcat(gradestring, reason);
    strcat(gradestring, "\n");
    write(grades_fd, gradestring, strlen(gradestring));
}

int main(int argc, char** argv)
{
    char path_to_dir[512];
    char path_to_input[512];
    char path_to_output[512];
    DIR* students;
    struct dirent *cur_dir;
    int conf_fd, grades_fd, error_fd;
    if (argc != 2) {
        perror("Error in: number of arguments\n");
        return -1;
    }
    //open config file and read locations from it
    conf_fd = open(argv[1], O_RDONLY);
    if (conf_fd < 0) {
        perror("Error in: open\n");
        return -1;
    }
    readfileline(conf_fd, path_to_dir, sizeof(path_to_dir));
    if (path_to_dir[0] == 0) {
        close(conf_fd);
        perror("Directory name not supplied\n");
        return -1;
    }
    readfileline(conf_fd, path_to_input, sizeof(path_to_input));
    if (path_to_input[0] == 0) {
        close(conf_fd);
        perror("Input file name not supplied\n");
        return -1;
    }
    readfileline(conf_fd, path_to_output, sizeof(path_to_output));
    close(conf_fd);
    if (path_to_output[0] == 0) {
        perror("Output name not supplied\n");
        return -1;
    }
    //open the grades file to write grades
    grades_fd = open("results.csv", O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
    if (grades_fd < 0) {
        perror("Error in: open\n");
        return -1;
    }
    error_fd = open("errors.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
    if (error_fd < 1) {
        perror("Couldn't create error file\n");
        return -1;
    }
    close(error_fd);
    //open students dirs
    students = opendir(path_to_dir);
    if (students == NULL) {
        close(grades_fd);
        perror("Not a valid directory\n");
        return -1;
    }
    while ((cur_dir = readdir(students)) != NULL) {
        pid_t child_pid;
        char full_cfile_name[512];
        char cfile_name[128];
        char executable_file_name[512];
        char user_output_file_name[512];
        int res;
        int input_fd, output_fd;
        time_t start_time, cur_time;
        int time_out = 0;
        DIR *user_dir;
        char student_output_path[256];
        char student_exe_path[256];
        int child_exit_code;
        int comp_ret_value = -1;
        if (cur_dir->d_type != DT_DIR)
            continue;   // Not a directory
        if (!strcmp(cur_dir->d_name, "."))
            continue;   // This directory
        if (!strcmp(cur_dir->d_name, ".."))
            continue;   // The upper directory
        strcpy(full_cfile_name, path_to_dir);
        strcat(full_cfile_name, "/");
        strcat(full_cfile_name, cur_dir->d_name);
        user_dir = opendir(full_cfile_name); // We only have the directory name in
                                             // full_cfile_name at this point
        if (user_dir == NULL) {
            perror("Error in: opendir\n");
            continue;
        }
        res = find_first_cfile(user_dir, cfile_name);
        closedir(user_dir);
        if (res == 0) {
            // No .c file found in the directory
            log_grade(grades_fd, cur_dir->d_name, "0", "NO_C_FILE");
            continue;
        }
        strcat(full_cfile_name, "/");
        strcpy(executable_file_name, full_cfile_name);
        strcpy(user_output_file_name, full_cfile_name);
        strcat(full_cfile_name, cfile_name);
        strcat(executable_file_name, "main.out");
        strcat(user_output_file_name, "output.txt");
        remove(executable_file_name);   // Just in case. If the output file name
                                        // already exists, it will be deleted
                                        // No need to check the result, failure is
                                        // OK and even expected
        error_fd = open("errors.txt", O_WRONLY);
        if (error_fd < 1) {
            perror("Couldn't create error file\n");
            continue;
        }
        child_pid = fork();
        switch (child_pid) {
        case 0:
            // We're the child
            dup2(error_fd, STDERR_FILENO);
            if (execlp("gcc", "gcc", "-o", executable_file_name, full_cfile_name,  NULL) == -1)
                perror("Error in: execlp\n");
            exit(0);
            break;
        case -1:
            perror("Error in: fork\n");
            break;
        default:
            // We're the parent
            wait(NULL);
            res = open(executable_file_name, O_RDONLY);
            if (res < 0) {
                // Compilation failed
                log_grade(grades_fd, cur_dir->d_name, "10", "COMPILATION_ERROR");
                continue;
            } else {
                close(res);
            }
            break;
        }
        close(error_fd);
        // if we got here it means we have successfully compiled the user's file
        // We'll run it now
        output_fd = open(user_output_file_name, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
        if (output_fd < 1) {
            perror("Couldn't create output file\n");
            continue;
        }
        error_fd = open("errors.txt", O_WRONLY);
        if (error_fd < 1) {
            perror("Couldn't create error file\n");
            continue;
        }
        input_fd = open(path_to_input, O_RDONLY);
        if (input_fd < 1) {
            perror("Couldn't open input file\n");
            close(output_fd);
            continue;
        }
        time(&start_time);
        child_pid = fork();
        switch (child_pid) {
        case 0:
            // We're the child
            dup2(input_fd, STDIN_FILENO);
            dup2(output_fd, STDOUT_FILENO);
            dup2(error_fd, STDERR_FILENO);
            if (execl(executable_file_name, executable_file_name,  NULL) == -1)
                perror("Error in: execl\n");
            exit(0);
            break;
        case -1:
            perror("Error in: fork\n");
            break;
        default:
            // We're the parent
            // We'll wait for the process to end, but no more than 5 seconds
            do {
                res = waitpid(child_pid, NULL, WNOHANG);
                if (res == child_pid) {
                    break;
                } else if (res == 0) {
                    // Still running
                    time(&cur_time);
                    if ((cur_time - start_time) > USER_PROG_TIME_LIMIT) {
                        time_out = 1;
                        break;
                    }
                }
            } while (res != -1);
            if (time_out) {
                log_grade(grades_fd, cur_dir->d_name, "20", "TIMEOUT");
                continue;
            } else if (res == -1) {
                perror("Error in: waitpid\n");
                continue;
            }
            break;
        }
        close(input_fd);
        close(error_fd);
        close(output_fd);
        strcpy(student_output_path, path_to_dir);
        strcat(student_output_path,"/");
        strcat(student_output_path, cur_dir->d_name); //student dir
        strcat(student_output_path,"/output.txt");
        child_pid = fork();
        switch (child_pid) {
        case 0:
            // We're the child
            if (execl("./comp.out", "./comp.out", student_output_path, path_to_output, NULL) == -1)
                printf("Error in: execl\n");
            exit(0);
            break;
        case -1:
            perror("Error in: fork\n");
            break;
        default:
            // We're the parent
            wait(&child_exit_code);
            //take returned status
            if (WIFEXITED(child_exit_code))
            	comp_ret_value = WEXITSTATUS(child_exit_code);
            // make a char that has: "student name,grade,comment"
            switch(comp_ret_value) {
            case 1:
                log_grade(grades_fd, cur_dir->d_name, "100", "EXCELLENT");
                break;
            case 3:
                log_grade(grades_fd, cur_dir->d_name, "75", "SIMILAR");
                break;
            default:
                log_grade(grades_fd, cur_dir->d_name, "50", "WRONG");
                break;
            }
            remove(student_output_path);
            remove(executable_file_name);
            break;
        }
    }
    closedir(students);
    close(grades_fd);
}
