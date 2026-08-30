#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>


#include "protocol.h"

#define MIN_OPTIONS 3
#define MAX_OPTIONS 1000
#define MAX_TARGET_FILE_SIZE (100 * 1024 * 1024) /* 100 MiB */
#define MAX_PATH 4096


typedef struct {
    uint16_t n;
    uint16_t beats[MAX_OPTIONS];
} GameRules;

typedef enum {
    STATE_WAITING_FOR_PLAY,
    STATE_WAITING_FOR_START,
    STATE_WAITING_FOR_WEAPON,
    STATE_WAITING_FOR_CONTENT,
    STATE_DONE,
} ServerState;

enum bfup_msg_type {
    BFUP_PLAY    = 0x00,
    BFUP_RULE    = 0x01,
    BFUP_NO      = 0x02,
    BFUP_START   = 0x03,
    BFUP_WEAPON  = 0x04,
    BFUP_RESULT  = 0x05,
    BFUP_CONTENT = 0x06,
    BFUP_END     = 0x07,
    BFUP_DONE    = 0x08
};

static bool validate_play_payload(const struct target_file *file)
{
    struct stat st;

    // length of name.
    if (strlen(file->name) > 1024) {
    	return false;
    }

    if (file == NULL || file->name == NULL || file->dir == NULL)
        return false;

    /*
     * 1. File size must not exceed the allowed maximum.
     */
    if (file->size > MAX_TARGET_FILE_SIZE)
        return false;

    /*
     * 2. Directory must either:
     *    - already exist and be a directory, or
     *    - be creatable by the server.
     *
     *    If it doesn't exist, walk up to an existing parent and
     *    check whether that parent is writable/searchable.
     */
    if (stat(file->dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode))
            return false;
    } else if (errno == ENOENT) {
        /*
         * The directory doesn't exist.
         * Your server can create it if its parent is accessible.
         */
        char path[PATH_MAX];

        if (strlen(file->dir) >= sizeof(path))
            return false;

        strcpy(path, file->dir);

        while (stat(path, &st) != 0) {
            char *slash = strrchr(path, '/');

            if (slash == NULL)
                return false;

            /*
             * Don't walk past "/".
             */
            if (slash == path) {
                strcpy(path, "/");
                break;
            }

            *slash = '\0';
        }

        if (!S_ISDIR(st.st_mode))
            return false;

        /*
         * The existing parent must be writable and searchable.
         */
        if (access(path, W_OK | X_OK) != 0)
            return false;
    } else {
        return false;
    }

    /*
     * 3. Check for duplicate filename.
     *
     * A duplicate is only a problem if the same filename already
     * exists in the SAME directory.
     */
    char full_path[PATH_MAX];

    int n = snprintf(full_path, sizeof(full_path),
                     "%s/%s", file->dir, file->name);

    if (n < 0 || (size_t)n >= sizeof(full_path))
        return false;

    if (stat(full_path, &st) == 0) {
        /*
         * File already exists in this directory.
         */
        return false;
    }

    if (errno != ENOENT)
        return false;

    return true;
}


