/* this command will read a .pcap file, parse any watch commands and responses it finds */

#include "libttwatch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libusb-1.0/libusb.h>

#include <pcap.h>

int main(int argc, char *argv[])
{
    //temporary packet buffers 
    struct pcap_pkthdr header; // The header that pcap gives us 
    const u_char *packet; // The actual packet 

    //check command line arguments 
    if (argc != 2) { 
        fprintf(stderr, "Usage: %s <file.pcap>\n", argv[0]); 
        exit(1); 
    }
    //open the pcap file 
    pcap_t *handle; 
    char errbuf[PCAP_ERRBUF_SIZE]; //not sure what to do with this, oh well 
    handle = pcap_open_offline(argv[1], errbuf);   //call pcap library function 
 
    if (handle == NULL) { 
        fprintf(stderr,"Couldn't open pcap file %s: %s\n", argv[1], errbuf); 
        return(2); 
    }
    
   while (packet = pcap_next(handle,&header)) { 
        uint8_t size;

        // header contains information about the packet (e.g. timestamp) 
        const u_char *pkt_ptr;

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

            pretty_print_packet(pkt_ptr, size);

        }
    } 

        //printf("length %i\n", header.caplen);

    pcap_close(handle);  //close the pcap file 

    return 0;
}

