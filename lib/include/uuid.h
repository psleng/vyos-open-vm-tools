/*********************************************************
 * Copyright (C) 1998-2015 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * uuid.h --
 *
 *      UUID generation
 */

#ifndef _UUID_H_
#define _UUID_H_

#ifdef __cplusplus
extern "C"{
#endif

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#define UUID_SIZE 16
#define UUID_STRSIZE (2*UUID_SIZE + 1)
#define	UUID_MAXLEN 48

typedef enum {
   UUID_WITH_PATH = 0,
   UUID_RANDOM,
   UUID_VPX_BIOS,
   UUID_VPX_INSTANCE,
   UUID_UNKNOWN
} UUIDStyle;

/* Scheme control */
#define UUID_CREATE_WS4     0  /* the "original", WS4 and earlier scheme */
#define UUID_CREATE_WS5     1  /* the WS5 scheme */
#define UUID_CREATE_WS6     2  /* the WS6 scheme - "native" path */
#define UUID_CREATE_ESX50   3  /* the scheme to allow location generated using
                                  host UUID with wrong endianness as reported by
                                  pre-ESX 5.0 U2. See PR 861271 for details. */
#define UUID_CREATE_WS65    4  /* the WS65 scheme - UTF-8 path */
#define UUID_CREATE_CURRENT 4  /* the current scheme - always the latest */


/*
 * An RFC 4122-compliant UUID.
 *
 * See RFC 4122 section 4.1.2 (Layout and Byte Order). All multi-byte types are
 * stored in big-endian (most significant byte first) order.
 *
 * The UUID packed text string
 *    00112233-4455-6677-8899-AABBCCDDEEFF
 * represents
 *    timeLow = 0x00112233
 *    timeMid = 0x4455
 *    timeHiAndVersion = 0x6677
 *    clockSeqHiAndReserved = 0x88
 *    clockSeqLow = 0x99
 *    node[0] = 0xAA
 *    node[1] = 0xBB
 *    node[2] = 0xCC
 *    node[3] = 0xDD
 *    node[4] = 0xEE
 *    node[5] = 0xFF
 * and the structure is stored as the sequence of bytes
 *    00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 timeLow;
   uint16 timeMid;
   uint16 timeHiAndVersion;
   uint8  clockSeqHiAndReserved;
   uint8  clockSeqLow;
   uint8  node[6];
}
#include "vmware_pack_end.h"
UUIDRFC4122;


/*
 * An EFI/UEFI/Microsoft-compliant GUID.
 *
 * See MdeModulePkg/Universal/DevicePathDxe/DevicePathFromText.c::StrToGuid(),
 * BaseTools/Source/C/Common/ParseInf.c::StringToGuid(),
 * http://en.wikipedia.org/wiki/GUID . All multi-byte types are stored in CPU
 * (a.k.a. native) byte order.
 *
 * The GUID packed text string
 *    00112233-4455-6677-8899-AABBCCDDEEFF
 * represents
 *    data1 = 0x00112233
 *    data2 = 0x4455
 *    data3 = 0x6677
 *    data4[0] = 0x88
 *    data4[1] = 0x99
 *    data4[2] = 0xAA
 *    data4[3] = 0xBB
 *    data4[4] = 0xCC
 *    data4[5] = 0xDD
 *    data4[6] = 0xEE
 *    data4[7] = 0xFF
 * and the structure is stored as the sequence of bytes
 *       big-endian CPU: 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
 *    little-endian CPU: 33 22 11 00 55 44 77 66 88 99 AA BB CC DD EE FF
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 data1;
   uint16 data2;
   uint16 data3;
   uint8  data4[8];
}
#include "vmware_pack_end.h"
EFIGUID;


Bool UUID_ConvertToBin(uint8 dest_id[UUID_SIZE], const char *text);
char *UUID_ConvertToText(const uint8 id[UUID_SIZE]);

char *UUID_Create(const char *configFileFullPath, int schemeControl);
char *UUID_CreateRandom(void);
void UUID_CreateRandomRFC4122V4(UUIDRFC4122 *id);
void UUID_CreateRandomEFI(EFIGUID *id);
char *UUID_CreateRandomVpxStyle(uint8 vpxdId, UUIDStyle);

Bool UUID_IsUUIDGeneratedByThatVpxd(const uint8 *id, int vpxdInstanceId);
char *UUID_PackText(const char *text, char *pack, int packLen);
char *UUID_ProperHostUUID(void);
char *UUID_GetHostUUID(void);
UUIDStyle UUID_GetStyle(const uint8 *id);
/* like UUID_GetHostUUID, except gets actual host UUID */
char *UUID_GetRealHostUUID(void);

#ifdef __cplusplus
} // extern "C" {
#endif

#endif
