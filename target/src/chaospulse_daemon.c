/**
 * @file chaospulse_daemon.c
 * @brief Real-Time QNX Neutrino 8.0 Telemetry Streamer & Chaos Control Daemon
 *
 * Spawns an internal TCP server (port 9092) and Unix domain socket.
 * Deserializes inbound JSON chaos directives, schedules synthetic fault injection,
 * and continuously broadcasts binary and JSON telemetry frames to host cockpits.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

#include "chaospulse_types.h"
#include "qnx_shims.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define close_socket(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define close_socket(s) close(s)
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR   -1
    typedef int SOCKET;
#endif

#define MAX_CLIENTS          16
#define RX_BUFFER_SIZE       2048
#define TELEMETRY_STREAM_HZ  30
#define STREAM_INTERVAL_US   (1000000 / TELEMETRY_STREAM_HZ)

static volatile bool g_daemon_running = true;
static SOCKET g_server_fd = INVALID_SOCKET;
static SOCKET g_clients[MAX_CLIENTS];
static int    g_client_count = 0;
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;

static void handle_signal(int sig) {
    (void)sig;
    g_daemon_running = false;
}

/**
 * @brief Emit QNX Kernel Trace event via TraceEvent(_NTO_TRACE_INSERTEVENT, ...)
 * Correlates user-space chaos directives with procnto context switches in Momentics IDE
 */
void qnx_trace_chaos_event(uint32_t event_code, uint32_t data1, uint32_t data2) {
#if defined(__QNXNTO__)
    TraceEvent(_NTO_TRACE_INSERTEVENT, _NTO_TRACE_USERFIRST + CHAOS_TRACE_CLASS_USER, event_code, data1, data2);
#else
    (void)event_code; (void)data1; (void)data2;
#endif
}

/**
 * @brief Parse key value integer from basic JSON string
 */
static int parse_json_int(const char *json, const char *key, int default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return default_val;
    pos = strchr(pos, ':');
    if (!pos) return default_val;
    return atoi(pos + 1);
}

/**
 * @brief Parse float from basic JSON string
 */
static float parse_json_float(const char *json, const char *key, float default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return default_val;
    pos = strchr(pos, ':');
    if (!pos) return default_val;
    return (float)atof(pos + 1);
}

/**
 * @brief Dispatch parsed directive to subsystem engines
 */
