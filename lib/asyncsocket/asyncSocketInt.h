/*********************************************************
 * Copyright (C) 2011,2014-2015 VMware, Inc. All rights reserved.
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

/*********************************************************
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License (the "License") version 1.0
 * and no later version.  You may not use this file except in
 * compliance with the License.
 *
 * You can obtain a copy of the License at
 *         http://www.opensource.org/licenses/cddl1.php
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 *********************************************************/

#ifndef __ASYNC_SOCKET_INT_H__
#define __ASYNC_SOCKET_INT_H__

/*
 * asyncsocket.h --
 *
 *      The AsyncSocket object is a fairly simple wrapper around a basic TCP
 *      socket. It's potentially asynchronous for both read and write
 *      operations. Reads are "requested" by registering a receive function
 *      that is called once the requested amount of data has been read from
 *      the socket. Similarly, writes are queued along with a send function
 *      that is called once the data has been written. Errors are reported via
 *      a separate callback.
 */

#define INCLUDE_ALLOW_VMCORE
#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#ifdef _WIN32
/*
 * We redefine strcpy/strcat because the Windows SDK uses it for getaddrinfo().
 * When we upgrade SDKs, this redefinition can go away.
 * Note: Now we are checking if we have secure libs for string operations
 */
#if !(defined(__GOT_SECURE_LIB__) && __GOT_SECURE_LIB__ >= 200402L)
#define strcpy(dst,src) Str_Strcpy((dst), (src), 0x7FFFFFFF)
#define strcat(dst,src) Str_Strcat((dst), (src), 0x7FFFFFFF)
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#include <MSWSock.h>
#include <windows.h>
#if !(defined(__GOT_SECURE_LIB__) && __GOT_SECURE_LIB__ >= 200402L)
#undef strcpy
#undef strcat
#endif
#else
#include <stddef.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "vmware.h"
#include "random.h"

#ifdef USE_SSL_DIRECT
#include "sslDirect.h"
#else
#include "ssl.h"
#endif

#ifdef _WIN32
#define ASOCK_LASTERROR()       WSAGetLastError()
#define ASOCK_ENOTCONN          WSAENOTCONN
#define ASOCK_ENOTSOCK          WSAENOTSOCK
#define ASOCK_EADDRINUSE        WSAEADDRINUSE
#define ASOCK_ECONNECTING       WSAEWOULDBLOCK
#define ASOCK_EWOULDBLOCK       WSAEWOULDBLOCK
#else
#define ASOCK_LASTERROR()       errno
#define ASOCK_ENOTCONN          ENOTCONN
#define ASOCK_ENOTSOCK          ENOTSOCK
#define ASOCK_EADDRINUSE        EADDRINUSE
#define ASOCK_ECONNECTING       EINPROGRESS
#define ASOCK_EWOULDBLOCK       EWOULDBLOCK
#endif

typedef enum {
   WEB_SOCKET_FRAME_OPCODE_BINARY = 0x02,
   WEB_SOCKET_FRAME_OPCODE_CLOSE = 0x08,
} WebSocketFrameOpcode;

typedef enum {
   WEB_SOCKET_STATE_CONNECTING  = 0,
   WEB_SOCKET_STATE_OPEN        = 1,
   WEB_SOCKET_STATE_CLOSING     = 2,
   WEB_SOCKET_STATE_CLOSED      = 3,
} WebSocketState;

typedef enum {
   WEB_SOCKET_NEED_FRAME_TYPE          = 0,
   WEB_SOCKET_NEED_FRAME_SIZE          = 1,
   WEB_SOCKET_NEED_EXTENDED_FRAME_SIZE = 2,
   WEB_SOCKET_NEED_FRAME_MASK          = 3,
   WEB_SOCKET_NEED_FRAME_DATA          = 4,
} WebSocketDecodeState;

/*
 * Bitmask indicates when masking should be applyed or removed,
 * None at all (rare - RFC 6455 expects masking in at least one
 * direction), applied to frames we receive, or applied to frames
 * we sent. Both is possible (but again rare - RFC 6455 indicates
 * masking is only required on frames from the client/browser to the
 * server.
 */
typedef enum {
   WEB_SOCKET_MASKING_NONE = 0,
   WEB_SOCKET_MASKING_RECV = 1,
   WEB_SOCKET_MASKING_SEND = 1 << 1,
} WebSocketMaskingRequired;

