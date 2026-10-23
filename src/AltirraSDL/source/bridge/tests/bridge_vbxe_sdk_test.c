#include <stdio.h>
#include <string.h>
#include "altirra_bridge.h"

int main(int argc, char **argv) {
	unsigned char source[] = {0x12, 0x34, 0xAB, 0xCD};
	unsigned char result[4] = {0};
	atb_client_t *client;
	int ok;
	if (argc != 2) return 2;
	client = atb_create();
	if (!client) return 2;
	ok = atb_connect_token_file(client, argv[1]) == ATB_OK
		&& atb_device_set(client, "vbxe", 1, "version=127") == ATB_OK
		&& atb_pause(client) == ATB_OK
		&& atb_vbxe(client) == ATB_OK
		&& strstr(atb_last_response(client), "\"version\":127")
		&& atb_vbxe_write(client, 0x7FFFC, source, sizeof source) == ATB_OK
		&& atb_vbxe_read(client, 0x7FFFC, sizeof result, result) == ATB_OK
		&& !memcmp(source, result, sizeof result)
		&& atb_vbxe_read(client, 0x7FFFF, 2, result) == ATB_ERR_BAD_ARG
		&& atb_vbxe_write(client, 0, source, 0) == ATB_ERR_BAD_ARG;
	if (!ok) fprintf(stderr, "VBXE C SDK failed: %s\n", atb_last_error(client));
	atb_close(client);
	return ok ? 0 : 1;
}
