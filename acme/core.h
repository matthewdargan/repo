#ifndef ACME_CORE_H
#define ACME_CORE_H

////////////////////////////////
//~ Base64 URL Encoding

read_only global u8 base64[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_',
};

internal String8 base64url_encode(Arena *arena, String8 data);

////////////////////////////////
//~ JSON Web Key

internal JSON_Value *acme_jwk_from_key(Arena *arena, EVP_PKEY *pkey);

////////////////////////////////
//~ JSON Web Signature

internal String8 acme_jws_sign(Arena *arena, EVP_PKEY *pkey, JSON_Value *protected_header, String8 payload);

#endif // ACME_CORE_H
