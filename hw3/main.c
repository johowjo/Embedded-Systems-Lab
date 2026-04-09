#include <ctype.h>
#include <glib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT_SEC 10
#define CCCD_UUID16 0x2902
#define DISCOVERY_RETRY_COUNT 6
#define DISCOVERY_RETRY_DELAY_MS 500
#define GATT_SERVICE_CHANGED_UUID16 0x2A05
#define START_RETRY_COUNT 5
#define START_RETRY_DELAY_MS 400

typedef struct {
	char* adapter_name;
	char* mac_address;
	uuid_t target_char_uuid;
	int auto_pick_indicate_char;
} app_args_t;

typedef struct {
	uint16_t decl_handle;
	uint16_t value_handle;
	uint16_t next_decl_handle;
	int found;
	uuid_t uuid;
} target_char_handles_t;

static app_args_t g_args;
static pthread_cond_t g_done_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static gattlib_adapter_t* g_adapter = NULL;
static int g_connecting = 0;

static void value_change_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	(void)user_data;
	char uuid_str[MAX_LEN_UUID_STR + 1] = {0};
	gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));
	printf("[VALUE CHANGE] uuid=%s len=%zu data=", uuid_str, data_length);
	for (size_t i = 0; i < data_length; i++) {
		printf("%02x", data[i]);
	}
	printf("\n");
}

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

static void print_gattlib_error_details(const char* prefix, int ret) {
	const int module = ret & GATTLIB_ERROR_MODULE_MASK;
	if (module == GATTLIB_ERROR_DBUS) {
		int domain = (ret >> 8) & 0xFF;
		int code = ret & 0xFF;
		fprintf(stderr, "%s (ret=%d, module=DBUS, domain=%d, code=%d)\n", prefix, ret, domain, code);
	} else if (module == GATTLIB_ERROR_BLUEZ) {
		fprintf(stderr, "%s (ret=%d, module=BLUEZ, code=%d)\n", prefix, ret, ret & 0xFFFF);
	} else if (module == GATTLIB_ERROR_UNIX) {
		fprintf(stderr, "%s (ret=%d, module=UNIX, errno=%d)\n", prefix, ret, ret & 0xFFFF);
	} else {
		fprintf(stderr, "%s (ret=%d)\n", prefix, ret);
	}
}

static int start_indication_with_retry(gattlib_connection_t* connection, const uuid_t* uuid) {
	int ret = GATTLIB_NOT_FOUND;
	for (int i = 0; i < START_RETRY_COUNT; i++) {
		ret = gattlib_indication_start(connection, uuid);
		if (ret == GATTLIB_SUCCESS) {
			return ret;
		}
		g_usleep(START_RETRY_DELAY_MS * 1000);
	}
	return ret;
}

