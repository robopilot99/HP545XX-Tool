#include "rs232.h"
#include <stdlib.h>
#include <assert.h>

char* read_serial_blocking(char* buffer);
int parse_ascii_data(char* ascii_data, unsigned short* data, int header_size);

int main()
{
    // Couldn't find any real data on the maximum length of the preamble, but this is twice as long as longest theoretical example I could come up with.
    char preamble[256];
    memset(preamble, 0, sizeof preamble); //Fill preamble with zeros

    if(RS232_OpenComport(16, 9600, "8N1",10))
    {
        printf("Can not open comport\n");

        return(0);
    }

    RS232_cputs(16, "*idn?\n");

    usleep(100000);  /* sleep for 100 milliSeconds */

    RS232_flushRXTX(16);

    RS232_cputs(16, ":waveform:format word\n");
    RS232_cputs(16, ":waveform:source chan1\n");
    RS232_cputs(16, ":waveform:format ascii\n");
    RS232_cputs(16, ":waveform:preamble?\n");
    usleep(100000);

    read_serial_blocking(preamble);
    printf("Preamble: %s\n", preamble);

    unsigned int format, type, points, count;
    float xIncrement, xOrigin, xReference, yIncrement, yOrigin, yReference;

    int fields = sscanf(preamble, "%d,%d,%d,%d,%G,%G,%G,%G,%G,%G", &format, &type, &points, &count, &xIncrement, &xOrigin, &xReference, &yIncrement, &yOrigin, &yReference);

    if(fields != 10){
        printf("Only returned %d fields\n" , fields);
        return 1;
    }

    printf("Reading %d points\n", points);
    usleep(100000);  /* sleep for 100 milliSeconds */

    const int header_size = 10;

    unsigned char* asciiData = malloc(sizeof(char) * points * 6 + 10 + 2); // Each entry in the comma seperated list can be up to six characters, including the comma
    unsigned short* data = malloc(sizeof(unsigned int) * points);
    double* timestamps = malloc(sizeof(double) * points);
    double* values = malloc(sizeof(double) * points);

    RS232_cputs(16, ":waveform:data?\n");
    read_serial_blocking(asciiData);
    // printf("data: %s\n", asciiData);
    int foundPoints = parse_ascii_data(asciiData, data, header_size);

    printf("Parsed %d points\n", foundPoints);

    for(int i = 0; i < points; i++){
        values[i] = ((data[i] - yReference) * yIncrement) + yOrigin;
        timestamps[i] = ((i - xReference) * xIncrement) + xOrigin;
    }

    FILE* file = fopen("output.csv", "w");

    for(int i = 0; i < points; i++){
        printf("%G, %G\n", timestamps[i], values[i]);
        fprintf(file, "%G, %G\n", timestamps[i], values[i]);
    }

    fclose(file);
    RS232_CloseComport(16);

    return 0;
}

// reads in a stream of input until reading in a newline
char* read_serial_blocking(char* buffer){
    unsigned char buf[4096];
    while(1)
    { 
        int n = RS232_PollComport(16, buf, 4095);

        if(n > 0)
        {
            buf[n] = 0;   /* always put a "null" at the end of a string! */

            strcat(buffer, buf);

            // printf("received %i bytes: %s\n", n, (char *)buf);

            if(buf[n - 1] == 10){ // LF
                return buffer;
            }
        }
    }
}

// Function that handles the parsing of the data returned by
// :waveform:data? in ascii mode. Returns the number of points
// read in
int parse_ascii_data(char* ascii_data, unsigned short* data, int header_size){
    int numPoints = 0; // Number of points in the data array
    int accumulator = 0;
    int stringIndex = 0;
    int dataIndex = 0;
    
    while(1){
        char thisChar = ascii_data[stringIndex];
        if(thisChar >= 48 && thisChar <= 57){
            accumulator *= 10;
            accumulator += (thisChar - 48);
        } else {
            data[dataIndex] = accumulator;
            accumulator = 0;
            dataIndex++;
            if(thisChar == 0 || thisChar == 10){ // NULL, LF
                return dataIndex;
            }
        }
        stringIndex++;
    }
    return dataIndex;
}