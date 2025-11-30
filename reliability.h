// reliability.h
#ifndef RELIABILITY_H
#define RELIABILITY_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")

#define REL_MAX_OUTSTANDING 256
#define REL_TIMEOUT_MS 500   // 500 milliseconds timeout
#define REL_MAX_RETRIES 3

typedef struct sockaddr_in SockAddrIn;

typedef void (*rel_connection_failed_cb)(SockAddrIn *peer);

// Initialize reliability with the bound UDP socket (socket already created and bound).
// Returns 0 on success.
int rel_init(SOCKET sock);

// Shutdown reliability layer (stop worker thread, free resources)
void rel_shutdown(void);

// Send a reliable (sequenced) message to dest. The function copies payload so caller may free.
// Returns 0 on success, -1 on failure (e.g., internal allocation error)
int rel_send(SockAddrIn *dest, const char *payload, int payload_len);

// Process an incoming raw UDP datagram. If it's a DATA message, this function will send ACK and
// copy the application payload into out_buf (up to *out_len). Returns 1 if there is app data to deliver,
// 0 if the datagram was an ACK or nothing to deliver, -1 on parse error.
int rel_process_incoming(char *raw_buf, int raw_len, SockAddrIn *from, char *out_buf, int *out_len);

// Set callback invoked when a peer exhausts retries (connection failure)
void rel_set_connection_failed_callback(rel_connection_failed_cb cb);

// Forcibly drop outstanding messages for a peer (optional)
void rel_force_terminate_for_peer(SockAddrIn *peer);

#endif // RELIABILITY_H
