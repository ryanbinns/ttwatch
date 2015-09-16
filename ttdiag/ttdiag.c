/* this command will read a .pcap file, parse any watch commands and responses it finds */

#include "libttwatch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>


#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libusb-1.0/libusb.h>

#include <pcap.h>

#define MAXLINECMD 256
#define MAXCMD 128


// Global options:

int opttextfile = 0;
int optpcapfile = 0;
int optpretty = 1;
int optsendpackets = 0;

// global message counter
uint8_t g_msg_counter = 0x01;

TTWATCH *watch = 0;

void print_packet(uint8_t *packet, uint8_t size) {
    if (optpretty) {
        pretty_print_packet(packet, size);
    } else {
        int i;
        for (i = 0; i < size; ++i)
            printf("%02X ", packet[i]);
        printf("\n");
    }

}

int send_raw_packet(TTWATCH *watch, uint8_t tx_length,
    const uint8_t *tx_data, uint8_t *rx_data)
{
    uint8_t packet[64] = {0};

    int count  = 0;
    int result = 0;

    // copy packet to send buffer
    memcpy(packet, tx_data, tx_length);

    //auto set counter;
    packet[2] = g_msg_counter++;

    // send the packet
    result = libusb_interrupt_transfer(watch->device, 0x05, packet, tx_length, &count, 10000);
    if (result || (count != tx_length))
        return TTWATCH_UnableToSendPacket;

    // read the reply
    result = libusb_interrupt_transfer(watch->device, 0x84, packet, 64, &count, 10000);
    if (result)
        return TTWATCH_UnableToReceivePacket;

    

    // copy the back data to the caller
    if (rx_data)
        memcpy(rx_data, packet, packet[1] + 2);
    return TTWATCH_NoError;
}

void read_from_pcapfile(char *filename) {
    struct pcap_pkthdr header; // The header that pcap gives us 
    u_char *packet; // The actual packet 

    //open the pcap file 
    pcap_t *handle; 
    char errbuf[PCAP_ERRBUF_SIZE]; //not sure what to do with this, oh well 
    handle = pcap_open_offline(filename, errbuf);   //call pcap library function 
 
    if (handle == NULL) { 
        fprintf(stderr,"Couldn't open pcap file %s: %s\n", filename, errbuf); 
        exit(1); 
    }
    
   while ((packet = (u_char *)pcap_next(handle,&header))) { 
        uint8_t size;

        // header contains information about the packet (e.g. timestamp) 
        u_char *pkt_ptr;

        /* check if packet looks like USB comms to watch */
        /* USBPcap in windows captures 91 byte packets, 
         * must check Linux */
        if (header.caplen == 91 &&
            // usbpcap magic bytes
            packet[0] == 0x1b && 
            packet[1] == 0x00 &&
            // watch i/o endpoints:
            (packet[21] == 0x05 || packet[21] == 0x84)) {

            pkt_ptr = &packet[27]; // start of usb data
            size = packet[28] + 2;

            print_packet(pkt_ptr, size);
        }
    } 

    pcap_close(handle);  //close the pcap file 

}

void read_from_file (FILE *fp) {
    uint8_t cmdbuf[MAXCMD];
    uint8_t sz = 0;

    uint8_t response[MAXCMD];

    char line[MAXLINECMD];
    char *p;

    while (fgets (line, MAXLINECMD, fp)) 
    {
        p = line;
        sz = 0;
        while (*p && sz < MAXCMD)
        {
            if (*p == ' ') {
                p++;
                continue;
            }
            if (*p == '\n' || *p == '\r') {
                break;
            }

            if (isxdigit(*p) && isxdigit(p[1])) {
                    sscanf(p, "%02x", (unsigned int *)&cmdbuf[sz]);
                    sz++;
                    p += 2;
                } else {
                    break;
                }
        }
        if (sz > 0) 
        {
            if (optsendpackets && cmdbuf[0] == 0x09) 
            {
                print_packet(cmdbuf, sz);
                send_raw_packet(watch, sz, cmdbuf, response);
                print_packet(response, response[1] + 2);
            } else {
                // we're not going to send anything to the watch, just print it
                print_packet(cmdbuf, sz);
            }
        }
    }
}

void read_from_textfile(char *filename) {
    FILE *fp = NULL;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Can't open file %s\n", filename);
        exit(1);
    }

    read_from_file(fp);
    fclose(fp);
}



int main(int argc, char *argv[]) {
    char *textfile = NULL;
    char *pcapfile = NULL;

    int c;

    while ((c = getopt (argc, argv, "srt:p:")) != -1) {
        switch(c) {
            case 'p':
                optpcapfile = 1;
                pcapfile = optarg;
                break;
            case 't':
                opttextfile = 1;
                textfile = optarg;
                break;
            case 'r':
                optpretty = 0;
                break;
            case 's':
                optsendpackets = 1;
                break;
            case '?':
            default:
                fprintf(stderr,
                    "usage: %s [-p <file>] [-s]\n"
                    "\t -p <file>\t-> read USBPcap file and print all watch commands/responses it finds\n"
                    "\t -t <file>\t-> read commands from textfile\n"
                    "\t -r \t\t-> print raw hex (do not prettify output)\n"
                    "\t -s \t\t-> DANGEROUS send raw commands to device\n", argv[0]);
                return 1;
        }
    }

    if(optsendpackets) {
        libusb_init(NULL);

        if (ttwatch_open(0, &watch) != TTWATCH_NoError)
        {
            fprintf(stderr, "Unable to open watch\n");
            return 1;
        }
       
    }

    if (optpcapfile) 
        read_from_pcapfile (pcapfile);
    else if (opttextfile)
        read_from_textfile (textfile);
    // By default read commands from standard input;
    else read_from_file(stdin);
    return 0;
}