int generate_relations(uint16_t *beats, uint16_t n)
{
    if (beats == NULL || n < 2)
        return -1;

    uint16_t *order = malloc(n * sizeof(*order));
    if (order == NULL)
        return -1;

    // Start with:
    // 0, 1, 2, ..., n-1
    for (uint16_t i = 0; i < n; i++)
        order[i] = i;

    // Shuffle the order.
    for (uint16_t i = n - 1; i > 0; i--) {
        uint16_t j = rand() % (i + 1);

        uint16_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    // Create the circular relationship.
    //
    // order[0] beats order[1]
    // order[1] beats order[2]
    // ...
    // order[n-1] beats order[0]
    for (uint16_t i = 0; i < n; i++) {
        beats[order[i]] = order[(i + 1) % n];
    }

    free(order);

    return 0;
}

uint8_t determine_result(const GameRules *rules,
                         const char *server_weapon,
                         const char *client_weapon,
                         char *options[])
{
    if (rules == NULL || server_weapon == NULL || client_weapon == NULL)
        return -1;

    uint16_t server_index = 0;
    uint16_t client_index = 0;

    while (server_index < rules->n &&
           strcmp(options[server_index], server_weapon) != 0)
        server_index++;

    while (client_index < rules->n &&
           strcmp(options[client_index], client_weapon) != 0)
        client_index++;

    if (server_index >= rules->n || client_index >= rules->n)
        return -1;

    if (server_index == client_index)
        return 1;  // client loses

    if (rules->beats[server_index] == client_index)
        return 1;  // server wins

    return 0;      // client wins
}

void add_trailing_slash(char *path, size_t size)
{
    size_t len = strlen(path);

    if (len == 0 || path[len - 1] != '/') {
        if (len + 1 < size) {
            path[len] = '/';
            path[len + 1] = '\0';
        }
    }
}


int main(int argc, char* argv[]) {

	srand((unsigned)time(NULL));



	if (argc < 4 || argc > 1001) {
		fprintf(stderr,
			"Usage: %s option1 option2 ... optionN\n"
			"N must be between 3 and 1000\n",
			argv[0]);
		exit(-1);
	}

	GameRules rules;
	uint16_t n = argc - 1;
	rules.n = n;


	// printf("Server weapon: %s\n", argv[server_weapon + 1]);

	int status;
	struct addrinfo hints;
	struct addrinfo* servinfo;
	const char *PORT = "55555";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, PORT, &hints, &servinfo);
	if (status != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(-1);
	}

	struct addrinfo *p;
	int server_fd;
	for (p = servinfo; p != NULL; p = p->ai_next) {

        server_fd = socket(
            p->ai_family,
            p->ai_socktype,
            p->ai_protocol
        );

        if (server_fd == -1) {
            perror("socket");
            continue;
        }

		int yes = 1;
		setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

        if (bind(
                server_fd,
                p->ai_addr,
                p->ai_addrlen
            ) == -1) {

            perror("bind");
            close(server_fd);
            continue;
        }

        break;
	}

	freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "Could not bind to port %s\n", PORT);
        exit(-1);
    }


	status = listen(server_fd, 10);
	if (status != 0) {
		perror("socket error");
		close(server_fd);
		exit(-1);
	}

	printf("Server listening on port %s...\n", PORT);

	int client_fd;
	struct sockaddr_storage client_addr;
	socklen_t client_addr_size;
	struct bfup_payload* payload;
	uint8_t buf[1024];
	uint8_t bytes_received;
	uint8_t msg_type;
	struct target_file* target_file;
	FILE* file;
	//uint16_t server_weapon;


	while (1) {

		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_fd == -1) {
			perror("accept");
			continue;
		}

		printf("Client connected!\n");	
		ServerState state = STATE_WAITING_FOR_PLAY;
		uint16_t beats[MAX_OPTIONS];

	    if (generate_relations(beats, n) != 0) {
        fprintf(stderr, "Failed to generate relationships\n");
        return 1;
    	}

    	bool connected = true;

    	while (connected) {
    		
    		switch (state) {

				case STATE_WAITING_FOR_PLAY:
				
                	printf("\n--- [STATE: WAITING_FOR_PLAY] ---\n");
					
					bytes_received = recv(client_fd, buf, sizeof(buf), 0);

					if (bytes_received == 0) {
    				// Client closed the connection
    					printf("Client disconnected\n");
    					connected = false;
    					close(client_fd);
					}
					else if (bytes_received < 0) {
    					perror("recv");
					}

					msg_type = checkType(buf);
					if (msg_type == BFUP_PLAY) {
						bool valid = validate_play_payload;
						if (!valid) {
							payload = mkNo("bad condition");
							send(client_fd, (void *)payload, payload->data_len + 3, 0);
						} else {
		    			
		    				target_file = parsePlay(buf);
		    				add_trailing_slash(target_file->dir, 1024);
		    				printf("target_file dir: %s\n", target_file->dir);
		    				printf("target_file_path: %s\n", strcat(target_file->dir, target_file->name));
		    				file = fopen(strcat(target_file->dir, target_file->name), "wb");
		    				printf("target_file size: %d\n", target_file->size);	
		    				generate_relations(rules.beats, rules.n);
		    				payload = mkRule(argv + 1, rules.n);
		    				printf("%d\n", payload->data_len);
							send(client_fd, (void *)payload, payload->data_len + 3, 0);
							state = STATE_WAITING_FOR_START;
						}
					}

					break;

				case STATE_WAITING_FOR_START:


                	printf("\n--- [STATE: WAITING_FOR_START] ---\n");


					bytes_received = recv(client_fd, buf, sizeof(buf), 0);


					if (bytes_received == 0) {
    				// Client closed the connection
    					printf("Client disconnected\n");
    					connected = false;
    					close(client_fd);
					}
					else if (bytes_received < 0) {
    					perror("recv");
					}

					msg_type = checkType(buf);
					if (msg_type == BFUP_NO) {
						state = STATE_WAITING_FOR_PLAY;
					} else if (msg_type == BFUP_START) {
						state = STATE_WAITING_FOR_WEAPON;
					}
					break;

				case STATE_WAITING_FOR_WEAPON:

                	printf("\n--- [STATE: WAITING_FOR_WEAPON] ---\n");


					bytes_received = recv(client_fd, buf, sizeof(buf), 0);

					if (bytes_received == 0) {
    				// Client closed the connection
    					printf("Client disconnected\n");
    					connected = false;
    					close(client_fd);
					}
					else if (bytes_received < 0) {
    					perror("recv");
					}

					msg_type = checkType(buf);
					if ((msg_type == BFUP_WEAPON)) {
						
						uint8_t server_weapon_idx = rand() % rules.n;
						char* server_weapon = argv[server_weapon_idx + 1];
						printf("Server weapon: %s\n", server_weapon);

						char* client_weapon = parseWeapon(buf);
						uint8_t result = determine_result(&rules, server_weapon, client_weapon, argv + 1);
						printf("%d\n", result);
						if (result == 0) {
							payload = mkResult(1);
							send(client_fd, (void *)payload, payload->data_len + 3, 0);
							state = STATE_WAITING_FOR_CONTENT;
						} else if (result == 1) {
							state = STATE_WAITING_FOR_PLAY;
							payload = mkResult(-1);
							send(client_fd, (void *)payload, payload->data_len + 3, 0);
						}
					}
					break;

				case STATE_WAITING_FOR_CONTENT:

                	printf("\n--- [STATE: WAITING_FOR_CONTENT] ---\n");

                	bytes_received = recv(client_fd, buf, sizeof(buf), 0);
					if (bytes_received == 0) {
    				// Client closed the connection
    					printf("Client disconnected\n");
    					connected = false;
    					close(client_fd);
					} else if (bytes_received < 0) {
    					perror("recv");
					}

                	msg_type = checkType(buf);
                	char content_buf[target_file->size];
                	while (1) {
                		bytes_received = recv(client_fd, buf, 1024, 0);
                		msg_type = checkType(buf);
                		if (msg_type == BFUP_END) {
                			break;
                		}

                		strcat(content_buf, buf);
                	}

                	fclose(file);
					break;
    		}
    	}
	}


	close(server_fd);
	
	return 0;
}