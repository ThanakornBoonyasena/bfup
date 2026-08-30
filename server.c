#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include "protocol.h"

#define PORT 55555
#define MAX_PENDING 5
#define HEADER_SIZE 3

char *weapons[] = {"Rock", "Paper", "Scissors"};
#define NUM_WEAPONS 3

// Server States
enum server_state {
    WAIT_PLAY,
    WAIT_START,
    WAIT_WEAPON,
    WAIT_CONTENT
};

void send_packet(int client_fd, struct bfup_payload *pkt) {
    if (!pkt) return;
    size_t total_len = HEADER_SIZE + pkt->data_len;
    send(client_fd, pkt, total_len, 0);
    free(pkt);
}

struct bfup_payload *recv_packet(int client_fd) {
    uint8_t header[HEADER_SIZE];
    int n = recv(client_fd, header, HEADER_SIZE, MSG_WAITALL);
    if (n != HEADER_SIZE) return NULL;

    uint16_t data_len;
    memcpy(&data_len, header + 1, sizeof(uint16_t));

    struct bfup_payload *pkt = malloc(HEADER_SIZE + data_len);
    if (!pkt) return NULL;
    
    memcpy(pkt, header, HEADER_SIZE);
    if (data_len > 0) {
        n = recv(client_fd, pkt->data, data_len, MSG_WAITALL);
        if (n != data_len) {
            free(pkt);
            return NULL;
        }
    }
    return pkt;
}

int resolve_game(const char *client_weapon, const char *server_weapon) {
    if (strcmp(client_weapon, server_weapon) == 0) return 0; // Draw
    
    if ((strcmp(client_weapon, "Rock") == 0 && strcmp(server_weapon, "Scissors") == 0) ||
        (strcmp(client_weapon, "Paper") == 0 && strcmp(server_weapon, "Rock") == 0) ||
        (strcmp(client_weapon, "Scissors") == 0 && strcmp(server_weapon, "Paper") == 0)) {
        return 1; // Client wins
    }
    return 0; // Server wins
}

void cleanup_target_file(struct target_file *tf) {
    if (tf) {
        if (tf->name) free(tf->name);
        if (tf->dir) free(tf->dir);
        free(tf);
    }
}

void handle_client(int client_fd) {
    enum server_state state = WAIT_PLAY;
    struct bfup_payload *pkt = NULL;
    struct target_file *tf = NULL;
    FILE *file = NULL;

    printf("Starting state machine for client...\n");

    while ((pkt = recv_packet(client_fd)) != NULL) {
        uint8_t type = checkType((uint8_t*)pkt);

        switch (state) {
            case WAIT_PLAY:
                if (type == 0x00) { // Play
                    tf = parsePlay((uint8_t*)pkt);
                    
                    // isBadCondition
                    if (!tf || !tf->name || !tf->dir) {
                        send_packet(client_fd, mkNo("Invalid file parameters."));
                        cleanup_target_file(tf);
                        tf = NULL;
                        // State remains WAIT_PLAY
                    } else {
                        // isGoodCondition
                        send_packet(client_fd, mkRule(weapons, NUM_WEAPONS));
                        state = WAIT_START;
                    }
                }
                break;

            case WAIT_START:
                if (type == 0x02) { // No
                    state = WAIT_PLAY;
                    cleanup_target_file(tf);
                    tf = NULL;
                } else if (type == 0x03) { // Start
                    state = WAIT_WEAPON;
                }
                break;

            case WAIT_WEAPON:
                if (type == 0x04) { // Weapon
                    char *client_weapon = parseWeapon((uint8_t*)pkt);
                    int srv_idx = rand() % NUM_WEAPONS;
                    char *server_weapon = weapons[srv_idx];

                    printf("Match: %s (client) vs %s (server)\n", client_weapon, server_weapon);
                    int client_won = resolve_game(client_weapon, server_weapon);
                    free(client_weapon);

                    if (!client_won) { // Server win or draw -> Client loses
                        send_packet(client_fd, mkResult(0)); // NOOB
                        state = WAIT_PLAY;
                        cleanup_target_file(tf);
                        tf = NULL;
                    } else { // Server lose -> Client wins
                        send_packet(client_fd, mkResult(1)); // BEAT
                        
                        char filepath[512];
                        snprintf(filepath, sizeof(filepath), "%s/%s", tf->dir, tf->name);
                        mkdir(tf->dir, 0777);
                        file = fopen(filepath, "wb");
                        
                        if (!file) {
                            perror("File open failed");
                            state = WAIT_PLAY; // Fallback state if FS fails
                        } else {
                            state = WAIT_CONTENT;
                        }
                    }
                }
                break;

            case WAIT_CONTENT:
                if (type == 0x06) { // Content
                    char *content = parseContent((uint8_t*)pkt);
                    if (file) {
                        fwrite(pkt->data, 1, pkt->data_len, file);
                    }
                    free(content);
                    // State remains WAIT_CONTENT
                } else if (type == 0x07) { // End
                    if (file) {
                        fclose(file);
                        file = NULL;
                        printf("File written successfully.\n");
                    }
                    send_packet(client_fd, mkEmpty(0x08)); // Send Done
                    
                    cleanup_target_file(tf);
                    tf = NULL;
                    state = WAIT_PLAY; // Loop back for next transfer
                }
                break;
        }

        free(pkt);
    }

    // Client disconnected or socket error
    printf("Client disconnected.\n");
    if (file) fclose(file);
    cleanup_target_file(tf);
    close(client_fd);
}

int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    srand(time(NULL));

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PENDING) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("BFUP Server listening on 127.0.0.1:%d...\n", PORT);

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        printf("New connection established.\n");
        handle_client(client_fd);
    }

    return 0;
}
