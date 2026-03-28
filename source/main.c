#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#define CONFIG_FILE "config.ini"
#define MAX_LINE 256

#define DSU_DEFAULT_PORT 26760
#define DSU_PROTOCOL_VERSION 1001
#define DSU_SERVER_ID 1001
#define DSU_MAX_PACKET_SIZE 512

#define EVENT_PROTOCOL_VERSION 0x100000
#define EVENT_CONTROLLER_INFO 0x100001
#define EVENT_CONTROLLER_DATA 0x100002

#define CONTROLLER_SLOT 0
#define MAX_SUBSCRIBERS 8
#define SUBSCRIBER_TIMEOUT_MS 5000

#define LCD_TOGGLE_KEY KEY_SELECT
#define LCD_TOGGLE_HOLD_TIME 5000

#define TOUCH_WIDTH 320
#define TOUCH_HEIGHT 240
#define DS4_TOUCH_MAX_X 1919
#define DS4_TOUCH_MAX_Y 941

#define ACCEL_RAW_TO_G 512.0f
#define GYRO_RAW_TO_DPS 14.375f
#define ACCEL_DEADZONE_G 0.03f
#define GYRO_DEADZONE_DPS 0.70f

typedef struct {
    char targetip[64];
    int port;
    int invertcpady;
    int invertcsticky;
} config;

typedef struct {
    int active;
    struct sockaddr_in addr;
    u8 flags;
    u8 slot;
    u8 mac[6];
    u32 packetnum;
    u64 lastseenms;
} subscriber;

typedef struct {
    u32 keys;
    circlePosition circlepad;
    circlePosition cstick;
    touchPosition touch;
    int touchactive;
    u8 touchid;
    accelVector accel;
    angularRate gyro;
    u64 motiontsus;
} inputstate;

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    char *end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return text;
}

static void write_default_config_file(void) {
    FILE *file = fopen(CONFIG_FILE, "w");
    if (!file) {
        printf("failed to create %s\n", CONFIG_FILE);
        return;
    }

    fprintf(file, "# DSU (Cemuhook) server settings\n");
    fprintf(file, "# targetip=0.0.0.0 means allow all clients\n");
    fprintf(file, "targetip=0.0.0.0\n");
    fprintf(file, "port=%d\n", DSU_DEFAULT_PORT);
    fprintf(file, "invertcpady=0\n");
    fprintf(file, "invertcsticky=0\n");
    fclose(file);

    printf("default %s created\n", CONFIG_FILE);
}

static void readconfigfile(config *cfg) {
    strncpy(cfg->targetip, "0.0.0.0", sizeof(cfg->targetip) - 1);
    cfg->targetip[sizeof(cfg->targetip) - 1] = '\0';
    cfg->port = DSU_DEFAULT_PORT;
    cfg->invertcpady = 0;
    cfg->invertcsticky = 0;

    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) {
        printf("%s not found, using defaults\n", CONFIG_FILE);
        write_default_config_file();
        return;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        char *parsed = trim(line);
        if (*parsed == '\0' || *parsed == '#') {
            continue;
        }

        char *equals = strchr(parsed, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = trim(parsed);
        char *value = trim(equals + 1);

        if (strcmp(key, "targetip") == 0 || strcmp(key, "serverip") == 0) {
            strncpy(cfg->targetip, value, sizeof(cfg->targetip) - 1);
            cfg->targetip[sizeof(cfg->targetip) - 1] = '\0';
        } else if (strcmp(key, "port") == 0) {
            int port = atoi(value);
            if (port > 0 && port <= 65535) {
                cfg->port = port;
            }
        } else if (strcmp(key, "invertcpady") == 0) {
            cfg->invertcpady = atoi(value) ? 1 : 0;
        } else if (strcmp(key, "invertcsticky") == 0) {
            cfg->invertcsticky = atoi(value) ? 1 : 0;
        }
    }

    fclose(file);
    printf("config loaded from %s\n", CONFIG_FILE);
}

static void write_u16_le(u8 *dst, u16 value) {
    dst[0] = (u8)(value & 0xFF);
    dst[1] = (u8)((value >> 8) & 0xFF);
}

static void write_u32_le(u8 *dst, u32 value) {
    dst[0] = (u8)(value & 0xFF);
    dst[1] = (u8)((value >> 8) & 0xFF);
    dst[2] = (u8)((value >> 16) & 0xFF);
    dst[3] = (u8)((value >> 24) & 0xFF);
}

