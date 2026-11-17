; REQUIRES: mera400-registered-target
; RUN: llvm-mc -triple=mera400-unknown-none -show-encoding %s | FileCheck %s

; Expected words come from the EMAS acceptance fixtures (tests/acceptance/args/
; shc, hlt, blc, brc, exl, nrf) and the instruction table in src/keywords.c at
; https://github.com/jakubfi/emas/tree/fe06a31b5d11f6037d77c84c2f2833c56c67cfa4

	shc r0, 1
; CHECK: shc{{[ \t]+}}r0, 1{{.*}}encoding: [0xe8,0x11]
	shc r7, 8
; CHECK: shc{{[ \t]+}}r7, 8{{.*}}encoding: [0xeb,0xd0]
	shc r0, 15
; CHECK: shc{{[ \t]+}}r0, 15{{.*}}encoding: [0xea,0x17]
	shc r1, 0
; CHECK: shc{{[ \t]+}}r1, 0{{.*}}encoding: [0xe8,0x50]
	shc r2, -1
; CHECK: shc{{[ \t]+}}r2, -1{{.*}}encoding: [0xea,0x97]
	shc r5, -8
; CHECK: shc{{[ \t]+}}r5, -8{{.*}}encoding: [0xeb,0x50]
	hlt
; CHECK: hlt{{[ \t]+}}0{{.*}}encoding: [0xec,0x00]
	hlt 1
; CHECK: hlt{{[ \t]+}}1{{.*}}encoding: [0xec,0x01]
	hlt 127
; CHECK: hlt{{[ \t]+}}127{{.*}}encoding: [0xee,0x3f]
	blc 0
; CHECK: blc{{[ \t]+}}0{{.*}}encoding: [0xe4,0x00]
	blc 256
; CHECK: blc{{[ \t]+}}256{{.*}}encoding: [0xe4,0x01]
	blc 65280
; CHECK: blc{{[ \t]+}}65280{{.*}}encoding: [0xe4,0xff]
	brc 0
; CHECK: brc{{[ \t]+}}0{{.*}}encoding: [0xe6,0x00]
	brc 255
; CHECK: brc{{[ \t]+}}255{{.*}}encoding: [0xe6,0xff]
	exl 0
; CHECK: exl{{[ \t]+}}0{{.*}}encoding: [0xe5,0x00]
	exl 255
; CHECK: exl{{[ \t]+}}255{{.*}}encoding: [0xe5,0xff]
	nrf
; CHECK: nrf{{[ \t]+}}255{{.*}}encoding: [0xe7,0xff]
	nrf 0
; CHECK: nrf{{[ \t]+}}0{{.*}}encoding: [0xe7,0x00]
	nrf 1
; CHECK: nrf{{[ \t]+}}1{{.*}}encoding: [0xe7,0x01]
	mcl
; CHECK: mcl{{.*}}encoding: [0xec,0x40]
	cit
; CHECK: cit{{.*}}encoding: [0xec,0x80]
	sil
; CHECK: sil{{.*}}encoding: [0xec,0x81]
	siu
; CHECK: siu{{.*}}encoding: [0xec,0x82]
	sit
; CHECK: sit{{.*}}encoding: [0xec,0x83]
	giu
; CHECK: giu{{.*}}encoding: [0xec,0xc0]
	lip
; CHECK: lip{{.*}}encoding: [0xed,0x00]
	gil
; CHECK: gil{{.*}}encoding: [0xee,0xc0]
	ric r1
; CHECK: ric{{[ \t]+}}r1{{.*}}encoding: [0xe8,0x40]
	rpc r2
; CHECK: rpc{{[ \t]+}}r2{{.*}}encoding: [0xe8,0x89]
	rky r3
; CHECK: rky{{[ \t]+}}r3{{.*}}encoding: [0xea,0xc0]
	lpc r4
; CHECK: lpc{{[ \t]+}}r4{{.*}}encoding: [0xeb,0x09]
