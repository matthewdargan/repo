// clang-format off
#include "base/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "auth/inc.c"
// clang-format on

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	(void)cmd_line;

	Arena *arena = arena_alloc();
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	log_info(str8_lit("9auth: test harness\n"));
	log_info(str8_lit("========================================\n"));

	////////////////////////////////
	//~ Test: Key Ring Operations

	log_info(str8_lit("Testing key ring operations...\n"));

	Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

	Auth_Key test_key = {0};
	test_key.user = str8_lit("testuser");
	test_key.rp_id = str8_lit("9p.localhost");
	test_key.credential_id_len = 16;
	for(u64 i = 0; i < 16; i += 1)
	{
		test_key.credential_id[i] = (u8)i;
	}
	test_key.public_key_len = 32;
	for(u64 i = 0; i < 32; i += 1)
	{
		test_key.public_key[i] = (u8)(i + 100);
	}

	auth_keyring_add(&ring, &test_key);
	log_info(str8_lit("✓ Added test key\n"));

	Auth_Key *found = auth_keyring_lookup(&ring, str8_lit("testuser"), str8_lit("9p.localhost"));
	log_infof("✓ Key lookup %s\n", found ? "success" : "failed");

	////////////////////////////////
	//~ Test: Key Ring Serialization

	log_info(str8_lit("\nTesting key ring serialization...\n"));

	String8 saved = auth_keyring_save(arena, &ring);
	log_infof("Serialized keyring (%llu bytes):\n", saved.size);
	log_infof("%S\n", saved);

	Auth_KeyRing ring2 = auth_keyring_alloc(arena, 0);
	if(auth_keyring_load(arena, &ring2, saved))
	{
		log_info(str8_lit("✓ Deserialization success\n"));
		Auth_Key *found2 = auth_keyring_lookup(&ring2, str8_lit("testuser"), str8_lit("9p.localhost"));
		if(found2)
		{
			log_info(str8_lit("✓ Key found in loaded ring\n"));
		}
	}
	else
	{
		log_info(str8_lit("✗ Deserialization failed\n"));
	}

	////////////////////////////////
	//~ Test: Conversation Management

	log_info(str8_lit("\nTesting conversation management...\n"));

	Auth_Conv *conv = auth_conv_alloc(arena, 1, str8_lit("testuser"), str8_lit("testserver"));
	u64 now = os_now_microseconds();
	b32 expired = auth_conv_is_expired(conv, now, 300);
	log_infof("✓ Conversation %s (expected not expired)\n", expired ? "expired" : "not expired");

	////////////////////////////////
	//~ Test: FIDO2 Challenge Generation

	log_info(str8_lit("\nTesting FIDO2 challenge generation...\n"));

	u8 challenge[32];
	if(auth_fido2_generate_challenge(challenge))
	{
		log_info(str8_lit("✓ Challenge generated: "));
		for(u64 i = 0; i < 32; i += 1)
		{
			log_infof("%02x", challenge[i]);
		}
		log_info(str8_lit("\n"));
	}
	else
	{
		log_info(str8_lit("✗ Challenge generation failed\n"));
	}

	////////////////////////////////
	//~ Test: FIDO2 Device Enumeration

	log_info(str8_lit("\nTesting FIDO2 device enumeration...\n"));

	Auth_Fido2_DeviceList devices = auth_fido2_enumerate_devices(arena);
	log_infof("Found %llu FIDO2 device(s)\n", devices.count);

	for(Auth_Fido2_DeviceInfo *dev = devices.first; dev != 0; dev = dev->next)
	{
		log_infof("Device: %S\n", dev->path);
		log_infof("Product: %S\n", dev->product);
		log_infof("Manufacturer: %S\n", dev->manufacturer);
		log_infof("VID:PID: %04x:%04x\n", dev->vendor_id, dev->product_id);
	}

	if(devices.count > 0)
	{
		log_info(str8_lit("✓ Device enumeration successful\n"));
	}
	else
	{
		log_info(str8_lit("⚠ No devices found (Yubikey not connected?)\n"));
	}

	////////////////////////////////
	//~ Test: FIDO2 Credential Registration (Interactive)

	log_info(str8_lit("\nTesting FIDO2 credential registration...\n"));

	if(devices.count > 0)
	{
		log_info(str8_lit("This test requires user interaction (touch Yubikey)\n"));
		log_info(str8_lit("Registering credential for user 'testuser' with rp_id '9p.localhost'\n"));
		log_scope_flush(arena); // Flush now so user sees the prompt
		log_scope_begin();      // Begin new scope after flush

		Auth_Fido2_RegisterParams reg_params = {0};
		reg_params.user = str8_lit("testuser");
		reg_params.rp_id = str8_lit("9p.localhost");
		reg_params.rp_name = str8_lit("9P Filesystem Test");
		reg_params.require_uv = 0;

		Auth_Key new_key = {0};
		String8 error = {0};

		if(auth_fido2_register_credential(arena, &reg_params, &new_key, &error))
		{
			log_info(str8_lit("✓ Credential registration successful\n"));
			log_infof("User: %S\n", new_key.user);
			log_infof("RP ID: %S\n", new_key.rp_id);
			log_infof("Credential ID length: %llu\n", new_key.credential_id_len);
			log_infof("Public key length: %llu\n", new_key.public_key_len);
		}
		else
		{
			log_infof("✗ Credential registration failed: %S\n", error);
		}
	}
	else
	{
		log_info(str8_lit("⊘ Skipping credential registration (no devices)\n"));
	}

	////////////////////////////////
	//~ Summary

	log_info(str8_lit("\n========================================\n"));
	log_info(str8_lit("9auth: test suite complete\n"));

	log_scope_flush(arena);
	log_release(log);
}