typedef enum {
   ASYNCSOCKET_TYPE_SOCKET =    0,
   ASYNCSOCKET_TYPE_NAMEDPIPE = 1,
} AsyncSocketType;

/*
 * Output buffer list data type, for the queue of outgoing buffers
 */
typedef struct SendBufList {
   struct SendBufList   *next;
   void                 *buf;
   int                   len;
   AsyncSocketSendFn     sendFn;
   void                 *clientData;
   /*
    * If the data needs to be encoded somehow before sending over the
    * wire, this will point to an internally-allocated buffer
    * containing the encoded version of buf.  The len field above will
    * hold the length of the encoded data.
    */
   char                 *encodedBuf;
} SendBufList;

struct AsyncSocket {
   uint32 id;
   AsyncSocketState state;
   int fd;
   SSLSock sslSock;
   AsyncSocketType asockType;
   const struct AsyncSocketVTable *vt;

   unsigned int refCount;
   int genericErrno;
   AsyncSocketErrorFn errorFn;
   void *errorClientData;
   VmTimeType drainTimeoutUS;

   struct sockaddr_storage localAddr;
   socklen_t localAddrLen;
   struct sockaddr_storage remoteAddr;
   socklen_t remoteAddrLen;

   AsyncSocketConnectFn connectFn;
   AsyncSocketRecvFn recvFn;
   AsyncSocketSslAcceptFn sslAcceptFn;
   void *clientData;       /* shared by recvFn, connectFn and sslAcceptFn */
   AsyncSocketPollParams pollParams;

   void *recvBuf;
   int recvPos;
   int recvLen;
   Bool recvCb;
   Bool recvCbTimer;
   Bool recvFireOnPartial;

   SendBufList *sendBufList;
   SendBufList **sendBufTail;
   int sendPos;
   Bool sendCb;
   Bool sendCbTimer;
   Bool sendCbRT;
   Bool sendBufFull;
   Bool sendLowLatency;

   Bool sslConnected;

   uint8 inIPollCb;
   Bool inRecvLoop;
   uint32 inBlockingRecv;

   AsyncSocket *listenAsock4;
   AsyncSocket *listenAsock6;

   struct {
      Bool expected;
      int fd;
   } passFd;

   struct {
      char *origin;
      char *host;
      char *hostname;
      char *uri;
      char *cookie;
      int version;
      WebSocketMaskingRequired maskingRequirements;
      WebSocketFrameOpcode frameOpcode;
      WebSocketState state;
      void *connectClientData;
      // Saved error reporting values.
      AsyncSocketErrorFn errorFn;
      void *errorClientData;
      char *socketBuffer;     // Accumulates incoming data (including framing etc.)
      char *decodeBuffer;     // Accumulates incoming data after removing framing
      int32 socketBufferWriteOffset;
      int32 socketBufferReadOffset;
      int32 decodeBufferWriteOffset;
      int32 decodeBufferReadOffset;
      size_t frameBytesRemaining;
      size_t frameSize;
      Bool maskPresent;
      uint8 maskBytes[4];
      uint8 maskOffset;
      const char **streamProtocols;    // null terminated list of protocols
      const char *streamProtocol;      // points to one of the above.
      WebSocketDecodeState decodeState;
      Bool useSSL;
      SSLVerifyParam *sslVerifyParam;  // Used for certificate verifications
      char *upgradeNonceBase64;
      rqContext *randomContext;
      uint16 closeStatus;
   } webSocket;

#ifdef _WIN32
   struct {
      char *pipeName;
      uint32 connectCount;
      HANDLE pipe;
      OVERLAPPED rd;
      OVERLAPPED wr;
   } namedPipe;
#endif

   struct {
      struct VSockSocket *socket;
      Bool signalCb;
      Bool sendCb;
      uint32 opMask;
      void *partialRecvBuf;
      uint32 partialRecvLen;
   } vmci;
};