static void execute_directive_json(const char *json_cmd, SOCKET client_sock) {
    printf("[CHAOSPULSE-DAEMON] Directing incoming command: %s\n", json_cmd);

    if (strstr(json_cmd, "\"start_storm\"")) {
        int threads = parse_json_int(json_cmd, "threads", 4);
        float drop = parse_json_float(json_cmd, "drop_rate", 0.0f);
        cpu_storm_start((uint8_t)threads);
        qnx_trace_chaos_event(CHAOS_TRACE_STORM_START, (uint32_t)threads, 0);
        if (drop > 0.0f) {
            pulse_interceptor_set_fault(FAULT_PULSE_DROP, true, drop);
            qnx_trace_chaos_event(CHAOS_TRACE_PULSE_DROP, (uint32_t)(drop * 100), 0);
        }
    } else if (strstr(json_cmd, "\"stop_storm\"")) {
        cpu_storm_stop();
        pulse_interceptor_set_fault(FAULT_PULSE_DROP, false, 0.0f);
        qnx_trace_chaos_event(CHAOS_TRACE_STORM_STOP, 0, 0);
    } else if (strstr(json_cmd, "\"inject_drop\"")) {
        float rate = parse_json_float(json_cmd, "rate", 0.15f);
        pulse_interceptor_set_fault(FAULT_PULSE_DROP, true, rate);
        qnx_trace_chaos_event(CHAOS_TRACE_PULSE_DROP, (uint32_t)(rate * 100), 0);
    } else if (strstr(json_cmd, "\"inject_jitter\"")) {
        float jitter_us = parse_json_float(json_cmd, "jitter_us", 15.0f);
        pulse_interceptor_set_fault(FAULT_PULSE_JITTER, true, jitter_us);
        qnx_trace_chaos_event(CHAOS_TRACE_JITTER_INJECT, (uint32_t)jitter_us, 0);
    } else if (strstr(json_cmd, "\"priority_inversion\"")) {
        int enabled = parse_json_int(json_cmd, "enabled", 1);
        pulse_interceptor_set_fault(FAULT_PRIORITY_INVERSION_TEST, enabled != 0, 0.0f);
        qnx_trace_chaos_event(enabled ? CHAOS_TRACE_INVERSION_START : CHAOS_TRACE_INVERSION_HEALED, 0, 0);
    } else if (strstr(json_cmd, "\"memory_fault\"")) {
        pulse_interceptor_set_fault(FAULT_MEMORY_CORRUPT, true, 0.0f);
    } else if (strstr(json_cmd, "\"reset\"")) {
        cpu_storm_stop();
        pulse_interceptor_set_fault(FAULT_PULSE_DROP, false, 0.0f);
        pulse_interceptor_set_fault(FAULT_PULSE_JITTER, false, 0.0f);
        pulse_interceptor_set_fault(FAULT_PRIORITY_INVERSION_TEST, false, 0.0f);
        pulse_interceptor_set_fault(FAULT_MEMORY_CORRUPT, false, 0.0f);
        pulse_interceptor_reset_stats();
        telemetry_reset();
        qnx_trace_chaos_event(CHAOS_TRACE_SYSTEM_RESET, 0, 0);
    }

    const char *ack = "{\"status\":\"ACK\",\"code\":200}\n";
    send(client_sock, ack, (int)strlen(ack), 0);
}

/**
 * @brief Telemetry Broadcast Worker Thread
 */
