#ifndef ACME_ORDER_H
#define ACME_ORDER_H

////////////////////////////////
//~ ACME Order Functions

internal String8 acme_create_order(ACME_Client *client, String8 domain);
internal String8 acme_get_authorization_url(ACME_Client *client, String8 order_url);
internal String8 acme_finalize_order(ACME_Client *client, String8 order_url, EVP_PKEY *cert_key, String8 domain);
internal b32 acme_poll_order_status(ACME_Client *client, String8 order_url, u64 max_attempts, u64 delay_ms);
internal String8 acme_download_certificate(ACME_Client *client, String8 order_url);

#endif // ACME_ORDER_H
