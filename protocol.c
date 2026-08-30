#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include <sys/stat.h>

uint8_t checkType(uint8_t *buff) {
	struct bfup_payload *p = (struct bfup_payload *) buff;
	return (p->version_n_type & 0x0F);
}

void putInfo(uint8_t **ptr, uint16_t len, const void *data)
{
    memcpy(*ptr, &len, sizeof(len));
    *ptr += sizeof(len);

    memcpy(*ptr, data, len);
    *ptr += len;
}

struct bfup_payload *mkPlay(int fd, char *filename, char *target_dr) {
	struct stat stats;

	if (fstat(fd, &stats) == -1) perror("fstat");

	uint16_t data_len = 
		sizeof(stats.st_size)
		+ strlen(filename)
		+ strlen(target_dr)
		//+ sizeof(stats.st_uid)
		//+ sizeof(stats.st_gid)
		+ (sizeof(uint16_t) * 3); // plus of each length prefix
	struct bfup_payload *payload = (struct bfup_payload *) calloc(1, sizeof(struct bfup_payload) + data_len);

	if (!payload) return NULL;

	payload->version_n_type = (1 << 4); // ts should be 00010000 for version 1 and msg type 0 (play)
	payload->data_len = data_len;
	uint8_t *ptr = payload->data;

	putInfo(&ptr, sizeof(stats.st_size), &stats.st_size);
	putInfo(&ptr, strlen(filename), filename);
	putInfo(&ptr, strlen(target_dr), target_dr);
	//putInfo(&p, sizeof(stats.st_uid), &stats.st_uid);
	//putInfo(&p, sizeof(stats.st_gid), &stats.st_gid);
	return payload;
}

struct target_file* parsePlay(uint8_t *buff) {
	struct bfup_payload *p = (struct bfup_payload *) buff;

	if ((p->version_n_type & 0x0F) != 0x0) return NULL;

	struct target_file *file = (struct target_file *) calloc(1, sizeof(struct target_file));
	if (!file) return NULL;
	uint16_t len;
	uint8_t *data = p->data;

	memcpy(&len, data, sizeof(len));
	data += sizeof(uint16_t);
	memcpy(&file->size, data, len);
	data += len;

	memcpy(&len, data, sizeof(len));
	file->name = malloc(len + 1);
	data += sizeof(uint16_t);
	memcpy(file->name, data, len);
	file->name[len] = '\0';
	data += len;

	memcpy(&len, data, sizeof(len));
	file->dir = malloc(len + 1);
	data += sizeof(uint16_t);
	memcpy(file->dir, data, len);
	file->dir[len] = '\0';
	data += len;

	return file;

}


struct bfup_payload *mkRule(char **rules, uint16_t n) {
	uint16_t data_len = 0;
	for (int i = 0; i < n; i++) {
		data_len += strlen(rules[i]);
	}
	data_len += sizeof(uint16_t) * (n + 1); //first field in data section will tell how many rules are there
	struct bfup_payload *payload = (struct bfup_payload *) calloc(1, sizeof(struct bfup_payload) + data_len);

	if (!payload) return NULL;

	payload->version_n_type = (1 << 4) | 0x01; // ts should be 00010001 for version 1 and msg type 1 (rule)
	payload->data_len = data_len;

	uint8_t *ptr = payload->data;

	memcpy(ptr, &n, sizeof(n));
	ptr += sizeof(n);

	for (int i = 0; i < n; i++) {
		putInfo(&ptr, strlen(rules[i]), rules[i]);
	}
	return payload;
}

struct rule *parseRule(struct bfup_payload *p) {
	//struct bfup_payload *p = (struct bfup_payload *) buff;

	if ((p->version_n_type & 0x0F) != 0x01) return NULL;
	uint8_t *data = p->data;
	uint16_t n;

	memcpy(&n, data, sizeof(n));
	data += sizeof(uint16_t);

	struct rule *rules = (struct rule *) calloc(1, sizeof(struct rule));
	rules->n = n;
	rules->rules = (char **) malloc(sizeof(char *) * n);
	if (!rules->rules) return NULL;

	for (int i = 0; i < n; i++) {
		uint16_t len = 0;
		memcpy(&len, data, sizeof(len));

		rules->rules[i] = malloc((sizeof(char) * len) + 1);

		data += sizeof(uint16_t);
		memcpy(rules->rules[i], data, len);
		rules->rules[i][len] = '\0';
		data += len;
	}
	return rules;

}
