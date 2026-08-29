#include <stdint.h>

#ifndef PROTOCOL_H
#define PROTOCOL_H

struct __attribute__((packed)) bfup_payload {
	uint8_t version_n_type; // first 4 bytes for version and the rest for msg type
	uint16_t data_len;
	uint8_t data[];
}

void putInfo(uint8_t **ptr, uint16_t len, const void *data);

bfup_payload *mkPlay(int fd, char *target_dr);
bfup_payload *mkEmpty(uint8_t type); // this shit doesnt really do anything just set the type field for START, END, DONE
bfup_payload *mkRule(enum Rule rules);
bfup_payload *mkNo(char *msg);
bfup_payload *mkWeapon(enum Rule weapon);
bfup_payload *mkResult(uint8_t result);
bfup_payload *mkContent(char *content);

#endif

