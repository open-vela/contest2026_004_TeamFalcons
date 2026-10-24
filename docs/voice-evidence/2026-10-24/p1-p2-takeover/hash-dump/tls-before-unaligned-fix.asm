 252:	8058      	strh	r0, [r3, #2]
 254:	6823      	ldr	r3, [r4, #0]
 256:	885a      	ldrh	r2, [r3, #2]
 258:	4282      	cmp	r2, r0
 25a:	d8a7      	bhi.n	1ac <mbedtls_ssl_handshake_client_step+0x1ac>
 25c:	881b      	ldrh	r3, [r3, #0]
 25e:	4283      	cmp	r3, r0
 260:	d3a4      	bcc.n	1ac <mbedtls_ssl_handshake_client_step+0x1ac>
 262:	1cae      	adds	r6, r5, #2
 264:	6c23      	ldr	r3, [r4, #64]	@ 0x40
 266:	f105 0122 	add.w	r1, r5, #34	@ 0x22
 26a:	4632      	mov	r2, r6
 26c:	f503 7339 	add.w	r3, r3, #740	@ 0x2e4
 270:	f852 0b04 	ldr.w	r0, [r2], #4
 274:	428a      	cmp	r2, r1
 276:	f843 0b04 	str.w	r0, [r3], #4
 27a:	d1f9      	bne.n	270 <mbedtls_ssl_handshake_client_step+0x270>
 27c:	6c20      	ldr	r0, [r4, #64]	@ 0x40
 27e:	2220      	movs	r2, #32
 280:	4631      	mov	r1, r6
 282:	f500 7039 	add.w	r0, r0, #740	@ 0x2e4
 286:	f7ff fffe 	bl	0 <memcmp>
			286: R_ARM_THM_CALL	memcmp
 28a:	b198      	cbz	r0, 2b4 <mbedtls_ssl_handshake_client_step+0x2b4>
 28c:	6c23      	ldr	r3, [r4, #64]	@ 0x40
 28e:	4632      	mov	r2, r6
 290:	4947      	ldr	r1, [pc, #284]	@ (3b0 <mbedtls_ssl_handshake_client_step+0x3b0>)
 292:	2003      	movs	r0, #3
 294:	f503 7339 	add.w	r3, r3, #740	@ 0x2e4
 298:	f7ff fffe 	bl	0 <syslog>
			298: R_ARM_THM_CALL	syslog
 29c:	2220      	movs	r2, #32
 29e:	4631      	mov	r1, r6
 2a0:	4844      	ldr	r0, [pc, #272]	@ (3b4 <mbedtls_ssl_handshake_client_step+0x3b4>)
 2a2:	f7ff fffe 	bl	0 <mbedtls_ssl_handshake_client_step>