static void write_u64_le(u8 *dst, u64 value) {
    for (int i = 0; i < 8; i++) {
        dst[i] = (u8)((value >> (8 * i)) & 0xFF);
    }
}

static void write_f32_le(u8 *dst, float value) {
    union {
        float f;
        u32 u;
    } conv;
    conv.f = value;
    write_u32_le(dst, conv.u);
}

static u16 read_u16_le(const u8 *src) {
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static u32 read_u32_le(const u8 *src) {
    return (u32)src[0] |
           ((u32)src[1] << 8) |
           ((u32)src[2] << 16) |
           ((u32)src[3] << 24);
}

static s32 read_i32_le(const u8 *src) {
    return (s32)read_u32_le(src);
}

static u32 crc32_compute(const u8 *data, size_t len) {
    u32 crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= (u32)data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static int initsocket(const config *cfg) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("socket creation failed\n");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u16)cfg->port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("bind failed on UDP port %d\n", cfg->port);
        close(sockfd);
        return -1;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    return sockfd;
}

static int is_sender_allowed(const config *cfg, const struct sockaddr_in *sender) {
    if (cfg->targetip[0] == '\0' || strcmp(cfg->targetip, "0.0.0.0") == 0) {
        return 1;
    }

    struct in_addr allowed;
    if (inet_pton(AF_INET, cfg->targetip, &allowed) <= 0) {
        return 1;
    }

    return sender->sin_addr.s_addr == allowed.s_addr;
}

static u8 normalize_slot(u8 rawslot) {
    if (rawslot >= 1 && rawslot <= 4) {
        return (u8)(rawslot - 1);
    }
    if (rawslot <= 3) {
        return rawslot;
    }
    return CONTROLLER_SLOT;
}

static subscriber *find_or_create_subscriber(subscriber *subs, const struct sockaddr_in *sender, u64 nowms) {
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subs[i].active) {
            continue;
        }

        if (subs[i].addr.sin_addr.s_addr == sender->sin_addr.s_addr &&
            subs[i].addr.sin_port == sender->sin_port) {
            return &subs[i];
        }
    }

    int freeidx = -1;
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subs[i].active) {
            freeidx = i;
            break;
        }
    }

    if (freeidx < 0) {
        int oldest = 0;
        for (int i = 1; i < MAX_SUBSCRIBERS; i++) {
            if (subs[i].lastseenms < subs[oldest].lastseenms) {
                oldest = i;
            }
        }
        freeidx = oldest;
    }

    memset(&subs[freeidx], 0, sizeof(subscriber));
    subs[freeidx].active = 1;
    subs[freeidx].addr = *sender;
    subs[freeidx].slot = CONTROLLER_SLOT;
    subs[freeidx].lastseenms = nowms;
    return &subs[freeidx];
}

static int send_dsu_packet(int sockfd, const struct sockaddr_in *dst, u32 eventtype, const u8 *payload, u16 payloadlen) {
    u8 packet[16 + 4 + 80];
    const u16 totalpayload = (u16)(4 + payloadlen);
    const size_t packetlen = (size_t)16 + totalpayload;

    if (packetlen > sizeof(packet)) {
        return -1;
    }

    memcpy(packet, "DSUS", 4);
    write_u16_le(packet + 4, DSU_PROTOCOL_VERSION);
    write_u16_le(packet + 6, totalpayload);
    write_u32_le(packet + 8, 0);
    write_u32_le(packet + 12, DSU_SERVER_ID);
    write_u32_le(packet + 16, eventtype);
    if (payloadlen > 0) {
        memcpy(packet + 20, payload, payloadlen);
    }

    write_u32_le(packet + 8, crc32_compute(packet, packetlen));

    return (int)sendto(sockfd,
                       packet,
                       packetlen,
                       0,
                       (const struct sockaddr *)dst,
                       sizeof(*dst));
}

static u8 get_dsu_battery_status(void) {
    u8 level = 0;
    u8 charging = 0;
    PTMU_GetBatteryLevel(&level);
    PTMU_GetBatteryChargeState(&charging);

    if (charging) {
        return (level >= 4) ? 0xEF : 0xEE;
    }

    switch (level) {
        case 0: return 0x01;
        case 1: return 0x02;
        case 2: return 0x03;
        case 3: return 0x04;
        default: return 0x05;
    }
}

