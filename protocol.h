#include <stdint.h>

#ifndef PROTOCOL_H
#define PROTOCOL_H

struct __attribute__((packed)) bfup_payload {
	uint8_t version_n_type; // first 4 bytes for version and the rest for msg type
	uint16_t data_len;
	uint8_t data[];
};

struct target_file {
	char *name;
	uint16_t size;
	char *dir;
};

struct rule {
	uint16_t n;
	char **rules;
};

void putInfo(uint8_t **ptr, uint16_t len, const void *data);
uint8_t checkType(uint8_t * buff);

struct bfup_payload *mkPlay(int fd, char* filename, char *target_dr);
struct bfup_payload *mkRule(char **rules, uint16_t n);
struct bfup_payload *mkEmpty(uint8_t type); // this shit doesnt really do anything just set the type field for START, END, DONE
struct bfup_payload *mkNo(char *msg);
struct bfup_payload *mkWeapon(char *weapon);
struct bfup_payload *mkResult(uint8_t result);
struct bfup_payload *mkContent(char *content);

struct target_file *parsePlay(uint8_t *buff);
struct rule *parseRule(uint8_t *buff);
char *parseWeapon(uint8_t *buff);
char *parseResult(uint8_t *buff);
#endif
