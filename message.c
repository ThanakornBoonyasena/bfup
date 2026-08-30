#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdint.h>

#include "protocol.h"

#define BUFFER_SIZE 1024
#define MAX_FILE_SIZE 65535

typedef enum {
    TYPE_PLAY   = 0x00,
    TYPE_RULE   = 0x01,
    TYPE_NO     = 0x02,
    TYPE_START  = 0x03,
    TYPE_WEAPON = 0x04,
    TYPE_RESULT = 0x05,
    TYPE_CONTENT= 0x06,
    TYPE_END    = 0x07,
    TYPE_DONE   = 0x08
} MsgType;

typedef enum {
    STATE_WAITING_FOR_CALL,
    STATE_WAIT_FOR_RULE,
    STATE_WAIT_FOR_RESULT,
    STATE_WAIT_FOR_DONE
} ClientState;

int send_all(int sock_fd, const uint8_t *buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sock_fd, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return 0;
}

ssize_t get_file_size_stat(const char *filepath) {
    struct stat st;
    
    // เรียก stat เพื่อดึง metadata ของไฟล์
    if (stat(filepath, &st) == 0) {
        return (size_t)st.st_size; // ขนาดไฟล์หน่วยเป็น Bytes
    }
    
    return -1; // เกิดข้อผิดพลาด เช่น ไม่พบไฟล์
}

int send_packet(int sock_fd, struct bfup_payload *packet)
{
    if (packet == NULL) {
        return -1;
    }

    size_t packet_len =
        sizeof(struct bfup_payload) + packet->data_len;

    int result = send_all(
        sock_fd,
        (uint8_t *)packet,
        packet_len
    );

    free(packet);

    return result;
}

