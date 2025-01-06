/*
 * app_packet.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_APP_PACKET_H_
#define MAIN_APP_PACKET_H_

#include <stdio.h>
#include <unistd.h>
#include "system.h"

#define REG_HEADER  0x0001
#define DEL_HEADER  0x0002
#define DATA_HEADER 0x0003
#define HEADER_SIZE 2//sizeof(uint16_t)


// forms a packet by adding a header and optional data
int8_t form_packet(uint8_t* dest_buff, uint16_t header_tag, const uint8_t* data_buff, uint8_t data_buff_len)
{
    if (dest_buff == NULL)  // check if the destination buffer is not NULL
        return -1;

    // if the system uses little-endian, reverse the bytes of the header
    uint8_t endianness = check_endianness();
    if (endianness == L_ENDIAN)
        reverse_bytes((uint8_t*)&header_tag, sizeof(header_tag));

    memcpy(dest_buff, &header_tag, HEADER_SIZE);

    // if there is data, copy it into the destination buffer after the header
    if (data_buff != NULL)
        memcpy(dest_buff + HEADER_SIZE, data_buff, data_buff_len);

    return 0;
}


// opens and parses a packet, extracting the header and optional data
int8_t open_packet(uint16_t* dest_header, uint8_t* dest_buff, const uint8_t* packet, uint8_t packet_len)
{
    // if the packet length is too small to contain a header or
    // the packet is NULL, return error
    if (packet_len < HEADER_SIZE || packet == NULL)
        return -1;

    uint8_t header_arr[HEADER_SIZE];
    memcpy(header_arr, packet, HEADER_SIZE);

    // if the system uses little-endian, reverse the bytes of the header
    uint8_t endianness = check_endianness();
    if (endianness == L_ENDIAN)
        reverse_bytes((uint8_t*)&header_arr, sizeof(header_arr));

    // check if the header matches valid types (registration, deletion, or data)
    uint16_t header = *(uint16_t*)header_arr;
    if ((header != REG_HEADER) && (header != DEL_HEADER) && (header != DATA_HEADER))
        return -1;

    *dest_header = header;

    // if packet is longer than header (contains data) and destination buffer not NULL
    if (packet_len > HEADER_SIZE && dest_buff != NULL)
        memcpy(dest_buff, packet + HEADER_SIZE, packet_len - HEADER_SIZE);

    return 0;
}


// extracts and validates the header from a packet
int8_t get_packet_header(uint16_t* dest_header, const uint8_t* packet)
{
    // if the packet size is smaller than the header size or
    // the packet is NULL, return error
    if (sizeof(packet) < HEADER_SIZE || packet == NULL)
        return -1;

    uint8_t header_arr[HEADER_SIZE];
    memcpy(header_arr, packet, HEADER_SIZE);

    // if the system uses little-endian, reverse the bytes of the header
    uint8_t endianness = check_endianness();
    if (endianness == L_ENDIAN)
        reverse_bytes((uint8_t*)&header_arr, sizeof(header_arr));

    // check if the header matches valid types (registration, deletion, or data)
    uint16_t header = *(uint16_t*)header_arr;
    if ((header != REG_HEADER) && (header != DEL_HEADER) && (header != DATA_HEADER))
        return -1;

    *dest_header = header;
    return 0;
}

#endif /* MAIN_APP_PACKET_H_ */
