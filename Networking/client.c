#include "sham.h"
#include <poll.h>

void die(const char *s) {
    perror(s);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  File Transfer: %s <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]\n", argv[0]);
        fprintf(stderr, "  Chat Mode:     %s <server_ip> <server_port> --chat [loss_rate]\n", argv[0]);
        exit(1);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int chat_mode = 0;
    const char *input_file = NULL;
    const char *output_file_name = NULL;
    double loss_rate = 0.0;

    if (strcmp(argv[3], "--chat") == 0) {
        chat_mode = 1;
        if (argc > 4) loss_rate = atof(argv[4]);
    } else {
        if (argc < 5) {
            fprintf(stderr, "Missing file arguments for file transfer mode.\n");
            exit(1);
        }
        input_file = argv[3];
        output_file_name = argv[4];
        if (argc > 5) loss_rate = atof(argv[5]);
    }

    // **FIX:** Acknowledge that loss_rate is intentionally unused on the client.
    (void)loss_rate; 

    init_logging("client_log.txt");
    srand(time(NULL));

    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        die("socket creation failed");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_aton(server_ip, &server_addr.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    // --- State Variables ---
    uint32_t seq_num = rand() % 10000;
    uint32_t ack_num = 0;

    // --- Handshake ---
    struct sham_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.seq_num = htonl(seq_num);
    packet.header.flags = htons(SYN);
    sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    log_event("SND SYN SEQ=%u\n", seq_num);
    
    recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
    if ((ntohs(packet.header.flags) & (SYN | ACK)) && ntohl(packet.header.ack_num) == seq_num + 1) {
        log_event("RCV SYN-ACK SEQ=%u ACK=%u\n", ntohl(packet.header.seq_num), ntohl(packet.header.ack_num));
        ack_num = ntohl(packet.header.seq_num) + 1;
        seq_num++;
        
        memset(&packet, 0, sizeof(packet));
        packet.header.seq_num = htonl(seq_num);
        packet.header.ack_num = htonl(ack_num);
        packet.header.flags = htons(ACK);
        sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        log_event("SND ACK FOR SYN\n");
        printf("Connection established.\n");
    } else {
        fprintf(stderr, "Handshake failed.\n");
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
                sendto(sockfd, &packet, sizeof(packet.header) + strlen(buffer) + 1, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                
                if (strcmp(buffer, "/quit") == 0) break;
            }
            if (fds[1].revents & POLLIN) { // Network input
                recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
                printf("Server: %s\n", packet.data);
                if (strcmp(packet.data, "/quit") == 0) break;
            }
        }
    } else {
        // --- FILE TRANSFER MODE ---
        FILE *fp = fopen(input_file, "rb");
        if (!fp) die("fopen input file");

        // Send filename first
        memset(&packet, 0, sizeof(packet));
        packet.header.seq_num = htonl(seq_num);
        strcpy(packet.data, output_file_name);
        sendto(sockfd, &packet, sizeof(packet.header) + strlen(output_file_name) + 1, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        log_event("SND DATA SEQ=%u LEN=%zu\n", seq_num, strlen(output_file_name) + 1);
        
        recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
        log_event("RCV ACK=%u\n", ntohl(packet.header.ack_num));
        seq_num += strlen(output_file_name) + 1;

        // Send file contents
        while(!feof(fp)) {
            memset(&packet, 0, sizeof(packet));
            int bytes_read = fread(packet.data, 1, PAYLOAD_SIZE, fp);
            if (bytes_read <= 0) break;

            packet.header.seq_num = htonl(seq_num);
            
            int sent = 0;
            while(!sent) {
                sendto(sockfd, &packet, sizeof(packet.header) + bytes_read, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                log_event("SND DATA SEQ=%u LEN=%d\n", seq_num, bytes_read);

                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = RTO_MS * 1000;
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                
                int n = recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
                if (n > 0 && (ntohs(packet.header.flags) & ACK) && ntohl(packet.header.ack_num) >= seq_num + bytes_read) {
                    log_event("RCV ACK=%u\n", ntohl(packet.header.ack_num));
                    sent = 1;
                } else {
                    log_event("TIMEOUT SEQ=%u\n", seq_num);
                    log_event("RETX DATA SEQ=%u LEN=%d\n", seq_num, bytes_read);
                }
            }
            seq_num += bytes_read;
        }
        fclose(fp);
        
        // Send FIN
        memset(&packet, 0, sizeof(packet));
        packet.header.seq_num = htonl(seq_num);
        packet.header.flags = htons(FIN);
        sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        log_event("SND FIN SEQ=%u\n", seq_num);
        printf("File transfer complete.\n");
    }

    close(sockfd);
    close_logging();
    return 0;
}// // #include "sham.h"
// #include <poll.h>

// void die(const char *s) {
//     perror(s);
//     exit(1);
// }

// int main(int argc, char *argv[]) {
//     if (argc < 4) {
//         fprintf(stderr, "Usage:\n");
//         fprintf(stderr, "  File Transfer: %s <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]\n", argv[0]);
//         fprintf(stderr, "  Chat Mode:     %s <server_ip> <server_port> --chat [loss_rate]\n", argv[0]);
//         exit(1);
//     }

//     const char *server_ip = argv[1];
//     int port = atoi(argv[2]);
//     int chat_mode = 0;
//     const char *input_file = NULL;
//     const char *output_file_name = NULL;
//     double loss_rate = 0.0;

//     if (strcmp(argv[3], "--chat") == 0) {
//         chat_mode = 1;
//         if (argc > 4) loss_rate = atof(argv[4]);
//     } else {
//         if (argc < 5) {
//             fprintf(stderr, "Missing file arguments for file transfer mode.\n");
//             exit(1);
//         }
//         input_file = argv[3];
//         output_file_name = argv[4];
//         if (argc > 5) loss_rate = atof(argv[5]);
//     }

//     // **FIX:** Acknowledge that loss_rate is intentionally unused on the client.
//     (void)loss_rate; 

//     init_logging("client_log.txt");
//     srand(time(NULL));

//     int sockfd;
//     struct sockaddr_in server_addr;

//     if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
//         die("socket creation failed");
//     }

//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(port);
//     if (inet_aton(server_ip, &server_addr.sin_addr) == 0) {
//         fprintf(stderr, "inet_aton() failed\n");
//         exit(1);
//     }

//     // --- State Variables ---
//     uint32_t seq_num = rand() % 10000;
//     uint32_t ack_num = 0;

//     // --- Handshake ---
//     struct sham_packet packet;
//     memset(&packet, 0, sizeof(packet));
//     packet.header.seq_num = htonl(seq_num);
//     packet.header.flags = htons(SYN);
//     sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
//     log_event("SND SYN SEQ=%u\n", seq_num);
    
//     recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
//     if ((ntohs(packet.header.flags) & (SYN | ACK)) && ntohl(packet.header.ack_num) == seq_num + 1) {
//         log_event("RCV SYN-ACK SEQ=%u ACK=%u\n", ntohl(packet.header.seq_num), ntohl(packet.header.ack_num));
//         ack_num = ntohl(packet.header.seq_num) + 1;
//         seq_num++;
        
//         memset(&packet, 0, sizeof(packet));
//         packet.header.seq_num = htonl(seq_num);
//         packet.header.ack_num = htonl(ack_num);
//         packet.header.flags = htons(ACK);
//         sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
//         log_event("SND ACK FOR SYN\n");
//         printf("Connection established.\n");
//     } else {
//         fprintf(stderr, "Handshake failed.\n");
//         exit(1);
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
                
//                 memset(&packet, 0, sizeof(packet));
//                 strcpy(packet.data, buffer);
//                 sendto(sockfd, &packet, sizeof(packet.header) + strlen(buffer) + 1, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                
//                 if (strcmp(buffer, "/quit") == 0) break;
//             }
//             if (fds[1].revents & POLLIN) { // Network input
//                 recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
//                 printf("Server: %s\n", packet.data);
//                 if (strcmp(packet.data, "/quit") == 0) break;
//             }
//         }
//     } else {
//         // --- FILE TRANSFER MODE ---
//         FILE *fp = fopen(input_file, "rb");
//         if (!fp) die("fopen input file");

//         // Send filename first
//         memset(&packet, 0, sizeof(packet));
//         packet.header.seq_num = htonl(seq_num);
//         strcpy(packet.data, output_file_name);
//         sendto(sockfd, &packet, sizeof(packet.header) + strlen(output_file_name) + 1, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
//         log_event("SND DATA SEQ=%u LEN=%zu\n", seq_num, strlen(output_file_name) + 1);
        
//         recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
//         log_event("RCV ACK=%u\n", ntohl(packet.header.ack_num));
//         seq_num += strlen(output_file_name) + 1;

//         // Send file contents
//         while(!feof(fp)) {
//             memset(&packet, 0, sizeof(packet));
//             int bytes_read = fread(packet.data, 1, PAYLOAD_SIZE, fp);
//             if (bytes_read <= 0) break;

//             packet.header.seq_num = htonl(seq_num);
            
//             int sent = 0;
//             while(!sent) {
//                 sendto(sockfd, &packet, sizeof(packet.header) + bytes_read, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
//                 log_event("SND DATA SEQ=%u LEN=%d\n", seq_num, bytes_read);

//                 struct timeval tv;
//                 tv.tv_sec = 0;
//                 tv.tv_usec = RTO_MS * 1000;
//                 setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                
//                 int n = recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL);
//                 if (n > 0 && (ntohs(packet.header.flags) & ACK) && ntohl(packet.header.ack_num) >= seq_num + bytes_read) {
//                     log_event("RCV ACK=%u\n", ntohl(packet.header.ack_num));
//                     sent = 1;
//                 } else {
//                     log_event("TIMEOUT SEQ=%u\n", seq_num);
//                     log_event("RETX DATA SEQ=%u LEN=%d\n", seq_num, bytes_read);
//                 }
//             }
//             seq_num += bytes_read;
//         }
//         fclose(fp);
        
//         // Send FIN
//         memset(&packet, 0, sizeof(packet));
//         packet.header.seq_num = htonl(seq_num);
//         packet.header.flags = htons(FIN);
//         sendto(sockfd, &packet, sizeof(packet.header), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
//         log_event("SND FIN SEQ=%u\n", seq_num);
//         printf("File transfer complete.\n");
//     }

//     close(sockfd);
//     close_logging();
//     return 0;
// }