#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include <sys/stat.h>

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
	uint8_t *p = payload->data;

	putInfo(&p, sizeof(stats.st_size), &stats.st_size);
	putInfo(&p, strlen(filename), filename);
	putInfo(&p, strlen(target_dr), target_dr);
	//putInfo(&p, sizeof(stats.st_uid), &stats.st_uid);
	//putInfo(&p, sizeof(stats.st_gid), &stats.st_gid);
	return payload;
}

struct target_file* parsePlay(struct bfup_payload *p) {
	if ((p->version_n_type & 0x0F) != 0x0) return NULL;

	struct target_file *file = (struct target_file *) calloc(1, sizeof(struct target_file));
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