static void send_protocol_version_response(int sockfd, const struct sockaddr_in *sender) {
    u8 payload[2];
    write_u16_le(payload, DSU_PROTOCOL_VERSION);
    send_dsu_packet(sockfd, sender, EVENT_PROTOCOL_VERSION, payload, sizeof(payload));
}

static void send_controller_info_response(int sockfd, const struct sockaddr_in *sender, u8 slot) {
    u8 payload[12];
    memset(payload, 0, sizeof(payload));

    if (slot == CONTROLLER_SLOT) {
        payload[0] = CONTROLLER_SLOT;
        payload[1] = 2;
        payload[2] = 2;
        payload[3] = 2;
        memset(payload + 4, 0, 6);
        payload[10] = get_dsu_battery_status();
        payload[11] = 0;
    }

    send_dsu_packet(sockfd, sender, EVENT_CONTROLLER_INFO, payload, sizeof(payload));
}

static void handle_dsu_packet(int sockfd,
                              const config *cfg,
                              subscriber *subs,
                              const u8 *data,
                              size_t datalen,
                              const struct sockaddr_in *sender,
                              u64 nowms) {
    (void)cfg;

    if (datalen < 20 || memcmp(data, "DSUC", 4) != 0) {
        return;
    }

    const u16 version = read_u16_le(data + 4);
    if (version != DSU_PROTOCOL_VERSION) {
        return;
    }

    const u16 payloadlen = read_u16_le(data + 6);
    const size_t packetlen = (size_t)16 + payloadlen;
    if (packetlen > datalen || packetlen > DSU_MAX_PACKET_SIZE) {
        return;
    }

    u8 packet[DSU_MAX_PACKET_SIZE];
    memcpy(packet, data, packetlen);

    const u32 receivedcrc = read_u32_le(packet + 8);
    memset(packet + 8, 0, 4);
    if (receivedcrc != crc32_compute(packet, packetlen)) {
        return;
    }

    const u32 eventtype = read_u32_le(packet + 16);
    const u8 *eventpayload = packet + 20;
    const size_t eventpayloadlen = packetlen - 20;

    if (eventtype == EVENT_PROTOCOL_VERSION) {
        send_protocol_version_response(sockfd, sender);
        return;
    }

    if (eventtype == EVENT_CONTROLLER_INFO) {
        if (eventpayloadlen < 4) {
            return;
        }

        s32 count = read_i32_le(eventpayload);
        if (count < 0 || count > 4 || (size_t)(4 + count) > eventpayloadlen) {
            return;
        }

        for (s32 i = 0; i < count; i++) {
            send_controller_info_response(sockfd, sender, eventpayload[4 + i]);
        }
        return;
    }

    if (eventtype == EVENT_CONTROLLER_DATA) {
        subscriber *sub = find_or_create_subscriber(subs, sender, nowms);
        if (!sub) {
            return;
        }

        sub->flags = (eventpayloadlen >= 1) ? eventpayload[0] : 0;
        sub->slot = (eventpayloadlen >= 2) ? normalize_slot(eventpayload[1]) : CONTROLLER_SLOT;
        if (eventpayloadlen >= 8) {
            memcpy(sub->mac, eventpayload + 2, 6);
        } else {
            memset(sub->mac, 0, sizeof(sub->mac));
        }
        sub->lastseenms = nowms;
    }
}

static void process_incoming_packets(int sockfd, const config *cfg, subscriber *subs, u64 nowms) {
    for (int i = 0; i < 6; i++) {
        u8 buffer[DSU_MAX_PACKET_SIZE];
        struct sockaddr_in sender;
        socklen_t senderlen = sizeof(sender);

        ssize_t received = recvfrom(sockfd,
                                    buffer,
                                    sizeof(buffer),
                                    0,
                                    (struct sockaddr *)&sender,
                                    &senderlen);

        if (received < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            break;
        }

        if (!is_sender_allowed(cfg, &sender)) {
            continue;
        }

        handle_dsu_packet(sockfd, cfg, subs, buffer, (size_t)received, &sender, nowms);
    }
}

static int count_active_subscribers(subscriber *subs, u64 nowms) {
    int count = 0;

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subs[i].active) {
            continue;
        }

        if (nowms - subs[i].lastseenms > SUBSCRIBER_TIMEOUT_MS) {
            memset(&subs[i], 0, sizeof(subscriber));
            continue;
        }

        count++;
    }

    return count;
}

static float apply_deadzone(float value, float deadzone) {
    return (fabsf(value) < deadzone) ? 0.0f : value;
}

