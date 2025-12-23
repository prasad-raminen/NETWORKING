#ifndef SHAM_H
#define SHAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <stdarg.h> // **FIX:** Required for va_start, va_end

// Packet constants
#define PAYLOAD_SIZE 1024
#define WINDOW_SIZE 10       // Sender's fixed congestion window size (in packets)
#define RTO_MS 500           // Retransmission Timeout in milliseconds
#define BUFFER_SIZE 65535    // Receiver's buffer size

// S.H.A.M. packet flags
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4

// S.H.A.M. Header Structure
struct sham_header {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t window_size; // Receiver's flow control window
};

// S.H.A.M. Packet Structure (Header + Data)
struct sham_packet {
    struct sham_header header;
    char data[PAYLOAD_SIZE];
};

// --- Logging ---
FILE* log_file = NULL;

void log_event(const char* format, ...) {
    if (log_file == NULL) return;

    char time_buffer[30];
    struct timeval tv;
    time_t curtime;

    gettimeofday(&tv, NULL);
    curtime = tv.tv_sec;

    strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));
    fprintf(log_file, "[%s.%06ld] [LOG] ", time_buffer, tv.tv_usec);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fflush(log_file);
}

void init_logging(const char* filename) {
    if (getenv("RUDP_LOG") != NULL && strcmp(getenv("RUDP_LOG"), "1") == 0) {
        log_file = fopen(filename, "w");
        if (log_file == NULL) {
            perror("fopen log file");
        }
    }
}

void close_logging() {
    if (log_file != NULL) {
        fclose(log_file);
    }
}

#endif // SHAM_H

// #ifndef SHAM_H
// #define SHAM_H

// #include <stdarg.h> // Add this line!
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <stdint.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <sys/time.h>
// #include <unistd.h>
// #include <time.h>
// #include <fcntl.h>
// #include <openssl/md5.h>

// // Packet constants
// #define PAYLOAD_SIZE 1024
// #define WINDOW_SIZE 10       // Sender's fixed congestion window size (in packets)
// #define RTO_MS 500           // Retransmission Timeout in milliseconds
// #define BUFFER_SIZE 65535    // Receiver's buffer size

// // S.H.A.M. packet flags
// #define SYN 0x1
// #define ACK 0x2
// #define FIN 0x4

// // S.H.A.M. Header Structure
// struct sham_header {
//     uint32_t seq_num;
//     uint32_t ack_num;
//     uint16_t flags;
//     uint16_t window_size; // Receiver's flow control window
// };

// // S.H.A.M. Packet Structure (Header + Data)
// struct sham_packet {
//     struct sham_header header;
//     char data[PAYLOAD_SIZE];
// };

// // --- Logging ---
// FILE* log_file = NULL;

// void log_event(const char* format, ...) {
//     if (log_file == NULL) return;

//     char time_buffer[30];
//     struct timeval tv;
//     time_t curtime;

//     gettimeofday(&tv, NULL);
//     curtime = tv.tv_sec;

//     strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));
//     fprintf(log_file, "[%s.%06ld] [LOG] ", time_buffer, tv.tv_usec);

//     va_list args;
//     va_start(args, format);
//     vfprintf(log_file, format, args);
//     va_end(args);

//     fflush(log_file);
// }

// void init_logging(const char* filename) {
//     // if (getenv("RUDP_LOG") != NULL && strcmp(getenv("RUDP_LOG"), "1") == 0) {
//         log_file = fopen(filename, "w");
//         if (log_file == NULL) {
//             perror("fopen log file");
//         }
//     }
// }

// void close_logging() {
//     if (log_file != NULL) {
//         fclose(log_file);
//     }
// }

// #endif // SHAM_H
