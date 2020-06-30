/**
 *
 * Very simple "bit-banging" library for the following ATT packet types:
 *   att_read: send BT_ATT_OP_READ_REQ, await BT_ATT_OP_READ_RSP)
 *   att_write and att_wrreq: send BT_ADD_OP_WRITE_CMD,
 *                         or send BT_ADD_OP_WRITE_REQ and await BT_ADD_OP_WRITE_RSP
 *   att_read_not: await BT_ATT_OP_HANDLE_VAL_NOT)
 */


#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <bluetooth/bluetooth.h>
#include "bbatt.h"

int
att_read(int fd, uint16_t handle, void *buf)
{
    int result;

    struct { uint8_t opcode; uint16_t handle; } __attribute__((packed)) pkt = { BT_ATT_OP_READ_REQ, htobs(handle) };
    result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    struct { uint8_t opcode; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt = {0};
    while (rpkt.opcode != BT_ATT_OP_READ_RSP) {
        result = recv(fd, &rpkt, sizeof rpkt, 0);
        if (result<0)
            return result;
        else if (rpkt.opcode == BT_ATT_OP_ERROR_RSP && result==1+sizeof(struct bt_att_pdu_error_rsp)) {
            struct bt_att_pdu_error_rsp *err = (void *)rpkt.buf;
            fprintf(stderr, "ATT error for opcode 0x%02x, handle 0x%04x: %s\n", err->opcode, btohs(err->handle), att_ecode2str(err->ecode));
            return -2;
        } else if (rpkt.opcode != BT_ATT_OP_READ_RSP) {
            fprintf(stderr, "Expect ATT READ response opcode (0x%02x) but received 0x%02x\n", BT_ATT_OP_READ_RSP, rpkt.opcode);
            continue;
        } else {
            int length = result-1;
            memcpy(buf, rpkt.buf, length);
            return length;
        }
    }
    return -2;
}

int
att_write(int fd, uint16_t handle, const void *buf, int length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_CMD;
    pkt.handle = htobs(handle);

    if (sizeof pkt > BT_ATT_DEFAULT_LE_MTU)
        return -1;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    return length;
}

int
att_wrreq(int fd, uint16_t handle, const void *buf, int length)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[length]; } __attribute__((packed)) pkt;
    pkt.opcode = BT_ATT_OP_WRITE_REQ;
    pkt.handle = htobs(handle);

    if (sizeof pkt > BT_ATT_DEFAULT_LE_MTU)
        return -1;
    memcpy(pkt.buf, buf, length);

    int result = send(fd, &pkt, sizeof(pkt), 0);
    if (result<0)
        return result;

    struct { uint8_t opcode; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt = {0};
    result = recv(fd, &rpkt, sizeof rpkt, 0);
    if (result < 0)
        return result;
    else if (rpkt.opcode == BT_ATT_OP_ERROR_RSP && result==1+sizeof(struct bt_att_pdu_error_rsp)) {
        struct bt_att_pdu_error_rsp *err = (void *)rpkt.buf;
        fprintf(stderr, "ATT error for opcode 0x%02x, handle 0x%04x: %s\n", err->opcode, btohs(err->handle), att_ecode2str(err->ecode));
        return -2;
    } else if (rpkt.opcode != BT_ATT_OP_WRITE_RSP) {
        fprintf(stderr, "Expected ATT WRITE response opcode (0x%02x) but received 0x%02x\n", BT_ATT_OP_WRITE_RSP, rpkt.opcode);
        return -2;
    }

    return length;
}

int
att_read_not(int fd, uint16_t *handle, void *buf)
{
    struct { uint8_t opcode; uint16_t handle; uint8_t buf[BT_ATT_DEFAULT_LE_MTU]; } __attribute__((packed)) rpkt;
    int result = recv(fd, &rpkt, sizeof rpkt, 0);

    if (result<0)
        return result;
    else if (rpkt.opcode == BT_ATT_OP_ERROR_RSP && result==1+sizeof(struct bt_att_pdu_error_rsp)) {
        struct bt_att_pdu_error_rsp *err = (void *)rpkt.buf;
        fprintf(stderr, "ATT error for opcode 0x%02x, handle 0x%04x: %s\n", err->opcode, btohs(err->handle), att_ecode2str(err->ecode));
        return -2;
    } else if (rpkt.opcode != BT_ATT_OP_HANDLE_VAL_NOT) {
        fprintf(stderr, "Expect ATT NOTIFY opcode (0x%02x) but received 0x%02x\n", BT_ATT_OP_HANDLE_VAL_NOT, rpkt.opcode);
        return -2;
    } else {
        int length = result-3;
        *handle = htobs(rpkt.handle);
        memcpy(buf, rpkt.buf, length);
        return length;
    }
}