typedef struct AsyncSocketVTable {
   void (*dispatchConnect)(AsyncSocket *asock, AsyncSocket *newsock);
   int (*prepareSend)(AsyncSocket *asock, void *buf, int len,
                      AsyncSocketSendFn sendFn, void *clientData,
                      Bool *bufferListWasEmpty);
   int (*send)(AsyncSocket *asock, Bool bufferListWasEmpty, void *buf, int len);
   int (*recv)(AsyncSocket *asock, void *buf, int len);
   PollerFunction sendCallback;
   PollerFunction recvCallback;
   Bool (*hasDataPending)(AsyncSocket *asock);
   void (*cancelListenCb)(AsyncSocket *asock);
   void (*cancelRecvCb)(AsyncSocket *asock);
   void (*cancelCbForClose)(AsyncSocket *asock);
   Bool (*cancelCbForConnectingClose)(AsyncSocket *asock);
   void (*close)(AsyncSocket *asock);
   void (*release)(AsyncSocket *asock);
} AsyncSocketVTable;

AsyncSocket *AsyncSocketInit(int socketFamily,
                             AsyncSocketPollParams *pollParams,
                             int *outError);
Bool AsyncSocketBind(AsyncSocket *asock, struct sockaddr_storage *addr,
                     socklen_t addrLen, int *outError);
Bool AsyncSocketListen(AsyncSocket *asock, AsyncSocketConnectFn connectFn,
                       void *clientData, int *outError);
int AsyncSocketResolveAddr(const char *hostname,
                           unsigned int port,
                           int family,
                           Bool passive,
                           struct sockaddr_storage *addr,
                           socklen_t *addrLen,
                           char **addrString);
AsyncSocket *AsyncSocketConnectWithAsock(AsyncSocket *asock,
                                         struct sockaddr_storage *addr,
                                         socklen_t addrLen,
                                         AsyncSocketConnectFn connectFn,
                                         void *clientData,
                                         PollerFunction internalConnectFn,
                                         AsyncSocketPollParams *pollParams,
                                         int *outError);
int AsyncSocketAddRef(AsyncSocket *s);
int AsyncSocketRelease(AsyncSocket *s, Bool unlock);
void AsyncSocketLock(AsyncSocket *asock);
void AsyncSocketUnlock(AsyncSocket *asock);
Bool AsyncSocketIsLocked(AsyncSocket *asock);
void AsyncSocketHandleError(AsyncSocket *asock, int asockErr);
int AsyncSocketFillRecvBuffer(AsyncSocket *s);
void AsyncSocketDispatchSentBuffer(AsyncSocket *s);
Bool AsyncSocketCheckAndDispatchRecv(AsyncSocket *s, int *error);
int AsyncSocketSendInternal(AsyncSocket *asock, void *buf, int len,
                            AsyncSocketSendFn sendFn, void *clientData,
                            Bool *bufferListWasEmpty);
int AsyncSocketSendSocket(AsyncSocket *asock, Bool bufferListWasEmpty,
                          void *buf, int len);
void AsyncSocketSendCallback(void *clientData);
AsyncSocket *AsyncSocketCreate(AsyncSocketPollParams *pollParams);
void AsyncSocketDispatchConnect(AsyncSocket *asock, AsyncSocket *newsock);
void AsyncSocketRecvCallback(void *clientData);
int AsyncSocketRecvSocket(AsyncSocket *asock, void *buf, int len);
void AsyncSocketCancelListenCbSocket(AsyncSocket *asock);
void AsyncSocketCancelRecvCbSocket(AsyncSocket *asock);
void AsyncSocketCancelCbForCloseSocket(AsyncSocket *asock);
Bool AsyncSocketCancelCbForConnectingCloseSocket(AsyncSocket *asock);
void AsyncSocketCloseSocket(AsyncSocket *asock);
#ifndef VMX86_TOOLS
void AsyncSocketInitWebSocket(AsyncSocket *asock,
                              void *clientData,
                              Bool useSSL,
                              const char *protocols[]);
#endif
AsyncSocket *AsyncSocketListenImpl(struct sockaddr_storage *addr,
                                   socklen_t addrLen,
                                   AsyncSocketConnectFn connectFn,
                                   void *clientData,
                                   AsyncSocketPollParams *pollParams,
                                   Bool isWebSock,
                                   Bool webSockUseSSL,
                                   const char *protocols[],
                                   int *outError);
AsyncSocket *AsyncSocketListenerCreate(const char *addrStr,
                                       unsigned int port,
                                       AsyncSocketConnectFn connectFn,
                                       void *clientData,
                                       AsyncSocketPollParams *pollParams,
                                       Bool isWebSock,
                                       Bool webSockUseSSL,
                                       const char *protocols[],
                                       int *outError);

#endif // __ASYNC_SOCKET_INT_H__