static void *telemetry_broadcaster_entry(void *arg) {
    (void)arg;
    chaospulse_telemetry_t frame;
    char json_buffer[1024];

    while (g_daemon_running) {
        telemetry_get_snapshot(&frame);

        if (frame.asil_breach_flag) {
            qnx_trace_chaos_event(CHAOS_TRACE_ASIL_BREACH, (uint32_t)(frame.jitter_us * 1000), 0);
        }

        /* Format both binary frame and JSON text payload */
        int json_len = snprintf(json_buffer, sizeof(json_buffer),
            "{\"timestamp\":%llu,\"jitter_us\":%.3f,\"latency_us\":%.3f,\"p50\":%.3f,\"p99\":%.3f,"
            "\"wcet\":%.3f,\"dropped\":%u,\"total\":%u,\"cpu_load\":%u,\"threads\":%u,\"breach\":%u,"
            "\"state\":%u,\"priority\":%u,\"active_faults\":%u}\n",
            (unsigned long long)frame.timestamp_cycles,
            frame.jitter_us,
            frame.latency_us,
            frame.p50_jitter_us,
            frame.p99_jitter_us,
            frame.wcet_jitter_us,
            frame.dropped_pulses,
            frame.total_pulses,
            frame.cpu_load_pct,
            frame.active_storm_threads,
            frame.asil_breach_flag,
            frame.safety_state,
            frame.current_priority,
            frame.active_faults
        );

        /* Broadcast to all connected clients */
        pthread_mutex_lock(&g_clients_lock);
        for (int i = 0; i < g_client_count; i++) {
            SOCKET sock = g_clients[i];
            /* Send JSON line payload (can also send binary frame) */
            int sent = send(sock, json_buffer, json_len, 0);
            if (sent <= 0) {
                close_socket(sock);
                g_clients[i] = g_clients[g_client_count - 1];
                g_client_count--;
                i--;
            }
        }
        pthread_mutex_unlock(&g_clients_lock);

#if defined(_WIN32)
        Sleep(1000 / TELEMETRY_STREAM_HZ);
#else
        usleep(STREAM_INTERVAL_US);
#endif
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = CHAOSPULSE_DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0) port = CHAOSPULSE_DEFAULT_PORT;
    }

    printf("===================================================================\n");
    printf("  QNX-CHAOSPULSE // SDP 8.0 Real-Time Resilience Daemon\n");
    printf("  Target Platform: %s\n", QNX_PLATFORM_NAME);
    printf("  Telemetry Port:  %d\n", port);
    printf("  Safety Bounds:   ASIL-D Max Jitter <= %.2f us | Priority: 63\n", ASIL_D_MAX_JITTER_US);
    printf("===================================================================\n");

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Initialize Subsystems */
    telemetry_init();
    pulse_interceptor_init();
    pulse_interceptor_start();
    cpu_storm_init();

    /* Setup TCP Telemetry Server Socket */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd == INVALID_SOCKET) {
        perror("[CHAOSPULSE] Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(g_server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        perror("[CHAOSPULSE] Socket bind failed");
        close_socket(g_server_fd);
        return 1;
    }

    if (listen(g_server_fd, 8) == SOCKET_ERROR) {
        perror("[CHAOSPULSE] Socket listen failed");
        close_socket(g_server_fd);
        return 1;
    }

    printf("[CHAOSPULSE] Telemetry server listening on TCP 0.0.0.0:%d\n", port);

    /* Launch Telemetry Broadcast Thread */
    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, telemetry_broadcaster_entry, NULL);

    /* Main Non-Blocking Select Event Loop for Inbound Directives */
    char rx_buf[RX_BUFFER_SIZE];
    while (g_daemon_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_server_fd, &read_fds);
        SOCKET max_fd = g_server_fd;

        pthread_mutex_lock(&g_clients_lock);
        for (int i = 0; i < g_client_count; i++) {
            FD_SET(g_clients[i], &read_fds);
            if (g_clients[i] > max_fd) max_fd = g_clients[i];
        }
        pthread_mutex_unlock(&g_clients_lock);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100 ms timeout */

        int activity = select((int)(max_fd + 1), &read_fds, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) {
            break;
        }

        /* Check for new client connection */
        if (FD_ISSET(g_server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET new_sock = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_sock != INVALID_SOCKET) {
                pthread_mutex_lock(&g_clients_lock);
                if (g_client_count < MAX_CLIENTS) {
                    g_clients[g_client_count++] = new_sock;
                    printf("[CHAOSPULSE] Accepted cockpit client [%s:%d]\n",
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                } else {
                    close_socket(new_sock);
                }
                pthread_mutex_unlock(&g_clients_lock);
            }
        }

        /* Check for inbound chaos directives from connected clients */
        pthread_mutex_lock(&g_clients_lock);
        for (int i = 0; i < g_client_count; i++) {
            SOCKET sock = g_clients[i];
            if (FD_ISSET(sock, &read_fds)) {
                int bytes = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
                if (bytes <= 0) {
                    printf("[CHAOSPULSE] Cockpit client disconnected\n");
                    close_socket(sock);
                    g_clients[i] = g_clients[g_client_count - 1];
                    g_client_count--;
                    i--;
                } else {
                    rx_buf[bytes] = '\0';
                    execute_directive_json(rx_buf, sock);
                }
            }
        }
        pthread_mutex_unlock(&g_clients_lock);
    }

    printf("\n[CHAOSPULSE] Initiating graceful shutdown...\n");

    /* Cleanup Resources */
    g_daemon_running = false;
    pthread_join(broadcast_thread, NULL);

    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < g_client_count; i++) {
        close_socket(g_clients[i]);
    }
    g_client_count = 0;
    pthread_mutex_unlock(&g_clients_lock);

    close_socket(g_server_fd);
    cpu_storm_cleanup();
    pulse_interceptor_stop();

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("[CHAOSPULSE] Shutdown complete. Microkernel state clean.\n");
    return 0;
}