// รับ argument เข้ามาเป็น File_path, Destination Directory
/*
argv[0] = File, 
argv[1] = Destination Directory, 
*/
int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("Usage: %s <filepath> <dir>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    printf("Selected file %s\n", filepath);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("Can't open file");
        return 1;
    }

    ssize_t file_size = get_file_size_stat(filepath);
    if (file_size < 0) {
        printf("Cannot get file size.\n");
        return 1;
    }
    if ((size_t)file_size > MAX_FILE_SIZE) {
        printf("File too large. Maximum is 64 KB.\n");
        return 1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in serv_addr;
    // แค่ clear หน่วยความจำของ struct serv_addr
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(55555);

    // แปลง IP ปลายทางของ server เป็น Binary
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sock_fd);
        return 1;
    }
    printf("[TCP] Connected to server.\n");

    ClientState state = STATE_WAITING_FOR_CALL;
    uint8_t recv_buf[BUFFER_SIZE];

    while (1) {
        switch (state) {

            // STATE: WAITING FOR CALL
            // Action: ส่ง PLAY พร้อม metadata แล้วขยับไปรอ RULE
            case STATE_WAITING_FOR_CALL: {
                printf("\n--- [STATE: WAITING_FOR_CALL] ---\n");
                
                struct bfup_payload *play;


                play = mkPlay(
                    fileno(fp),
                    (char *)filepath,
                    argv[2] // 3
                );

                printf("\n--- [STATE: WAITING_FOR_CALL] ---\n");


                if (play == NULL) {
                    printf("[Error] mkPlay failed.\n");
                    break;
                }

                if (send_packet(sock_fd, play) < 0) {
                    perror("send PLAY");
                    break;
                }

                state = STATE_WAIT_FOR_RULE;
                break;
            }

             // STATE: WAIT FOR RULE
             // Action: รอรับ RULE หรือ NO จาก Server

            case STATE_WAIT_FOR_RULE: {
                printf("\n--- [STATE: WAIT_FOR_RULE] ---\n");
                    
                ssize_t n = recv(
                    sock_fd,
                    recv_buf,
                    1024,
                    0
                );

                printf("%d\n", n);
                exit(1);

                uint16_t expected_len = getPacketLength(recv_buf);
                uint16_t total = n;

                printf("%d %d \n", expected_len, total);

                while (total < expected_len) {
                    ssize_t nn = recv(
                        sock_fd,
                        recv_buf + total,
                        expected_len - total,
                        0
                    );
                    printf("%d\n", nn);

                    if (n == 0) {
                        // Server closed the connection
                        printf("Server disconnected.\n");
                        break;
                    }

                    if (n < 0) {
                        perror("recv");
                        break;
                    }

                    total += nn;
                    printf("%d\n", total);
                }

                // เช็ค type ที่ server response กลับมา
                // printf("%ld\n", strlen(recv_buf));
                MsgType type = checkType(recv_buf);

                if (type == TYPE_NO) {
                    
                    char *err_msg = parseNo(recv_buf);
                    printf("<- Received NO: %s\n", err_msg);
                    free(err_msg);
                    
                    // วนกลับไปที่ State เริ่มต้น
                    state = STATE_WAITING_FOR_CALL;
                } 
                else if (type == TYPE_RULE) {

                    struct rule *rules = parseRule(recv_buf);
                    if (rules == NULL) {
                        printf("Invalid RULE packet\n");
                        break;
                    }

                    printf("<- RULE received:\n");

                    for (int i = 0; i < rules->n; i++) {
                        printf("  %d. %s\n", i + 1, rules->rules[i]);
                    }
                    free(rules->rules);
                    free(rules);

                    // client เลือกว่าจะเล่น หรือ ไม่เล่น
                    printf("Do you want to play a game? (y or n): ");

                    int want_to_play = 1;
                    while (1) {
                        char choice;
                        scanf(" %c", &choice);

                        choice = tolower(choice);

                        if (choice == 'n') {
                            struct bfup_payload *no_packet = mkNo("I don't want to play with you.");

                            send_packet(sock_fd, no_packet);
                            want_to_play = 0;
                            break;
                        }
                        else if (choice == 'y') {
                            break;
                        }
                        else {
                            printf("Please enter y or n: ");
                        }
                    }

                    if (!want_to_play) {
                        state = STATE_WAITING_FOR_CALL;
                        break; // ออกจาก case ทันที ไม่ไปส่ง START/WEAPON ต่อ
                    }

                    // Client ตกลงจะเล่น -> ส่ง START แล้วตามด้วย WEAPON
                    printf("-> Sending START...\n");
                    struct bfup_payload *start = mkEmpty(TYPE_START);
                    send_packet(sock_fd, start);

                    { int c; while ((c = getchar()) != '\n' && c != EOF); }

                    // เลือกอาวุธตาม Rule ที่กำหนดนะจ้ะ เบบี๋
                    printf("Enter your weapon: ");
                    char my_weapon[32];
                    fgets(my_weapon, sizeof(my_weapon), stdin);
                    printf("Sending WEAPON: %s\n", my_weapon);
                    
                    // เรียกใช้ method mkWeapon
                    struct bfup_payload *weapon;
                    my_weapon[strcspn(my_weapon, "\n")] = '\0';
                    weapon = mkWeapon(my_weapon);

                    send_packet(sock_fd, weapon);
                    state = STATE_WAIT_FOR_RESULT;
                } 
                else {
                    printf("[Warning] Unexpected packet type: %d\n", type);
                }
                break;
            }

             // STATE: WAIT FOR RESULT
             // Action: รอผลเป่ายิงฉุบ (BEAT หรือ NOOB)
             
            case STATE_WAIT_FOR_RESULT: {
                printf("\n--- [STATE: WAIT_FOR_RESULT] ---\n");
                ssize_t bytes_recv = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
                if (bytes_recv <= 0) {
                    state = STATE_WAITING_FOR_CALL;
                    break;
                }

                MsgType type = checkType(recv_buf);

                if (type == TYPE_RESULT) {

                    // เรียกใช้ method parseResult
                    char *result_str = parseResult(recv_buf);
                    if (result_str == NULL) {
                        printf("Invalid RESULT packet\n");
                        break;
                    }
                    printf("Received RESULT: %s\n", result_str);

                    // ไว้ทวนความจำตัวเอง: strcmp คือ การเปรียบเทียบ string 2 ตัวว่าเหมือนกันมั้ย
                    if (strcmp(result_str, "NOOB") == 0) {
                        printf("[!] We LOST (NOOB) -> Returning to WAITING_FOR_CALL\n");
                        state = STATE_WAITING_FOR_CALL;
                    } 
                    else if (strcmp(result_str, "BEAT") == 0) {
                        printf("[!] We WON (BEAT) -> Sending CONTENT and END...\n");

                        rewind(fp);

                        // ส่ง content
                        char content[65536 + 1];
                        size_t n = fread(content, 1, 65536, fp);

                        content[n] = '\0';
                        struct bfup_payload *content_packet = mkContent(content);

                        send_packet(sock_fd, content_packet);

                        // ส่ง END
                        printf("-> Sending END...\n");
                        struct bfup_payload *end = mkEmpty(TYPE_END);
                        send_packet(sock_fd, end);

                        state = STATE_WAIT_FOR_DONE;
                    }
                    free(result_str);
                } 
                else {
                    printf("[Warning] Expected TYPE_RESULT but got: %d\n", type);
                }
                break;
            }

             // STATE: WAIT FOR DONE
             // Action: รอการยืนยัน DONE จาก Server

            case STATE_WAIT_FOR_DONE: {
                printf("\n--- [STATE: WAIT_FOR_DONE] ---\n");
                ssize_t bytes_recv = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
                if (bytes_recv <= 0) {
                    state = STATE_WAITING_FOR_CALL;
                    break;
                }

                MsgType type = checkType(recv_buf);

                if (type == TYPE_DONE) {
                    printf("<- Received DONE! File transfer completed successfully.\n");
                    
                    // วนกลับไปรอส่งไฟล์ใหม่
                    state = STATE_WAITING_FOR_CALL; 
                } 
                else {
                    printf("[Warning] Expected TYPE_DONE but got: %d\n", type);
                }
                break;
            }

            default:
                state = STATE_WAITING_FOR_CALL;
                break;
        }
    }

    fclose(fp);
    printf("[TCP] Client finished and socket closed.\n");
    return 0;
}