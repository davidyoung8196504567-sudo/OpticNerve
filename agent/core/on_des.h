/* on_des.h: DES (FIPS 46-3), encryption only, and the VNC authentication response. */
#ifndef ON_DES_H
#define ON_DES_H

#include <stdint.h>

/* Encrypts one 8-byte block with an 8-byte key (parity bits ignored). */
void on_des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);

/* The VncAuth response: the 16-byte challenge encrypted as two blocks with the password's 8 bytes, each byte used
 * with its bits in reverse order, as every VNC client and server does (RFC 6143 does not say so). */
void on_vnc_response(const uint8_t password[8], const uint8_t challenge[16], uint8_t response[16]);

/* The 8 password bytes VncAuth uses: the first 8 characters, padded with zero bytes. */
void on_vnc_password(const char *text, uint8_t password[8]);

#endif
