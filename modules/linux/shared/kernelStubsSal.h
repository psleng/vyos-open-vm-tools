/*********************************************************
 * Copyright (C) 2014 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *********************************************************/

/*
 * kernelStubsSal.h
 *
 * Contains definitions source annotation language definitions for kernel drivers.
 * This solves two issues:
 * 1. Microsoft changed their annotation language from SAL 1.0 (original one
 *    widely distributed by the Windows team) to their more final SAL 2.0
 *    langauge (championed by the VS team). We target multiple versions of
 *    Driver Kits, so we have to map 2.0 to 1.0.
 * 2. We want these annotations to do nothing during non-Win32 compiles.
 *
 * A longer term goal is to rationalize this into Bora.
 */
#ifndef __KERNELSTUBSSAL_H__
#define __KERNELSTUBSSAL_H__

#if defined(_WIN32)
#  include <DriverSpecs.h>
#  if !defined(_SAL_VERSION)
#     define _SAL_VERSION 10
#  endif
#endif

#if !defined(_SAL_VERSION)
#define _In_
#define _In_z_
#define _In_opt_
#define _In_opt_z_
#define _Out_
#define _Out_opt_
#define _Out_z_cap_(e)
#define _Inout_
#define _Inout_z_cap_(e)
#define _Post_z_count_(e)
#define _Ret_writes_z_(e)
#define _Ret_writes_maybenull_z_(e)
#define _Ret_maybenull_z_
#define _Success_(expr)
#define _Check_return_
#define _Must_inspect_result_
#define _Group_(annos)
#define _When_(expr, annos)
#define _Printf_format_string_
#define _Use_decl_annotations_
#elif defined(_SAL_VERSION) && _SAL_VERSION == 10
// Microsoft didn't create a header mapping SAL 2.0 to 1.0. We do that here.
#define _In_                           __in
#define _In_z_                         __in_z
#define _In_opt_                       __in_opt
#define _In_opt_z_                     __in_z_opt
#define _Out_                          __out
#define _Out_opt_                      __out_opt
#define _Out_z_cap_(expr)              __out_ecount_z(expr)
#define _Inout_                        __inout
#define _Inout_z_cap_(expr)            __inout_ecount_z(expr)
#define _Post_z_count_(expr)
#define _Ret_writes_z_(expr)           __out_ecount_z(expr)
#define _Ret_writes_maybenull_z_(expr) __out_ecount_z_opt(expr)
#define _Ret_maybenull_z_              __out_z
#define _Check_return_                 __checkReturn
#define _Must_inspect_result_          __checkReturn
#define _Success_(annos)               __success(annos)
#define _Printf_format_string_         __format_string
#define _Use_decl_annotations_

// DriverSpecs.h was pretty much empty until the DDK that defined
// NTDDK_WIN6SP1 appeared.
#if defined(NTDDI_WIN6SP1)
#define _Group_(annos)              __$drv_group(annos)
#define _When_(expr, annos)         __drv_when(expr, annos)
#define _IRQL_requires_max_(irql)   __drv_maxIRQL(irql)
#else
#define _Group_(annos)
#define _When_(expr, annos)
#define _IRQL_requires_max_(irql)
#define __drv_allocatesMem(kind)
#define __drv_freesMem(kind)
#endif

#else
// Sal 2.0 path - everything is already defined.
#endif // _SAL_VERSION

// Now define our own annotations
#if !defined(_SAL_VERSION)
#define _When_windrv_(annos)
#define _Ret_allocates_malloc_mem_opt_bytecap_(_Size)
#define _Ret_allocates_malloc_mem_opt_bytecount_(_Size)
#define _Ret_allocates_malloc_mem_opt_bytecap_post_bytecount_(_Cap,_Count)
#define _Ret_allocates_malloc_mem_opt_z_bytecount_(_Size)
#define _Ret_allocates_malloc_mem_opt_z_
#define _In_frees_malloc_mem_opt_
#elif defined(_SAL_VERSION) && _SAL_VERSION == 10
#define _When_windrv_(annos)                                               annos
#define _Ret_allocates_malloc_mem_opt_bytecap_(_Cap)                       __drv_allocatesMem("Memory") __checkReturn __post __byte_writableTo(_Cap) __exceptthat __maybenull
#define _Ret_allocates_malloc_mem_opt_bytecount_(_Count)                   __drv_allocatesMem("Memory") __checkReturn __post __byte_readableTo(_Count) __exceptthat __maybenull
#define _Ret_allocates_malloc_mem_opt_bytecap_post_bytecount_(_Cap,_Count) __drv_allocatesMem("Memory") __checkReturn __post __byte_writableTo(_Cap) __byte_readableTo(_Count) __exceptthat __maybenull
#define _Ret_allocates_malloc_mem_opt_z_bytecount_(_Count)                 __drv_allocatesMem("Memory") __checkReturn __post __byte_readableTo(_Count) __valid __nullterminated __exceptthat __maybenull
#define _Ret_allocates_malloc_mem_opt_z_                                   __drv_allocatesMem("Memory") __checkReturn __post __valid __nullterminated __exceptthat __maybenull
#define _In_frees_malloc_mem_opt_                                          __drv_freesMem("Memory") __in_opt __post __notvalid
#else
#define _When_windrv_(annos)                                               annos
#define _Ret_allocates_malloc_mem_opt_bytecap_(_Cap)                       __drv_allocatesMem("Memory") _Must_inspect_result_ _Ret_opt_bytecap_(_Cap)
#define _Ret_allocates_malloc_mem_opt_bytecount_(_Count)                   __drv_allocatesMem("Memory") _Must_inspect_result_ _Ret_opt_bytecount_(_Count)
#define _Ret_allocates_malloc_mem_opt_bytecap_post_bytecount_(_Cap,_Count) __drv_allocatesMem("Memory") _Must_inspect_result_ _Ret_opt_bytecap_(_Cap) _Ret_opt_bytecount_(_Count)
#define _Ret_allocates_malloc_mem_opt_z_bytecount_(_Count)                 __drv_allocatesMem("Memory") _Must_inspect_result_ _Ret_opt_z_bytecount_(_Count)
#define _Ret_allocates_malloc_mem_opt_z_                                   __drv_allocatesMem("Memory") _Must_inspect_result_ _Ret_opt_z_
#define _In_frees_malloc_mem_opt_                                          __drv_freesMem("Memory") _Pre_maybenull_ _Post_invalid_
#endif // _SAL_VERSION

// Best we can do for reallocate with simple annotations: assume old size was fully initialized.
#define _Ret_reallocates_malloc_mem_opt_newbytecap_oldbytecap_(_NewSize, _OldSize) _Ret_allocates_malloc_mem_opt_bytecap_post_bytecount_(_NewSize, _OldSize <= _NewSize ? _OldSize : _NewSize)
#define _Ret_reallocates_malloc_mem_opt_newbytecap_(_NewSize)                      _Ret_allocates_malloc_mem_opt_z_bytecount_(_NewSize)
#define _In_reallocates_malloc_mem_opt_oldptr_                                     _In_frees_malloc_mem_opt_

#endif // __KERNELSTUBSSAL_H__