static int start_notification_with_retry(gattlib_connection_t* connection, const uuid_t* uuid) {
	int ret = GATTLIB_NOT_FOUND;
	for (int i = 0; i < START_RETRY_COUNT; i++) {
		ret = gattlib_notification_start(connection, uuid);
		if (ret == GATTLIB_SUCCESS) {
			return ret;
		}
		g_usleep(START_RETRY_DELAY_MS * 1000);
	}
	return ret;
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

static int is_uuid_16bit(const char* uuid_str) {
	return (strncmp(uuid_str, "0x", 2) == 0) && (strlen(uuid_str) <= 6);
}

static target_char_handles_t find_target_char_handles(gattlib_connection_t* connection, const uuid_t* target_uuid, int auto_pick_indicate) {
	gattlib_characteristic_t* chars = NULL;
	int chars_count = 0;
	char target_uuid_str[MAX_LEN_UUID_STR + 1];
	char char_uuid_str[MAX_LEN_UUID_STR + 1];
	target_char_handles_t out = {0, 0, 0, 0};
	int fallback_notify_index = -1;

	if (gattlib_discover_char(connection, &chars, &chars_count) != GATTLIB_SUCCESS) {
		return out;
	}

	memset(target_uuid_str, 0, sizeof(target_uuid_str));
	if (target_uuid != NULL) {
		gattlib_uuid_to_string(target_uuid, target_uuid_str, sizeof(target_uuid_str));
	}

	for (int i = 0; i < chars_count; i++) {
		memset(char_uuid_str, 0, sizeof(char_uuid_str));
		gattlib_uuid_to_string(&chars[i].uuid, char_uuid_str, sizeof(char_uuid_str));

		if (auto_pick_indicate && ((chars[i].properties & GATTLIB_CHARACTERISTIC_INDICATE) != 0)) {
			/* Skip Service Changed (0x2A05): system characteristic, poor demo target. */
			if (chars[i].uuid.value.uuid16 == GATT_SERVICE_CHANGED_UUID16) {
				continue;
			}
			/* Prefer 128-bit app-defined characteristics for a phone app demo. */
			if (is_uuid_16bit(char_uuid_str)) {
				if (((chars[i].properties & GATTLIB_CHARACTERISTIC_NOTIFY) != 0) && (fallback_notify_index < 0)) {
					fallback_notify_index = i;
				}
				continue;
			}

			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			memcpy(&out.uuid, &chars[i].uuid, sizeof(uuid_t));
			free(chars);
			return out;
		}

		if (target_uuid == NULL) {
			continue;
		}
		if (strcmp(char_uuid_str, target_uuid_str) == 0) {
			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			memcpy(&out.uuid, &chars[i].uuid, sizeof(uuid_t));
			free(chars);
			return out;
		}
		if (gattlib_uuid_cmp((uuid_t*)&chars[i].uuid, (uuid_t*)target_uuid) == 0) {
			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			memcpy(&out.uuid, &chars[i].uuid, sizeof(uuid_t));
			free(chars);
			return out;
		}
	}

	/* Auto mode fallback: pick first 128-bit NOTIFY characteristic if no INDICATE candidate exists. */
	if (auto_pick_indicate) {
		for (int i = 0; i < chars_count; i++) {
			memset(char_uuid_str, 0, sizeof(char_uuid_str));
			gattlib_uuid_to_string(&chars[i].uuid, char_uuid_str, sizeof(char_uuid_str));
			if (!is_uuid_16bit(char_uuid_str) &&
			    ((chars[i].properties & GATTLIB_CHARACTERISTIC_NOTIFY) != 0)) {
				out.decl_handle = chars[i].handle;
				out.value_handle = chars[i].value_handle;
				out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
				out.found = 1;
				memcpy(&out.uuid, &chars[i].uuid, sizeof(uuid_t));
				free(chars);
				return out;
			}
		}

		if (fallback_notify_index >= 0) {
			int i = fallback_notify_index;
			out.decl_handle = chars[i].handle;
			out.value_handle = chars[i].value_handle;
			out.next_decl_handle = (i + 1 < chars_count) ? chars[i + 1].handle : 0xFFFF;
			out.found = 1;
			memcpy(&out.uuid, &chars[i].uuid, sizeof(uuid_t));
			free(chars);
			return out;
		}
	}

	if (target_uuid != NULL) {
		fprintf(stderr, "Target UUID %s not found.\n", target_uuid_str);
	} else {
		fprintf(stderr, "No characteristic with INDICATE property found.\n");
	}
	dump_characteristics(chars, chars_count);
	free(chars);
	return out;
}

static target_char_handles_t find_target_char_handles_with_retry(gattlib_connection_t* connection, const uuid_t* target_uuid, int auto_pick_indicate) {
	target_char_handles_t result = {0, 0, 0, 0};
	for (int attempt = 0; attempt < DISCOVERY_RETRY_COUNT; attempt++) {
		result = find_target_char_handles(connection, target_uuid, auto_pick_indicate);
		if (result.found) {
			return result;
		}
		g_usleep(DISCOVERY_RETRY_DELAY_MS * 1000);
	}
	return result;
}

static int try_enable_cccd_by_api_on_candidates(gattlib_connection_t* connection, uuid_t* selected_uuid) {
	gattlib_characteristic_t* chars = NULL;
	int chars_count = 0;
	char uuid_str[MAX_LEN_UUID_STR + 1];

	if (gattlib_discover_char(connection, &chars, &chars_count) != GATTLIB_SUCCESS) {
		return GATTLIB_NOT_FOUND;
	}

	/* First try INDICATE-capable 128-bit app characteristics. */
	for (int i = 0; i < chars_count; i++) {
		memset(uuid_str, 0, sizeof(uuid_str));
		gattlib_uuid_to_string(&chars[i].uuid, uuid_str, sizeof(uuid_str));
		if (is_uuid_16bit(uuid_str) || (chars[i].uuid.value.uuid16 == GATT_SERVICE_CHANGED_UUID16)) {
			continue;
		}
		if ((chars[i].properties & GATTLIB_CHARACTERISTIC_INDICATE) == 0) {
			continue;
		}

		int ret = start_indication_with_retry(connection, &chars[i].uuid);
		if (ret == GATTLIB_SUCCESS) {
			printf("SUCCESS: indication_start() succeeded on candidate UUID %s\n", uuid_str);
			if (selected_uuid != NULL) {
				memcpy(selected_uuid, &chars[i].uuid, sizeof(uuid_t));
			}
			free(chars);
			return GATTLIB_SUCCESS;
		}
	}

	/* Then try NOTIFY-capable 128-bit app characteristics. */
	for (int i = 0; i < chars_count; i++) {
		memset(uuid_str, 0, sizeof(uuid_str));
		gattlib_uuid_to_string(&chars[i].uuid, uuid_str, sizeof(uuid_str));
		if (is_uuid_16bit(uuid_str) || (chars[i].uuid.value.uuid16 == GATT_SERVICE_CHANGED_UUID16)) {
			continue;
		}
		if ((chars[i].properties & GATTLIB_CHARACTERISTIC_NOTIFY) == 0) {
			continue;
		}

		int ret = start_notification_with_retry(connection, &chars[i].uuid);
		if (ret == GATTLIB_SUCCESS) {
			printf("SUCCESS: notification_start() succeeded on candidate UUID %s\n", uuid_str);
			if (selected_uuid != NULL) {
				memcpy(selected_uuid, &chars[i].uuid, sizeof(uuid_t));
			}
			free(chars);
			return GATTLIB_SUCCESS;
		}
	}

	free(chars);
	return GATTLIB_NOT_FOUND;
}

static void monitor_value_changes(gattlib_connection_t* connection, const uuid_t* uuid, int duration_sec) {
	uint8_t* last_value = NULL;
	size_t last_len = 0;
	char uuid_str[MAX_LEN_UUID_STR + 1] = {0};
	gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

	printf("Listening for callback events and polling reads for %d seconds...\n", duration_sec);
	for (int i = 0; i < duration_sec; i++) {
		void* read_buf = NULL;
		size_t read_len = 0;
		int ret = gattlib_read_char_by_uuid(connection, (uuid_t*)uuid, &read_buf, &read_len);
		if (ret == GATTLIB_SUCCESS && read_buf != NULL) {
			if ((last_value == NULL) || (read_len != last_len) || (memcmp(last_value, read_buf, read_len) != 0)) {
				printf("[POLL READ CHANGE] uuid=%s len=%zu data=", uuid_str, read_len);
				for (size_t j = 0; j < read_len; j++) {
					printf("%02x", ((uint8_t*)read_buf)[j]);
				}
				printf("\n");

				free(last_value);
				last_value = malloc(read_len);
				if (last_value != NULL) {
					memcpy(last_value, read_buf, read_len);
					last_len = read_len;
				}
			}
			gattlib_characteristic_free_value(read_buf);
		} else {
			printf("[POLL] second=%d ", i + 1);
			print_gattlib_error_details("read failed", ret);
		}
		g_usleep(1 * G_USEC_PER_SEC);
	}

	free(last_value);
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
		print_gattlib_error_details("Connection callback error", error);
		signal_done();
		return;
	}

	target_char_handles_t target = find_target_char_handles_with_retry(
		connection,
		g_args.auto_pick_indicate_char ? NULL : &g_args.target_char_uuid,
		g_args.auto_pick_indicate_char
	);
	if (!target.found) {
		fprintf(stderr, "Target characteristic UUID not found on peer.\n");
		goto EXIT;
	}
	if (g_args.auto_pick_indicate_char) {
		char picked_uuid[MAX_LEN_UUID_STR + 1] = {0};
		gattlib_uuid_to_string(&target.uuid, picked_uuid, sizeof(picked_uuid));
		printf("Auto-picked characteristic with INDICATE property: %s\n", picked_uuid);
	}

	uuid_t* effective_uuid = g_args.auto_pick_indicate_char ? &target.uuid : &g_args.target_char_uuid;
	uuid_t monitored_uuid = *effective_uuid;
	int ret = gattlib_register_notification(connection, value_change_handler, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Warning: failed to register notification callback (ret=%d)\n", ret);
	}
	ret = gattlib_register_indication(connection, value_change_handler, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Warning: failed to register indication callback (ret=%d)\n", ret);
	}

	uint16_t cccd_handle = find_cccd_handle(connection, target.value_handle, target.next_decl_handle);
	if (cccd_handle == 0) {
		int ret_fallback = start_indication_with_retry(connection, effective_uuid);
		if (ret_fallback == GATTLIB_NOT_FOUND || ret_fallback == GATTLIB_NOT_SUPPORTED) {
			/* Some app characteristics support NOTIFY only; fallback to CCCD 0x0001 path. */
			ret_fallback = start_notification_with_retry(connection, effective_uuid);
			if (ret_fallback == GATTLIB_SUCCESS) {
				printf("SUCCESS: notification_start() accepted by BlueZ for target UUID.\n");
				printf("This enables notifications (equivalent CCCD value 0x0001).\n");
				monitor_value_changes(connection, effective_uuid, 30);
				goto EXIT;
			}
		}
		if (ret_fallback != GATTLIB_SUCCESS) {
			fprintf(stderr, "CCCD (0x2902) descriptor not found for target characteristic.\n");
			print_gattlib_error_details("Fallback indication/notification start failed", ret_fallback);
			ret_fallback = try_enable_cccd_by_api_on_candidates(connection, &monitored_uuid);
			if (ret_fallback == GATTLIB_SUCCESS) {
				printf("Enabled CCCD through API fallback on alternative candidate.\n");
				monitor_value_changes(connection, &monitored_uuid, 30);
				goto EXIT;
			}
			fprintf(stderr, "No alternative characteristic accepted indication/notification start.\n");
			goto EXIT;
		}
		printf("SUCCESS: indication_start() accepted by BlueZ for target UUID.\n");
		printf("This enables indications (equivalent CCCD value 0x0002) even when descriptor 0x2902 is not exposed.\n");
		monitor_value_changes(connection, &monitored_uuid, 30);
		goto EXIT;
	}

	/* CCCD value 0x0002 (little endian) enables indications. */
	const uint8_t cccd_enable_indication[2] = {0x02, 0x00};
	ret = gattlib_write_char_by_handle(connection, cccd_handle, cccd_enable_indication, sizeof(cccd_enable_indication));
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Failed to write CCCD handle 0x%04x (ret=%d)\n", cccd_handle, ret);
		goto EXIT;
	}

	printf("SUCCESS: wrote CCCD handle 0x%04x with value 0x0002 (indications enabled).\n", cccd_handle);
	monitor_value_changes(connection, &monitored_uuid, 30);

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
	if (g_connecting) {
		return;
	}
	g_connecting = 1;

	printf("Found target device: %s\n", addr);
	if (g_adapter != NULL) {
		/* Stop ongoing scan before connecting to reduce duplicate callbacks/races. */
		gattlib_adapter_scan_disable(g_adapter);
	}

	int options = GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT;
	int ret = gattlib_connect(adapter, addr, options, on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		print_gattlib_error_details("Failed to connect", ret);
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
	g_adapter = adapter;

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
	fprintf(stderr, "   or: %s <device_mac> auto\n", prog);
	fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF 0000fff1-0000-1000-8000-00805f9b34fb\n", prog);
	fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF auto\n", prog);
}

int main(int argc, char* argv[]) {
	/* Force immediate printing even when stdout is redirected/non-interactive. */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (argc != 3) {
		usage(argv[0]);
		return 1;
	}

	g_args.auto_pick_indicate_char = 0;
	if (strcmp(argv[2], "auto") == 0) {
		g_args.auto_pick_indicate_char = 1;
	} else if (gattlib_string_to_uuid(argv[2], strlen(argv[2]) + 1, &g_args.target_char_uuid) < 0) {
		fprintf(stderr, "Invalid UUID format: %s\n", argv[2]);
		return 1;
	}

	g_args.adapter_name = NULL; /* default adapter (hci0) */
	g_args.mac_address = argv[1];
	printf("Starting BLE CCCD demo. target=%s mode=%s\n",
	       g_args.mac_address,
	       g_args.auto_pick_indicate_char ? "auto" : "uuid");

	int ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "gattlib mainloop failed (ret=%d)\n", ret);
		return 1;
	}

	return 0;
}
