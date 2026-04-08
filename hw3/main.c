#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT_SEC 10
#define CCCD_UUID16 0x2902

typedef struct {
	char* adapter_name;
	char* mac_address;
	uuid_t target_char_uuid;
} app_args_t;

typedef struct {
	uint16_t decl_handle;
	uint16_t value_handle;
	uint16_t next_decl_handle;
	int found;
} target_char_handles_t;

static app_args_t g_args;
static pthread_cond_t g_done_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_done_mutex = PTHREAD_MUTEX_INITIALIZER;

static int stricmp_local(const char* a, const char* b) {
	for (;; a++, b++) {
		int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
		if ((d != 0) || (*a == '\0')) {
			return d;
		}
	}
}

static void signal_done(void) {
	pthread_mutex_lock(&g_done_mutex);
	pthread_cond_signal(&g_done_cond);
	pthread_mutex_unlock(&g_done_mutex);
}

static void dump_characteristics(gattlib_characteristic_t* chars, int chars_count) {
	char uuid_str[MAX_LEN_UUID_STR + 1];

	printf("Discovered %d characteristic(s):\n", chars_count);
	for (int i = 0; i < chars_count; i++) {
		memset(uuid_str, 0, sizeof(uuid_str));
		gattlib_uuid_to_string(&chars[i].uuid, uuid_str, sizeof(uuid_str));
		printf("  - UUID=%s, decl_handle=0x%04x, value_handle=0x%04x, props=0x%02x\n",
		       uuid_str, chars[i].handle, chars[i].value_handle, chars[i].properties);
	}
}

static target_char_handles_t find_target_char_handles(gattlib_connection_t* connection, const uuid_t* target_uuid) {
	gattlib_characteristic_t* chars = NULL;
	int chars_count = 0;
	char target_uuid_str[MAX_LEN_UUID_STR + 1];
	char char_uuid_str[MAX_LEN_UUID_STR + 1];
	target_char_handles_t out = {0, 0, 0, 0};

	if (gattlib_discover_char(connection, &chars, &chars_count) != GATTLIB_SUCCESS) {
		return out;
	}

	memset(target_uuid_str, 0, sizeof(target_uuid_str));
	gattlib_uuid_to_string(target_uuid, target_uuid_str, sizeof(target_uuid_str));

	for (int i = 0; i < chars_count; i++) {
		memset(char_uuid_str, 0, sizeof(char_uuid_str));
		gattlib_uuid_to_string(&chars[i].uuid, char_uuid_str, sizeof(char_uuid_str));
		if (strcmp(char_uuid_str, target_uuid_str) == 0) {
			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			free(chars);
			return out;
		}
		if (gattlib_uuid_cmp((uuid_t*)&chars[i].uuid, (uuid_t*)target_uuid) == 0) {
			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			free(chars);
			return out;
		}
	}

	fprintf(stderr, "Target UUID %s not found.\n", target_uuid_str);
	dump_characteristics(chars, chars_count);
	free(chars);
	return out;
}

static uint16_t find_cccd_handle(gattlib_connection_t* connection, uint16_t value_handle, uint16_t next_decl_handle) {
	gattlib_descriptor_t* descriptors = NULL;
	int desc_count = 0;
	uint16_t best_handle = 0;

	if (gattlib_discover_desc(connection, &descriptors, &desc_count) != GATTLIB_SUCCESS) {
		return 0;
	}

	for (int i = 0; i < desc_count; i++) {
		if ((descriptors[i].uuid16 == CCCD_UUID16) &&
		    (descriptors[i].handle > value_handle) &&
		    (descriptors[i].handle < next_decl_handle)) {
			if ((best_handle == 0) || (descriptors[i].handle < best_handle)) {
				best_handle = descriptors[i].handle;
			}
		}
	}

	free(descriptors);
	return best_handle;
}

static void on_device_connect(gattlib_adapter_t* adapter, const char* dst, gattlib_connection_t* connection, int error, void* user_data) {
	(void)adapter;
	(void)dst;
	(void)user_data;

	if (error != 0) {
		fprintf(stderr, "Connection callback error: %d\n", error);
		signal_done();
		return;
	}

	target_char_handles_t target = find_target_char_handles(connection, &g_args.target_char_uuid);
	if (!target.found) {
		fprintf(stderr, "Target characteristic UUID not found on peer.\n");
		goto EXIT;
	}

	uint16_t cccd_handle = find_cccd_handle(connection, target.value_handle, target.next_decl_handle);
	if (cccd_handle == 0) {
		int ret_fallback = gattlib_indication_start(connection, &g_args.target_char_uuid);
		if (ret_fallback != GATTLIB_SUCCESS) {
			fprintf(stderr, "CCCD (0x2902) descriptor not found for target characteristic.\n");
			fprintf(stderr, "Fallback gattlib_indication_start() failed (ret=%d)\n", ret_fallback);
			goto EXIT;
		}
		printf("SUCCESS: indication_start() accepted by BlueZ for target UUID.\n");
		printf("This enables indications (equivalent CCCD value 0x0002) even when descriptor 0x2902 is not exposed.\n");
		goto EXIT;
	}

	/* CCCD value 0x0002 (little endian) enables indications. */
	const uint8_t cccd_enable_indication[2] = {0x02, 0x00};
	int ret = gattlib_write_char_by_handle(connection, cccd_handle, cccd_enable_indication, sizeof(cccd_enable_indication));
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Failed to write CCCD handle 0x%04x (ret=%d)\n", cccd_handle, ret);
		goto EXIT;
	}

	printf("SUCCESS: wrote CCCD handle 0x%04x with value 0x0002 (indications enabled).\n", cccd_handle);

EXIT:
	gattlib_disconnect(connection, false);
	signal_done();
}

static void ble_discovered_device(gattlib_adapter_t* adapter, const char* addr, const char* name, void* user_data) {
	(void)name;
	(void)user_data;

	if (stricmp_local(addr, g_args.mac_address) != 0) {
		return;
	}

	printf("Found target device: %s\n", addr);
	int ret = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_NONE, on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Failed to connect to %s (ret=%d)\n", addr, ret);
		signal_done();
	}
}

static void* ble_task(void* arg) {
	(void)arg;
	gattlib_adapter_t* adapter = NULL;

	if (gattlib_adapter_open(g_args.adapter_name, &adapter) != GATTLIB_SUCCESS) {
		fprintf(stderr, "Failed to open BLE adapter.\n");
		return NULL;
	}

	if (gattlib_adapter_scan_enable(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT_SEC, NULL) != GATTLIB_SUCCESS) {
		fprintf(stderr, "Failed to start scan.\n");
		return NULL;
	}

	pthread_mutex_lock(&g_done_mutex);
	pthread_cond_wait(&g_done_cond, &g_done_mutex);
	pthread_mutex_unlock(&g_done_mutex);

	return NULL;
}

static void usage(const char* prog) {
	fprintf(stderr, "Usage: %s <device_mac> <target_char_uuid>\n", prog);
	fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF 0000fff1-0000-1000-8000-00805f9b34fb\n", prog);
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		usage(argv[0]);
		return 1;
	}

	if (gattlib_string_to_uuid(argv[2], strlen(argv[2]) + 1, &g_args.target_char_uuid) < 0) {
		fprintf(stderr, "Invalid UUID format: %s\n", argv[2]);
		return 1;
	}

	g_args.adapter_name = NULL; /* default adapter (hci0) */
	g_args.mac_address = argv[1];

	int ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "gattlib mainloop failed (ret=%d)\n", ret);
		return 1;
	}

	return 0;
}
