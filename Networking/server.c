#include "sham.h"
#include <poll.h>

void die(const char *s) {
    perror(s);
    exit(1);
}

void calculate_md5(const char* filename) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen(filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        printf("%s can't be opened.\n", filename);
        return;
    }

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, 1024, inFile)) != 0) {
        MD5_Update(&mdContext, data, bytes);
    }
    MD5_Final(c, &mdContext);
    
    printf("MD5: ");
    for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
        printf("%02x", c[i]);
    }
    printf("\n");
    fclose(inFile);
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    int chat_mode = 0;
    double loss_rate = 0.0;

    if (argc > 2) {
        if (strcmp(argv[2], "--chat") == 0) {
            chat_mode = 1;
            if (argc > 3) loss_rate = atof(argv[3]);
        } else {
            loss_rate = atof(argv[2]);
        }
    }
    
    init_logging("server_log.txt");
    srand(time(NULL)); // Seed for random loss simulation

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        die("socket creation failed");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        die("bind failed");
    }
    
    printf("Server listening on port %d\n", port);

    // --- State Variables ---
    uint32_t seq_num = rand();
    uint32_t expected_seq_num = 0;

    // --- Handshake ---
    struct sham_packet packet;
    recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);

    if (ntohs(packet.header.flags) & SYN) {
        log_event("RCV SYN SEQ=%u\n", ntohl(packet.header.seq_num));
        expected_seq_num = ntohl(packet.header.seq_num) + 1;

        struct sham_packet syn_ack_packet;
        memset(&syn_ack_packet, 0, sizeof(syn_ack_packet));
        syn_ack_packet.header.seq_num = htonl(seq_num);
        syn_ack_packet.header.ack_num = htonl(expected_seq_num);
        syn_ack_packet.header.flags = htons(SYN | ACK);
        syn_ack_packet.header.window_size = htons(BUFFER_SIZE);
        sendto(sockfd, &syn_ack_packet, sizeof(syn_ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
        log_event("SND SYN-ACK SEQ=%u ACK=%u\n", seq_num, expected_seq_num);

        recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
        if ((ntohs(packet.header.flags) & ACK) && (ntohl(packet.header.ack_num) == seq_num + 1)) {
            log_event("RCV ACK FOR SYN\n");
            printf("Connection established with %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        } else {
            fprintf(stderr, "Handshake failed.\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "Expected SYN packet, got something else.\n");
        exit(1);
    }
    
    if (chat_mode) {
        // --- CHAT MODE ---
        printf("Entering Chat Mode. Type '/quit' to exit.\n");
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = sockfd;
        fds[1].events = POLLIN;

        char buffer[PAYLOAD_SIZE];

        while(1) {
            poll(fds, 2, -1);

            if (fds[0].revents & POLLIN) { // Keyboard input
                fgets(buffer, PAYLOAD_SIZE, stdin);
                buffer[strcspn(buffer, "\n")] = 0;

                memset(&packet, 0, sizeof(packet));
                strcpy(packet.data, buffer);
                sendto(sockfd, &packet, sizeof(packet.header) + strlen(packet.data) + 1, 0, (struct sockaddr*)&client_addr, client_len);
                
                if (strcmp(buffer, "/quit") == 0) break;
            }
            if (fds[1].revents & POLLIN) { // Network input
                // **FIX:** Removed the unused 'n' variable assignment.
                recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
                printf("Client: %s\n", packet.data);
                if (strcmp(packet.data, "/quit") == 0) break;
            }
        }
    } else {
        // --- FILE TRANSFER MODE ---
        FILE* output_file = fopen("received_file.tmp", "wb");
        if (!output_file) die("fopen temp file");
        char output_filename[256] = {0};

        // First data packet is the filename
        recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
        log_event("RCV DATA SEQ=%u LEN=%zu\n", ntohl(packet.header.seq_num), strlen(packet.data) + 1);
        strcpy(output_filename, packet.data);
        printf("Receiving file, will be saved as: %s\n", output_filename);
        
        expected_seq_num = ntohl(packet.header.seq_num) + strlen(packet.data) + 1;
        
        struct sham_packet ack_packet;
        memset(&ack_packet, 0, sizeof(ack_packet));
        ack_packet.header.flags = htons(ACK);
        ack_packet.header.window_size = htons(BUFFER_SIZE);
        ack_packet.header.ack_num = htonl(expected_seq_num);
        sendto(sockfd, &ack_packet, sizeof(ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
        log_event("SND ACK=%u WIN=%u\n", expected_seq_num, BUFFER_SIZE);

        while (1) {
            int n = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
            if (n <= 0) continue;

            if (ntohs(packet.header.flags) & FIN) {
                log_event("RCV FIN SEQ=%u\n", ntohl(packet.header.seq_num));
                break;
            }

            // Simulate packet loss
            if ((double)rand() / RAND_MAX < loss_rate) {
                log_event("DROP DATA SEQ=%u\n", ntohl(packet.header.seq_num));
                continue;
            }

            log_event("RCV DATA SEQ=%u LEN=%d\n", ntohl(packet.header.seq_num), n - (int)sizeof(struct sham_header));
            
            if (ntohl(packet.header.seq_num) == expected_seq_num) {
                int data_len = n - sizeof(struct sham_header);
                fwrite(packet.data, 1, data_len, output_file);
                expected_seq_num += data_len;
            }
            
            ack_packet.header.ack_num = htonl(expected_seq_num);
            sendto(sockfd, &ack_packet, sizeof(ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
            log_event("SND ACK=%u WIN=%u\n", expected_seq_num, BUFFER_SIZE);
        }
        fclose(output_file);
        rename("received_file.tmp", output_filename);

        calculate_md5(output_filename);
    }
    
    close(sockfd);
    close_logging();
    return 0;
}// #include "sham.h"
// #include <poll.h>

// void die(const char *s) {
//     perror(s);
//     exit(1);
// }

// void calculate_md5(const char* filename) {
//     unsigned char c[MD5_DIGEST_LENGTH];
//     int i;
//     FILE *inFile = fopen(filename, "rb");
//     MD5_CTX mdContext;
//     int bytes;
//     unsigned char data[1024];

//     if (inFile == NULL) {
//         printf("%s can't be opened.\n", filename);
//         return;
//     }

//     MD5_Init(&mdContext);
//     while ((bytes = fread(data, 1, 1024, inFile)) != 0) {
//         MD5_Update(&mdContext, data, bytes);
//     }
//     MD5_Final(c, &mdContext);
    
//     printf("MD5: ");
//     for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
//         printf("%02x", c[i]);
//     }
//     printf("\n");
//     fclose(inFile);
// }


// int main(int argc, char *argv[]) {
//     if (argc < 2) {
//         fprintf(stderr, "Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
//         exit(1);
//     }

//     int port = atoi(argv[1]);
//     int chat_mode = 0;
//     double loss_rate = 0.0;

//     if (argc > 2) {
//         if (strcmp(argv[2], "--chat") == 0) {
//             chat_mode = 1;
//             if (argc > 3) loss_rate = atof(argv[3]);
//         } else {
//             loss_rate = atof(argv[2]);
//         }
//     }
    
//     init_logging("server_log.txt");

//     int sockfd;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_len = sizeof(client_addr);

//     if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
//         die("socket creation failed");
//     }

//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(port);

//     if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         die("bind failed");
//     }
    
//     printf("Server listening on port %d\n", port);

//     // --- State Variables ---
//     uint32_t seq_num = rand();
//     uint32_t expected_seq_num = 0;

//     // --- Handshake ---
//     struct sham_packet packet;
//     recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);

//     if (packet.header.flags & SYN) {
//         log_event("RCV SYN SEQ=%u\n", ntohl(packet.header.seq_num));
//         expected_seq_num = ntohl(packet.header.seq_num) + 1;

//         struct sham_packet syn_ack_packet;
//         syn_ack_packet.header.seq_num = htonl(seq_num);
//         syn_ack_packet.header.ack_num = htonl(expected_seq_num);
//         syn_ack_packet.header.flags = htons(SYN | ACK);
//         syn_ack_packet.header.window_size = htons(BUFFER_SIZE);
//         sendto(sockfd, &syn_ack_packet, sizeof(syn_ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
//         log_event("SND SYN-ACK SEQ=%u ACK=%u\n", seq_num, expected_seq_num);

//         recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
//         if ((ntohs(packet.header.flags) & ACK) && (ntohl(packet.header.ack_num) == seq_num + 1)) {
//             log_event("RCV ACK FOR SYN\n");
//             printf("Connection established with %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
//         }
//     }
    
//     if (chat_mode) {
//         // --- CHAT MODE ---
//         printf("Entering Chat Mode. Type '/quit' to exit.\n");
//         struct pollfd fds[2];
//         fds[0].fd = STDIN_FILENO;
//         fds[0].events = POLLIN;
//         fds[1].fd = sockfd;
//         fds[1].events = POLLIN;

//         char buffer[PAYLOAD_SIZE];

//         while(1) {
//             poll(fds, 2, -1);

//             if (fds[0].revents & POLLIN) { // Keyboard input
//                 fgets(buffer, PAYLOAD_SIZE, stdin);
//                 buffer[strcspn(buffer, "\n")] = 0;

//                 packet.header.seq_num = 0; // Sequencing not fully implemented for chat
//                 packet.header.ack_num = 0;
//                 packet.header.flags = 0;
//                 strcpy(packet.data, buffer);
//                 sendto(sockfd, &packet, sizeof(packet.header) + strlen(packet.data) + 1, 0, (struct sockaddr*)&client_addr, client_len);
                
//                 if (strcmp(buffer, "/quit") == 0) {
//                     // Chat termination is simplified for this example
//                     break;
//                 }
//             }
//             if (fds[1].revents & POLLIN) { // Network input
//                 int n = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
//                 printf("Client: %s\n", packet.data);

//                  if (strcmp(packet.data, "/quit") == 0) {
//                     break;
//                 }
//             }
//         }
//     } else {
//         // --- FILE TRANSFER MODE ---
//         FILE* output_file = fopen("received_file", "wb"); // Temp name
//         char output_filename[256] = {0};

//         // First packet is the filename
//         recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
//         strcpy(output_filename, packet.data);
//         printf("Receiving file: %s\n", output_filename);
        
//         expected_seq_num = ntohl(packet.header.seq_num) + strlen(packet.data) + 1;
        
//         struct sham_packet ack_packet;
//         ack_packet.header.flags = htons(ACK);
//         ack_packet.header.window_size = htons(BUFFER_SIZE);
//         ack_packet.header.ack_num = htonl(expected_seq_num);
//         sendto(sockfd, &ack_packet, sizeof(ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
//         log_event("SND ACK=%u WIN=%u\n", expected_seq_num, BUFFER_SIZE);

//         while (1) {
//             int n = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_len);
//             if (ntohs(packet.header.flags) & FIN) {
//                 log_event("RCV FIN SEQ=%u\n", ntohl(packet.header.seq_num));
//                 break;
//             }

//             // Simulate packet loss
//             if ((double)rand() / RAND_MAX < loss_rate) {
//                 log_event("DROP DATA SEQ=%u\n", ntohl(packet.header.seq_num));
//                 continue;
//             }

//             log_event("RCV DATA SEQ=%u LEN=%d\n", ntohl(packet.header.seq_num), n - (int)sizeof(struct sham_header));
            
//             if (ntohl(packet.header.seq_num) == expected_seq_num) {
//                 int data_len = n - sizeof(struct sham_header);
//                 fwrite(packet.data, 1, data_len, output_file);
//                 expected_seq_num += data_len;
//             }
            
//             ack_packet.header.ack_num = htonl(expected_seq_num);
//             sendto(sockfd, &ack_packet, sizeof(ack_packet.header), 0, (struct sockaddr*)&client_addr, client_len);
//             log_event("SND ACK=%u WIN=%u\n", expected_seq_num, BUFFER_SIZE);
//         }
//         fclose(output_file);
//         rename("received_file", output_filename);

//         calculate_md5(output_filename);
//     }
    
//     close(sockfd);
//     close_logging();
//     return 0;
// }
