 292:	d3a2      	bcc.n	1da <mbedtls_ssl_handshake_client_step+0x1da>
 294:	1cae      	adds	r6, r5, #2
 296:	6c20      	ldr	r0, [r4, #64]	@ 0x40
 298:	2220      	movs	r2, #32
 29a:	4631      	mov	r1, r6
 29c:	f500 7039 	add.w	r0, r0, #740	@ 0x2e4
 2a0:	f7ff fffe 	bl	0 <memcpy>
			2a0: R_ARM_THM_CALL	memcpy
 2a4:	6c20      	ldr	r0, [r4, #64]	@ 0x40
 2a6:	2220      	movs	r2, #32
 2a8:	4631      	mov	r1, r6
 2aa:	f500 7039 	add.w	r0, r0, #740	@ 0x2e4
 2ae:	f7ff fffe 	bl	0 <memcmp>
			2ae: R_ARM_THM_CALL	memcmp
 2b2:	b198      	cbz	r0, 2dc <mbedtls_ssl_handshake_client_step+0x2dc>
 2b4:	6c23      	ldr	r3, [r4, #64]	@ 0x40
 2b6:	4632      	mov	r2, r6
 2b8:	4947      	ldr	r1, [pc, #284]	@ (3d8 <mbedtls_ssl_handshake_client_step+0x3d8>)
 2ba:	2003      	movs	r0, #3
 2bc:	f503 7339 	add.w	r3, r3, #740	@ 0x2e4
 2c0:	f7ff fffe 	bl	0 <syslog>