const char *
addr_type_name(int dst_type) {
    switch (dst_type) {
    case BDADDR_BREDR: return "BDADDR_BREDR";
    case BDADDR_LE_PUBLIC: return "BDADDR_LE_PUBLIC";
    case BDADDR_LE_RANDOM: return "BDADDR_LE_RANDOM";
    default: return NULL;
    }
}

/**
 * This att_ecode2str function is copied from bluez/attrib/att.c
 *
 */

/* Error codes for Error response PDU */
#define ATT_ECODE_INVALID_HANDLE		0x01
#define ATT_ECODE_READ_NOT_PERM			0x02
#define ATT_ECODE_WRITE_NOT_PERM		0x03
#define ATT_ECODE_INVALID_PDU			0x04
#define ATT_ECODE_AUTHENTICATION		0x05
#define ATT_ECODE_REQ_NOT_SUPP			0x06
#define ATT_ECODE_INVALID_OFFSET		0x07
#define ATT_ECODE_AUTHORIZATION			0x08
#define ATT_ECODE_PREP_QUEUE_FULL		0x09
#define ATT_ECODE_ATTR_NOT_FOUND		0x0A
#define ATT_ECODE_ATTR_NOT_LONG			0x0B
#define ATT_ECODE_INSUFF_ENCR_KEY_SIZE		0x0C
#define ATT_ECODE_INVAL_ATTR_VALUE_LEN		0x0D
#define ATT_ECODE_UNLIKELY			0x0E
#define ATT_ECODE_INSUFF_ENC			0x0F
#define ATT_ECODE_UNSUPP_GRP_TYPE		0x10
#define ATT_ECODE_INSUFF_RESOURCES		0x11
/* Application error */
#define ATT_ECODE_IO				0x80
#define ATT_ECODE_TIMEOUT			0x81
#define ATT_ECODE_ABORTED			0x82

const char *att_ecode2str(uint8_t status)
{
	switch (status)  {
	case ATT_ECODE_INVALID_HANDLE:
		return "Invalid handle";
	case ATT_ECODE_READ_NOT_PERM:
		return "Attribute can't be read";
	case ATT_ECODE_WRITE_NOT_PERM:
		return "Attribute can't be written";
	case ATT_ECODE_INVALID_PDU:
		return "Attribute PDU was invalid";
	case ATT_ECODE_AUTHENTICATION:
		return "Attribute requires authentication before read/write";
	case ATT_ECODE_REQ_NOT_SUPP:
		return "Server doesn't support the request received";
	case ATT_ECODE_INVALID_OFFSET:
		return "Offset past the end of the attribute";
	case ATT_ECODE_AUTHORIZATION:
		return "Attribute requires authorization before read/write";
	case ATT_ECODE_PREP_QUEUE_FULL:
		return "Too many prepare writes have been queued";
	case ATT_ECODE_ATTR_NOT_FOUND:
		return "No attribute found within the given range";
	case ATT_ECODE_ATTR_NOT_LONG:
		return "Attribute can't be read/written using Read Blob Req";
	case ATT_ECODE_INSUFF_ENCR_KEY_SIZE:
		return "Encryption Key Size is insufficient";
	case ATT_ECODE_INVAL_ATTR_VALUE_LEN:
		return "Attribute value length is invalid";
	case ATT_ECODE_UNLIKELY:
		return "Request attribute has encountered an unlikely error";
	case ATT_ECODE_INSUFF_ENC:
		return "Encryption required before read/write";
	case ATT_ECODE_UNSUPP_GRP_TYPE:
		return "Attribute type is not a supported grouping attribute";
	case ATT_ECODE_INSUFF_RESOURCES:
		return "Insufficient Resources to complete the request";
	case ATT_ECODE_IO:
		return "Internal application error: I/O";
	case ATT_ECODE_TIMEOUT:
		return "A timeout occured";
	case ATT_ECODE_ABORTED:
		return "The operation was aborted";
	default:
		return "Unexpected error code";
	}
}