static float clampf(float value, float minv, float maxv) {
    if (value < minv) {
        return minv;
    }
    if (value > maxv) {
        return maxv;
    }
    return value;
}

static u8 axis_to_byte(int value, int range, int invert) {
    float normalized = (float)value / (float)range;
    normalized = clampf(normalized, -1.0f, 1.0f);
    if (invert) {
        normalized = -normalized;
    }

    int mapped = (int)lroundf((normalized * 127.0f) + 128.0f);
    if (mapped < 0) mapped = 0;
    if (mapped > 255) mapped = 255;
    return (u8)mapped;
}

static u16 scale_touch_coord(int value, int srcmax, int dstmax) {
    if (value < 0) value = 0;
    if (value > srcmax) value = srcmax;
    return (u16)((value * dstmax) / srcmax);
}

static void build_controller_payload(const config *cfg, const inputstate *state, u8 *outpayload80) {
    memset(outpayload80, 0, 80);

    const u32 keys = state->keys;

    const int dpadleft = (keys & KEY_LEFT) ? 1 : 0;
    const int dpaddown = (keys & KEY_DOWN) ? 1 : 0;
    const int dpadright = (keys & KEY_RIGHT) ? 1 : 0;
    const int dpadup = (keys & KEY_UP) ? 1 : 0;
    const int options = (keys & KEY_START) ? 1 : 0;
    const int l3 = 0;
    const int r3 = 0;
    const int share = (keys & KEY_SELECT) ? 1 : 0;

    const int triangle = (keys & KEY_Y) ? 1 : 0;
    const int circle = (keys & KEY_B) ? 1 : 0;
    const int cross = (keys & KEY_A) ? 1 : 0;
    const int square = (keys & KEY_X) ? 1 : 0;
    const int r1 = (keys & KEY_R) ? 1 : 0;
    const int l1 = (keys & KEY_L) ? 1 : 0;
    const int r2 = (keys & KEY_ZR) ? 1 : 0;
    const int l2 = (keys & KEY_ZL) ? 1 : 0;

    const u8 buttons1 = (u8)((dpadleft << 7) |
                             (dpaddown << 6) |
                             (dpadright << 5) |
                             (dpadup << 4) |
                             (options << 3) |
                             (r3 << 2) |
                             (l3 << 1) |
                             (share << 0));

    const u8 buttons2 = (u8)((triangle << 7) |
                             (circle << 6) |
                             (cross << 5) |
                             (square << 4) |
                             (r1 << 3) |
                             (l1 << 2) |
                             (r2 << 1) |
                             (l2 << 0));

    const u8 home = (keys & KEY_HOME) ? 1 : 0;
    const u8 touchbutton = state->touchactive ? 1 : 0;

    const u8 lx = axis_to_byte(state->circlepad.dx, 156, 0);
    const u8 ly = axis_to_byte(state->circlepad.dy, 156, cfg->invertcpady);
    const u8 rx = axis_to_byte(state->cstick.dx, 156, 0);
    const u8 ry = axis_to_byte(state->cstick.dy, 156, cfg->invertcsticky);

    const u16 touchx = scale_touch_coord(state->touch.px, TOUCH_WIDTH - 1, DS4_TOUCH_MAX_X);
    const u16 touchy = scale_touch_coord(state->touch.py, TOUCH_HEIGHT - 1, DS4_TOUCH_MAX_Y);

    float accelx = clampf((float)state->accel.x / ACCEL_RAW_TO_G, -8.0f, 8.0f);
    float accely = clampf((float)state->accel.y / ACCEL_RAW_TO_G, -8.0f, 8.0f);
    float accelz = clampf((float)state->accel.z / ACCEL_RAW_TO_G, -8.0f, 8.0f);
    float gyropitch = clampf((float)state->gyro.x / GYRO_RAW_TO_DPS, -2000.0f, 2000.0f);
    float gyroyaw = clampf((float)state->gyro.y / GYRO_RAW_TO_DPS, -2000.0f, 2000.0f);
    float gyroroll = clampf((float)state->gyro.z / GYRO_RAW_TO_DPS, -2000.0f, 2000.0f);

    accelx = apply_deadzone(accelx, ACCEL_DEADZONE_G);
    accely = apply_deadzone(accely, ACCEL_DEADZONE_G);
    accelz = apply_deadzone(accelz, ACCEL_DEADZONE_G);
    gyropitch = apply_deadzone(gyropitch, GYRO_DEADZONE_DPS);
    gyroyaw = apply_deadzone(gyroyaw, GYRO_DEADZONE_DPS);
    gyroroll = apply_deadzone(gyroroll, GYRO_DEADZONE_DPS);

    outpayload80[0] = CONTROLLER_SLOT;
    outpayload80[1] = 2;
    outpayload80[2] = 2;
    outpayload80[3] = 2;
    memset(outpayload80 + 4, 0, 6);
    outpayload80[10] = get_dsu_battery_status();
    outpayload80[11] = 1;

    write_u32_le(outpayload80 + 12, 0);
    outpayload80[16] = buttons1;
    outpayload80[17] = buttons2;
    outpayload80[18] = home;
    outpayload80[19] = touchbutton;

    outpayload80[20] = lx;
    outpayload80[21] = ly;
    outpayload80[22] = rx;
    outpayload80[23] = ry;

    outpayload80[24] = dpadleft ? 0xFF : 0x00;
    outpayload80[25] = dpaddown ? 0xFF : 0x00;
    outpayload80[26] = dpadright ? 0xFF : 0x00;
    outpayload80[27] = dpadup ? 0xFF : 0x00;

    outpayload80[28] = triangle ? 0xFF : 0x00;
    outpayload80[29] = circle ? 0xFF : 0x00;
    outpayload80[30] = cross ? 0xFF : 0x00;
    outpayload80[31] = square ? 0xFF : 0x00;

    outpayload80[32] = r1 ? 0xFF : 0x00;
    outpayload80[33] = l1 ? 0xFF : 0x00;
    outpayload80[34] = r2 ? 0xFF : 0x00;
    outpayload80[35] = l2 ? 0xFF : 0x00;

    outpayload80[36] = state->touchactive ? 1 : 0;
    outpayload80[37] = state->touchid;
    write_u16_le(outpayload80 + 38, touchx);
    write_u16_le(outpayload80 + 40, touchy);

    outpayload80[42] = 0;
    outpayload80[43] = 0;
    write_u16_le(outpayload80 + 44, 0);
    write_u16_le(outpayload80 + 46, 0);

    write_u64_le(outpayload80 + 48, state->motiontsus);
    write_f32_le(outpayload80 + 56, accelx);
    write_f32_le(outpayload80 + 60, accely);
    write_f32_le(outpayload80 + 64, accelz);
    write_f32_le(outpayload80 + 68, gyropitch);
    write_f32_le(outpayload80 + 72, gyroyaw);
    write_f32_le(outpayload80 + 76, gyroroll);
}

