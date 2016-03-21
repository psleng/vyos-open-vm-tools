/*********************************************************
 * Copyright (C) 2014-2015 VMware, Inc. All rights reserved.
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
 * sslDirect.c --
 *
 *      Mostly direct call stubs for AsyncSocket SSL functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include "str.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "log.h"
#include "debug.h"
#include "err.h"
#include "msg.h"
#include "sslDirect.h"
#include "vm_assert.h"

#define LOGLEVEL_MODULE SSLDirect
#include "loglevel_user.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/engine.h>

#define SSL_LOG(x) Debug x

struct SSLSockStruct {
   SSL *sslCnx;
   int fd;
   Bool encrypted;
   Bool closeFdOnShutdown;
   Bool connectionFailed;
#ifdef __APPLE__
   Bool loggedKernelReadBug;
#endif

   int sslIOError;
};


static Bool SSLModuleInitialized = FALSE;

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

enum {
   SSL_SOCK_WANT_RETRY,
   SSL_SOCK_LOST_CONNECTION,
};

/*
 *----------------------------------------------------------------------
 *
 * SSLPrintErrors
 *
 *    Print out all the errors in the SSL error queue.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    Clears out the SSL error stack
 *
 *----------------------------------------------------------------------
 */

static void
SSLPrintErrors(uint32 logLevel)  // IN:
{
   /*
    * Code inspection of the unsafe ERR_error_string function
    * implementation in crypto/err.c shows a static buffer of 256
    * characters. Presumably most messages will fit in that buffer.
    */
   enum { SSL_ERR_MAX_STRING = 256 };

   int errNum;
   char errString[SSL_ERR_MAX_STRING];
   while ((errNum = ERR_get_error())) {
      errString[0] = '\0';
      ERR_error_string_n(errNum, errString, ARRAYSIZE(errString));
      /* TODO: use LogV */
      if (logLevel == VMW_LOG_WARNING) {
         Warning("SSL Error: %s\n", errString);
      } else {
         Log("SSL Error: %s\n", errString);
      }
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSLSetSystemError
 *
 *    Maps the ssl error state into an appropriate errno / WSA error.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *----------------------------------------------------------------------
 */

static void
SSLSetSystemError(int err)
{
   switch (err) {
      case SSL_SOCK_WANT_RETRY:
#ifdef _WIN32
         WSASetLastError(WSAEWOULDBLOCK);
#else
         errno = EAGAIN;
#endif
         break;
      case SSL_SOCK_LOST_CONNECTION:
         /*
          * no good way to know what the real error was (could have been
          * a failure to load certificates in an accept), so return
          * something generic.
          */
#ifdef _WIN32
         WSASetLastError(WSAEACCES);
#else
         errno = EPERM;
#endif
         break;
      default:
         NOT_REACHED();
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SSLSetErrorState
 *
 *    Each ssl read / write could result in several reads and writes on
 *    the underlying socket.  In this case the actual value for errno
 *    will not be correct.  Manually setup the error value so that
 *    clients will do the right thing.
 *
 *    XXX: Mapping the SSL_ERROR_WANT_<something> errors to a single error code
 *    is not good. Applications using non-blocking IO would not know whether
 *    they should put the FD in a read wait or a write wait. Note that SSL_read
 *    can return SSL_ERROR_WANT_WRITE and SSL_write may return
 *    SSL_ERROR_WANT_READ.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    errno / windows error might be set.
 *
 *----------------------------------------------------------------------
 */

static int
SSLSetErrorState(SSL *ssl,
                 int result)
{
   int sslError = SSL_get_error(ssl, result);
   switch (sslError) {
      case SSL_ERROR_NONE:
         SSL_LOG(("SSL: action success, %d bytes\n", result));
         break;
      case SSL_ERROR_ZERO_RETURN:
         SSL_LOG(("SSL: Zero return\n"));
         break;
      case SSL_ERROR_WANT_READ:
         SSL_LOG(("SSL: Want read\n"));
         SSLSetSystemError(SSL_SOCK_WANT_RETRY);
         break;
      case SSL_ERROR_WANT_WRITE:
         SSL_LOG(("SSL: Want write\n"));
         SSLSetSystemError(SSL_SOCK_WANT_RETRY);
         break;
      case SSL_ERROR_WANT_X509_LOOKUP:
         SSL_LOG(("SSL: want x509 lookup\n"));
         break;
      case SSL_ERROR_SYSCALL:
         SSL_LOG(("SSL: syscall error\n"));
         SSLPrintErrors(VMW_LOG_INFO);
         if (result == 0) {
            Log("SSL: EOF in violation of protocol\n");
         } else {
            Log("SSL: syscall error %d: %s\n", Err_Errno(), Err_ErrString());
         }
         break;
      case SSL_ERROR_SSL:
         Warning("SSL: Unknown SSL Error\n");
         SSLPrintErrors(VMW_LOG_INFO);
         break;
   }
   return sslError;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Init --
 *
 *      Initializes the SSL library and prepares the session context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Lots.
 *----------------------------------------------------------------------
 */

void
SSL_Init(SSLLibFn *getLibFn,      // Ignored
         const char *defaultLib,  // Ignored
         const char *name)        // Ignored
{
   /*
    * Silently ignore any attempts to initialize module more than once.
    */
   if (SSLModuleInitialized) {
      return;
   }

   SSL_library_init();
   SSL_load_error_strings();

   OpenSSL_add_all_algorithms();

   /*
    * Force the PRNG to be initialized early, as opposed to at the
    * time when the SSL connection is made. A call to RAND_status
    * forces this initialization to happen. Initializing the PRNG
    * as early as possible in the process makes it take much less
    * time (e.g. 1sec. vs. sometimes 20sec.) compared to
    * initializing it later in the process, as may be the case on
    * the first SSL_accept() or SSL_connect(). That's because the
    * PRNG initialization walks the process heap and the total heap
    * is smaller at startup.
    *
    * If SSL_InitEx could not be called early enough in the
    * process, then the caller could just call RAND_status() by
    * itself. Only the first call to RAND_status will have the side
    * effect of initializing the PRNG, so calling it subsequently
    * would be a NOOP.
    */
   RAND_status();

   ENGINE_register_all_ciphers();
   ENGINE_register_all_digests();

   SSLModuleInitialized = TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_New()
 *
 * Results:
 *    Returns a freshly allocated SSLSock structure.
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

SSLSock
SSL_New(int fd,                       // IN
        Bool closeFdOnShutdown)       // IN
{
   SSLSock sslConnection;

   sslConnection = (SSLSock)calloc(1, sizeof(struct SSLSockStruct));
   VERIFY(sslConnection);
   sslConnection->fd = fd;
   sslConnection->closeFdOnShutdown = closeFdOnShutdown;

   return sslConnection;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetupAcceptWithContext()
 *
 *    Setting up the ssl connection and states to do a SSL accept operation
 *
 * Results:
 *    Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *    The server's certificate & private key may be loaded from disk.
 *
 *----------------------------------------------------------------------
 */

Bool SSL_SetupAcceptWithContext(SSLSock sSock, // IN: SSL socket
                                void *ctx)     // IN: OpenSSL context (SSL_CTX *)
{
   Bool ret = TRUE;

   ASSERT(SSLModuleInitialized);
   ASSERT(sSock);
   ASSERT(ctx);

   sSock->sslCnx = SSL_new(ctx);
   if (!sSock->sslCnx) {
      SSLPrintErrors(VMW_LOG_WARNING);
      Warning("Error Creating SSL connection structure\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_set_accept_state(sSock->sslCnx);

   SSL_LOG(("SSL: ssl created\n"));
   if (!SSL_set_fd(sSock->sslCnx, sSock->fd)) {
      SSLPrintErrors(VMW_LOG_WARNING);
      Warning("Error setting fd for SSL connection\n");
      sSock->connectionFailed = TRUE;
      ret = FALSE;
      goto end;
   }
   SSL_LOG(("SSL: fd set done\n"));

   sSock->encrypted = TRUE;

end:
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Read --
 *
 *    Functional equivalent of the read() syscall.
 *
 * Results:
 *    Returns the number of bytes read, or -1 on error.  The
 *    data read will be placed in buf.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

ssize_t
SSL_Read(SSLSock ssl,   // IN
         char *buf,         // OUT
         size_t num)        // IN
{
   int ret;
   ASSERT(ssl);

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }

   if (ssl->encrypted) {
      int result = SSL_read(ssl->sslCnx, buf, (int)num);

      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, result);
      if (ssl->sslIOError != SSL_ERROR_NONE) {
         SSL_LOG(("SSL: Read(%d, %p, %"FMTSZ"u): %d\n",
                  ssl->fd, buf, num, result));
         result = SOCKET_ERROR;
      }
      ret = result;
   } else {
      ret = SSLGeneric_read(ssl->fd, buf, (int)num);

#ifdef __APPLE__
      /*
       * Detect bug 161237 (Apple bug 5202831), which should no longer be
       * happening due to a workaround in our code.
       *
       * There is a bug on Mac OS 10.4 and 10.5 where passing an fd
       * over a socket can result in that fd being in an inconsistent state.
       * We can detect when this happens when read(2) returns zero
       * even if the other end of the socket is not disconnected.
       * We verify this by calling write(ssl->fd, "", 0) and
       * see if it is okay. (If the socket was really closed, it would
       * return -1 with errno==EPIPE.)
       */
      if (ret == 0) {
         ssize_t writeRet;
#ifdef VMX86_DEBUG
         struct stat statBuffer;

         /*
          * Make sure we're using a socket.
          */
         ASSERT((fstat(ssl->fd, &statBuffer) == 0) &&
                ((statBuffer.st_mode & S_IFSOCK) == S_IFSOCK));

#endif
         writeRet = write(ssl->fd, "", 0);
         if (writeRet == 0) {
            /*
             * The socket is still good. read(2) should not have returned zero.
             */
            if (! ssl->loggedKernelReadBug) {
               Log("Error: Encountered Apple bug #5202831.  Disconnecting.\n");
               ssl->loggedKernelReadBug = TRUE;
            }
         }
      }
#endif
   }

  end:
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_RecvDataAndFd --
 *
 *    recvmsg wrapper which can receive only file descriptors, not other
 *    control data.
 *
 * Results:
 *    Returns the number of bytes received, or -1 on error.  The
 *    data read will be placed in buf.  *fd is either -1 if no fd was
 *    received, or descriptor...
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

ssize_t
SSL_RecvDataAndFd(SSLSock ssl,    // IN/OUT: socket
                  char *buf,      // OUT: buffer
                  size_t num,     // IN: length of buffer
                  int *fd)        // OUT: descriptor received
{
   int ret;
   ASSERT(ssl);
   ASSERT(fd);

   *fd = -1;
   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }

   /*
    * No fd passing over SSL or Windows. Windows needs different code.
    */
#ifdef _WIN32
   return SSL_Read(ssl, buf, num);
#else
   if (ssl->encrypted) {
      int result = SSL_read(ssl->sslCnx, buf, (int)num);

      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, result);
      if (ssl->sslIOError != SSL_ERROR_NONE) {
         SSL_LOG(("SSL: Read(%d, %p, %"FMTSZ"u): %d\n",
                  ssl->fd, buf, num, result));
         result = SOCKET_ERROR;
      }
      ret = result;
   } else {
      struct iovec iov;
      struct msghdr msg = { 0 };
      uint8 cmsgBuf[CMSG_SPACE(sizeof(int))];

      iov.iov_base = buf;
      iov.iov_len = num;
      msg.msg_name = NULL;
      msg.msg_namelen = 0;
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = cmsgBuf;
      msg.msg_controllen = sizeof cmsgBuf;
      ret = SSLGeneric_recvmsg(ssl->fd, &msg, 0);
      if (ret >= 0 && msg.msg_controllen != 0) {
         struct cmsghdr *cmsg;

         for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
               int receivedFd = *(int *)CMSG_DATA(cmsg);

               ASSERT(*fd == -1);
               *fd = receivedFd;
            }
         }
      }
   }
#endif

  end:
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Write()
 *
 *    Functional equivalent of the write() syscall.
 *
 * Results:
 *    Returns the number of bytes written, or -1 on error.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

ssize_t
SSL_Write(SSLSock ssl,   // IN
          const char *buf,   // IN
          size_t num)        // IN
{
   int ret;
   ASSERT(ssl);

   if (ssl->connectionFailed) {
      SSLSetSystemError(SSL_SOCK_LOST_CONNECTION);
      ret = SOCKET_ERROR;
      goto end;
   }
   if (ssl->encrypted) {
      int result = SSL_write(ssl->sslCnx, buf, (int)num);

      ssl->sslIOError = SSLSetErrorState(ssl->sslCnx, result);
      if (ssl->sslIOError != SSL_ERROR_NONE) {
         SSL_LOG(("SSL: Write(%d)\n", ssl->fd));
         result = SOCKET_ERROR;
      }
      ret = result;
   } else {
      ret = SSLGeneric_write(ssl->fd, buf, (int)num);
   }

  end:
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Pending()
 *
 *	Functional equivalent of select when SSL is enabled
 *
 * Results:
 * 	Obtain number of readable bytes buffered in an SSL object if SSL
 *	is enabled, otherwise, return 0
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

int
SSL_Pending(SSLSock ssl) // IN
{
   int ret;
   ASSERT(ssl);

   if (ssl->encrypted) {
      ret = SSL_pending(ssl->sslCnx);
   } else {
      ret = 0;
   }

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_SetCloseOnShutdownFlag()
 *
 *    Sets closeFdOnShutdown flag.
 *
 * Results:
 *    None.  Always succeeds.  Do not call close/closesocket on
 *    the fd after this, call SSL_Shutdown() instead.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SSL_SetCloseOnShutdownFlag(SSLSock ssl)    // IN
{
   ASSERT(ssl);
   ssl->closeFdOnShutdown = TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_Shutdown()
 *
 *    Functional equivalent of the close() syscall.  Does
 *    not close the actual fd used for the connection.
 *
 *
 * Results:
 *    0 on success, -1 on failure.
 *
 * Side effects:
 *    closes the connection, freeing up the memory associated
 *    with the passed in socket object
 *
 *----------------------------------------------------------------------
 */

int
SSL_Shutdown(SSLSock ssl)     // IN
{
   int retVal = 0;
   ASSERT(ssl);

   SSL_LOG(("SSL: Starting shutdown for %d\n", ssl->fd));
   if (ssl->encrypted) {
      /* since quiet_shutdown is set, SSL_shutdown always succeeds */
      SSL_shutdown(ssl->sslCnx);
   }
   if (ssl->sslCnx) {
      SSL_free(ssl->sslCnx);
   }

   if (ssl->closeFdOnShutdown) {
      SSL_LOG(("SSL: Trying to close %d\n", ssl->fd));
      /*
       * Apparently in the past coverity has complained about the lack
       * of shutdown() before close() here. However we only want to
       * shut down the SSL layer, not the socket layer since authd may
       * handoff the fd to another process.
       */
      retVal = SSLGeneric_close(ssl->fd);
   }

   free(ssl);
   SSL_LOG(("SSL: shutdown done\n"));

   return retVal;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_GetFd()
 *
 *    Returns an SSL socket's file descriptor or handle.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_GetFd(SSLSock ssl) // IN
{
   ASSERT(ssl);

   return ssl->fd;
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_WantRead()
 *
 *    Wrapper around SSL_want_read.
 *
 * Results:
 *    That of SSL_want_read.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_WantRead(const SSLSock ssl)
{
   ASSERT(ssl);
   ASSERT(ssl->sslCnx);

   return SSL_want_read(ssl->sslCnx);
}


/*
 *----------------------------------------------------------------------
 *
 * SSL_TryCompleteAccept()
 *
 *    Call SSL_Accept() to start or redrive the SSL accept operation.
 *    Nonblocking.
 *
 * Results:
 *    > 0 if the SSL_accept completed successfully
 *    = 0 if the SSL_accept need a redrive
 *    < 0 if an error occurred
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
SSL_TryCompleteAccept(SSLSock ssl) // IN
{
   int sslRet;

   ASSERT(ssl);
   ASSERT(ssl->sslCnx);

   ERR_clear_error();
   sslRet = SSL_accept(ssl->sslCnx);
   ssl->sslIOError = SSL_get_error(ssl->sslCnx, sslRet);

   switch (ssl->sslIOError) {
   case SSL_ERROR_NONE:
      return 1;
   case SSL_ERROR_WANT_READ:
   case SSL_ERROR_WANT_WRITE:
      return 0;
   default:
      ssl->connectionFailed = TRUE;
      SSLPrintErrors(VMW_LOG_INFO);
      return -1;
   }
}

