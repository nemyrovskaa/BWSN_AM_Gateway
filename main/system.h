/*
 * System.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include <stdio.h>
#include <unistd.h>

#define L_ENDIAN    0x01
#define B_ENDIAN    0x00

uint8_t check_endianness()
{
    unsigned int one = 1;
    char* first_byte = (char*)&one;

    return *first_byte;
}

void reverse_bytes(uint8_t* data, size_t size) {
    for (size_t i = 0; i < size / 2; i++)
    {
        uint8_t temp = data[i];
        data[i] = data[size - 1 - i];
        data[size - 1 - i] = temp;
    }
}


#endif /* MAIN_SYSTEM_H_ */
