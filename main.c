#include "rs232.h"
#include <stdlib.h>
#include <assert.h>

struct OscilloscopeSetup
{
    unsigned int format, type, points, count;
    float xIncrement, xOrigin, xReference, yIncrement, yOrigin, yReference;
};

char* read_serial_blocking(char* buffer);
int parse_ascii_data(char* ascii_data, unsigned short* data, int header_size);
int readPreamble(struct OscilloscopeSetup* setup);

// Certain data blocks are prefixed by a header of a fixed size that contains
// some (redundant) information about the data about to be transmitted.
// On the HP545XX models, it is always 10 ascii characters in length
#define HEADER_SIZE 10

// The numerical value for the serial port that we are using as defined by
// the RS232 library. This need to be determined by command-line switches,
// but for my setup it's always 16
const unsigned char comportNum = 16;

// Baudrate of the machine. Needs to be a command-line switch
const unsigned int baudrate = 19200;

int main(){

    if(RS232_OpenComport(comportNum, baudrate, "8N1",10))
    {
        printf("Can not open comport\n");

        return(0);
    }

    // Place a simple ID request to put the target into remote mode
    RS232_cputs(comportNum, "*idn?\n");
    usleep(100000); // Wait 100ms for the oscilloscope to process that request
    RS232_flushRXTX(comportNum);// Reset the OS serial buffer, discarding the previous response

    // Setup the waveform interface to take our measurements
    RS232_cputs(comportNum, ":waveform:format word\n");
    RS232_cputs(comportNum, ":waveform:source chan1\n");
    RS232_cputs(comportNum, ":waveform:format ascii\n");

    // Read in information about the oscilloscope setup necessary
    // to reconstruct the waveform
    struct OscilloscopeSetup currentSetup;
    int fieldsRead = readPreamble(&currentSetup);

    if(fieldsRead != 10){
        printf("Only returned %d fields\n" , fieldsRead);
        return 1;
    }

    // A buffer to hold the raw ascii string read in from the serial port.
    // Each entry in the comma seperated list can be up to six characters, including the comma
    char* asciiData = malloc(sizeof(char) * currentSetup.points * 6 + 10 + 2);

    // A buffer to hold the parsed numberical data
    unsigned short* data = malloc(sizeof(unsigned int) * currentSetup.points);

    // Two buffers to hold the computed voltage and time values
    double* timestamps = malloc(sizeof(double) * currentSetup.points);
    double* values = malloc(sizeof(double) * currentSetup.points);

    printf("Reading %d points\n", currentSetup.points);

    RS232_cputs(comportNum, ":waveform:data?\n");
    read_serial_blocking(asciiData);
    int foundPoints = parse_ascii_data(asciiData, data, HEADER_SIZE);

    printf("Parsed %d points\n", foundPoints);

    // From the raw data received, reconstruct the waveform.
    // See page 23-9
    for(int i = 0; i < currentSetup.points; i++){
        values[i] = ((data[i] - currentSetup.yReference) * currentSetup.yIncrement) + currentSetup.yOrigin;
        timestamps[i] = ((i - currentSetup.xReference) * currentSetup.xIncrement) + currentSetup.xOrigin;
    }

    FILE* file = fopen("output.csv", "w");

    for(int i = 0; i < currentSetup.points; i++){
        printf("%G, %G\n", timestamps[i], values[i]);
        fprintf(file, "%G, %G\n", timestamps[i], values[i]);
    }

    fclose(file);
    RS232_CloseComport(comportNum);

    return 0;
}

// Read the waveform preamble which contains all the information
// about the oscilloscope's current setup. Returns the number of fields
// successfully parsed from the preamble (should be 10)
int readPreamble(struct OscilloscopeSetup* setup){
    // Couldn't find any real data on the maximum length of the preamble, 
    // but this is twice as long as longest theoretical example I could come up with.
    char preamble[256];
    memset(preamble, 0, sizeof preamble); //Fill preamble with zeros

    RS232_cputs(comportNum, ":waveform:preamble?\n");

    read_serial_blocking(preamble);

    int fieldsRead = sscanf(preamble, "%d,%d,%d,%d,%G,%G,%G,%G,%G,%G",
        &setup->format,
        &setup->type,
        &setup->points,
        &setup->count,
        &setup->xIncrement,
        &setup->xOrigin,
        &setup->xReference,
        &setup->yIncrement,
        &setup->yOrigin,
        &setup->yReference);

    return fieldsRead;
}

// reads in a stream of input until reading in a newline. Will block,
// potentially forever
char* read_serial_blocking(char* buffer){
    unsigned char buf[4096];
    while(1)
    {
        int n = RS232_PollComport(comportNum, buf, 4095);

        if(n > 0)
        {
            buf[n] = 0;   /* always put a "null" at the end of a string! */

            strcat(buffer, (char*)buf); // Suppresses a warning about diffrent signdedness

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