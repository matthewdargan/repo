#ifndef ACME_CHALLENGE_H
#define ACME_CHALLENGE_H

////////////////////////////////
//~ ACME Challenge Types

typedef struct ACME_ChallengeInfo ACME_ChallengeInfo;
struct ACME_ChallengeInfo
{
	String8 type;
	String8 url;
	String8 token;
};

////////////////////////////////
//~ ACME Challenge Functions

internal ACME_ChallengeInfo acme_get_http01_challenge(ACME_Client *client, String8 authz_url);
internal b32 acme_notify_challenge_ready(ACME_Client *client, String8 challenge_url);
internal b32 acme_poll_challenge_status(ACME_Client *client, String8 challenge_url, u64 max_attempts, u64 delay_ms);

#endif // ACME_CHALLENGE_H
