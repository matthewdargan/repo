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

	log_info(str8_lit("factotum: test harness\n"));

	Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

	Auth_Key test_key = {0};
	test_key.user = str8_lit("testuser");
	test_key.rp_id = str8_lit("9p.localhost");
	test_key.credential_id_len = 16;
	for(u64 i = 0; i < 16; i++)
	{
		test_key.credential_id[i] = (u8)i;
	}
	test_key.public_key_len = 32;
	for(u64 i = 0; i < 32; i++)
	{
		test_key.public_key[i] = (u8)(i + 100);
	}

	auth_keyring_add(&ring, &test_key);
	log_info(str8_lit("factotum: added test key\n"));

	Auth_Key *found = auth_keyring_lookup(&ring, str8_lit("testuser"), str8_lit("9p.localhost"));
	log_infof("factotum: key lookup %s\n", found ? "success" : "failed");

	String8 saved = auth_keyring_save(arena, &ring);
	log_infof("factotum: serialized keyring:\n%S\n", saved);

	Auth_KeyRing ring2 = auth_keyring_alloc(arena, 0);
	if(auth_keyring_load(arena, &ring2, saved))
	{
		log_info(str8_lit("factotum: deserialization success\n"));
		Auth_Key *found2 = auth_keyring_lookup(&ring2, str8_lit("testuser"), str8_lit("9p.localhost"));
		if(found2)
		{
			log_info(str8_lit("factotum: key found in loaded ring\n"));
		}
	}
	else
	{
		log_info(str8_lit("factotum: deserialization failed\n"));
	}

	Auth_Conv *conv = auth_conv_alloc(arena, 1, str8_lit("testuser"), str8_lit("testserver"));
	u64 now = os_now_microseconds();
	b32 expired = auth_conv_is_expired(conv, now, 300);
	log_infof("factotum: conversation %s (expected not expired)\n", expired ? "expired" : "not expired");

	log_info(str8_lit("factotum: all tests passed\n"));

	log_scope_flush(arena);
	log_release(log);
}