static void send_controller_data_to_subscribers(int sockfd, subscriber *subs, const u8 *basepayload80, u64 nowms) {
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subs[i].active) {
            continue;
        }

        if (nowms - subs[i].lastseenms > SUBSCRIBER_TIMEOUT_MS) {
            memset(&subs[i], 0, sizeof(subscriber));
            continue;
        }

        u8 payload[80];
        memcpy(payload, basepayload80, sizeof(payload));
        payload[0] = subs[i].slot;
        subs[i].packetnum++;
        write_u32_le(payload + 12, subs[i].packetnum);

        send_dsu_packet(sockfd, &subs[i].addr, EVENT_CONTROLLER_DATA, payload, sizeof(payload));
    }
}

static void getbatterystatus(char *buffer, size_t size) {
    u8 percentage = 0;
    u8 charging = 0;
    PTMU_GetBatteryLevel(&percentage);
    PTMU_GetBatteryChargeState(&charging);

    char icon[6];
    int actualpercentage = (percentage + 1) * 20;
    if (actualpercentage > 100) {
        actualpercentage = 100;
    }

    for (int i = 0; i < 5; i++) {
        icon[i] = (i <= percentage) ? '|' : ' ';
    }
    icon[5] = '\0';

    if (charging) {
        snprintf(buffer, size, "[%s] +", icon);
    } else {
        snprintf(buffer, size, "[%s] %d%%", icon, actualpercentage);
    }
}

