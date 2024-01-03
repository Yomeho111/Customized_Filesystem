#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>


char *read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
	long filesize;
	char *buffer;

    if (file == NULL) {
        perror("Error opening file");
        return NULL;
    }

    // Go to the end of the file.
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Error seeking file");
        fclose(file);
        return NULL;
    }

    // Get the size of the file.
    filesize = ftell(file);
    printf("The size of file is %d\n", filesize);
    if (filesize == -1) {
        perror("Error getting file size");
        fclose(file);
        return NULL;
    }

    // Allocate memory for the file content + null terminator
    buffer = (char *)malloc(filesize);
    if (buffer == NULL) {
        perror("Error allocating memory");
        fclose(file);
        return NULL;
    }

    // Go back to the start of the file and read it into the buffer
    rewind(file);
    size_t read_size = fread(buffer, 1, filesize, file);
    printf("The read_size is %d\n", read_size);
    if (read_size != filesize) {
        perror("Error reading file");
        free(buffer);
        fclose(file);
        return NULL;
    }


    // Close the file and return the buffer
    fclose(file);
    return buffer;
}


int main(void) 
{
    const char *big_img_dir = "./big_img.jpeg";
    char *big_img_contents = read_file(big_img_dir);
    return 0;

}