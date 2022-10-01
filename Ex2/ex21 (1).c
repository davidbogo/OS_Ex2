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
#include <ctype.h>

//David Bogoslavsky

#define FALSE   0
#define TRUE    1

int is_blank(char ch)
{
    return ((ch == ' ') || (ch == '\n')) ? 1 : 0;
}

int main(int argc, char* argv[]) 
{
    int ret = 0;
    char ch1;
    char ch2;
    int fd1, fd2;
    int read1 = 0;
    int read2 = 0;
    int diff = FALSE;
    int carry1 = FALSE;
    int carry2 = FALSE;
    if (argc != 3) {
        perror("illegal number of arguments\n");
        return 0;
    }
    fd1 = open(argv[1], O_RDONLY);
    if (fd1 == -1) {
        perror("non readable file\n");
        return 0;
    }
    fd2 = open(argv[2], O_RDONLY);
    if (fd2 == -1) {
        perror("non readable file\n");
        close(fd1);
        return 0;
    }
    while (TRUE) {
        int read_more1 = FALSE;
        int read_more2 = FALSE;
        if (!carry1) {
            read1 = read(fd1, &ch1, sizeof(ch1));
            if (read1 == -1){
                perror("non readable file 1\n");
                break;
            }
        } else
            carry1 = FALSE;
        if (!carry2) {
            read2 = read(fd2, &ch2, sizeof(ch2));
            if (read2 == -1){
                perror("non readable file 2\n");
                break;
            }
        } else
            carry2 = FALSE;
        if (read1 == 0 && read2 == 0) {
            // reached the end of both files
            ret = diff ? 3 : 1;
            break;
        } else if (read1 == 0) {
            if (is_blank(ch2)) {
                // reached the end of the first file,
                // need to keep reading from the second one
                read_more2 = TRUE;
                diff = TRUE;
            } else {
                ret = 2;
                break;
            }
        } else if (read2 == 0) {
            if (is_blank(ch1)) {
                // reached the end of the second file,
                // need to keep reading from the first one
                read_more1 = TRUE;
                diff = TRUE;
            } else {
                ret = 2;
                break;
            }
        }
        else if (ch1 != ch2) {
            diff = TRUE;
            if (tolower(ch1) == tolower(ch2)) {
                continue;
            }
            if (is_blank(ch1) || is_blank(ch2)) {
                if (is_blank(ch1))
                    read_more1 = TRUE;
                else
                    carry1 = TRUE; // We already have a char in ch1
                if (is_blank(ch2))
                    read_more2 = TRUE;
                else
                    carry2 = TRUE; // We already have a char in ch2
            } else {
                ret = 2;
                break;
            }
        }
        if (read_more1) {
            do {
                read1 = read(fd1, &ch1, sizeof(ch1));
            } while ((read1 == 1) && is_blank(ch1));
            if (read1 == 1) {
                carry1 = TRUE;
            }
        }
        if (read_more2) {
            do {
                read2 = read(fd2, &ch2, sizeof(ch2));
            } while ((read2 == 1) && is_blank(ch2));
            if (read2 == 1) {
                carry2 = TRUE;
            }
        }
    }
    close(fd1);
    close(fd2);
    return ret;
}