static void printstatusmessage(const config *cfg, int socketready, int subscribers, int lcdstate) {
    consoleClear();

    char batterystatus[32];
    getbatterystatus(batterystatus, sizeof(batterystatus));

    printf("\x1b[0;0H+------------------------------+");
    printf("\x1b[1;0H|   \x1b[1;36m3DS DSU (DUALSHOCK)\x1b[0m    |");
    printf("\x1b[2;0H+------------------------------+");

    if (socketready) {
        printf("\x1b[3;0H| Server: \x1b[32mRUNNING\x1b[0m            |");
    } else {
        printf("\x1b[3;0H| Server: \x1b[31mSOCKET ERROR\x1b[0m       |");
    }

    printf("\x1b[4;0H| Port: %-24d|", cfg->port);
    printf("\x1b[5;0H| IP Filter: %-19s|", cfg->targetip);
    printf("\x1b[6;0H| Subscribers: %-17d|", subscribers);
    printf("\x1b[7;0H| Battery: %-20s|", batterystatus);

    if (lcdstate) {
        printf("\x1b[8;0H| Display: \x1b[32mON\x1b[0m                 |");
    } else {
        printf("\x1b[8;0H| Display: \x1b[31mOFF\x1b[0m                |");
    }

    printf("\x1b[9;0H+------------------------------+");
    printf("\x1b[10;0H| CirclePad -> Left Stick      |");
    printf("\x1b[11;0H| C-Stick   -> Right Stick     |");
    printf("\x1b[12;0H| Touchscreen -> DS4 Touchpad  |");
    printf("\x1b[13;0H| Gyro+Accel -> DSU Motion     |");
    printf("\x1b[14;0H| SELECT: Share / Hold LCD     |");
    printf("\x1b[15;0H| START : Options              |");
    printf("\x1b[16;0H| START+SELECT: Exit           |");
    printf("\x1b[17;0H+------------------------------+");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    void *socbuffer = memalign(0x1000, 0x100000);
    if (!socbuffer) {
        printf("failed to allocate SOC buffer\n");
        gfxExit();
        return 1;
    }

    if (socInit((u32 *)socbuffer, 0x100000) != 0) {
        printf("socInit failed\n");
        free(socbuffer);
        gfxExit();
        return 1;
    }

    ptmuInit();
    HIDUSER_EnableAccelerometer();
    HIDUSER_EnableGyroscope();

    config cfg;
    readconfigfile(&cfg);

    int sockfd = initsocket(&cfg);
    int lcdstate = 1;
    u32 lcdtoggleheldtime = 0;
    u32 laststatusupdate = 0;

    subscriber subscribers[MAX_SUBSCRIBERS];
    memset(subscribers, 0, sizeof(subscribers));

    int was_touch_active = 0;
    u8 touch_id = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 keysheld = hidKeysHeld();
        u32 currenttime = osGetTime();

        if ((keysheld & KEY_START) && (keysheld & KEY_SELECT)) {
            break;
        }

        if (keysheld & LCD_TOGGLE_KEY) {
            if (lcdtoggleheldtime == 0) {
                lcdtoggleheldtime = currenttime;
            } else if (currenttime - lcdtoggleheldtime > LCD_TOGGLE_HOLD_TIME) {
                lcdstate = !lcdstate;
                if (lcdstate) {
                    gspLcdInit();
                    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
                } else {
                    GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
                }
                lcdtoggleheldtime = currenttime;
            }
        } else {
            lcdtoggleheldtime = 0;
        }

        const u64 nowms = osGetTime();
        if (sockfd >= 0) {
            process_incoming_packets(sockfd, &cfg, subscribers, nowms);
        }

        inputstate state;
        memset(&state, 0, sizeof(state));
        state.keys = keysheld;
        hidCircleRead(&state.circlepad);
        hidCstickRead(&state.cstick);

        state.touchactive = (keysheld & KEY_TOUCH) ? 1 : 0;
        if (state.touchactive) {
            hidTouchRead(&state.touch);
            if (!was_touch_active) {
                touch_id++;
            }
        }
        was_touch_active = state.touchactive;
        state.touchid = touch_id;

        hidAccelRead(&state.accel);
        hidGyroRead(&state.gyro);
        state.motiontsus = ((u64)nowms) * 1000ULL;

        u8 controllerpayload[80];
        build_controller_payload(&cfg, &state, controllerpayload);

        if (sockfd >= 0) {
            send_controller_data_to_subscribers(sockfd, subscribers, controllerpayload, nowms);
        }

        int active_subscribers = count_active_subscribers(subscribers, nowms);
        if (currenttime - laststatusupdate > 500) {
            printstatusmessage(&cfg, sockfd >= 0, active_subscribers, lcdstate);
            laststatusupdate = currenttime;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    if (sockfd >= 0) {
        close(sockfd);
    }

    HIDUSER_DisableGyroscope();
    HIDUSER_DisableAccelerometer();
    ptmuExit();
    socExit();
    free(socbuffer);
    gfxExit();
    return 0;
}
