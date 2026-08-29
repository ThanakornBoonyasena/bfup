#include <stdint.h>

#ifndef PROTOCOL_H
#define PROTOCOL_H

struct bfup_header {
	uint4_t version;
	uint4_t type;
	uint8_t *data;
}

#endif

