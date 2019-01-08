/* Copyright (c) Microsoft Corporation. All rights reserved. */
/* Licensed under the MIT License. */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openenclave/bits/safecrt.h>
#include <openenclave/bits/sal_unsup.h>
#include <openenclave/internal/trace.h>
#include <openenclave/host.h>
#include "socket_u.h"

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define COPY_MEMORY_BUFFER_FROM_STRING(buff, str) \
    memset((buff), 0, sizeof(buff));              \
    oe_strncpy_s((buff), sizeof(buff), (str), strlen(str))

#define COPY_MEMORY_BUFFER(dest, src, srcLen)                  \
    if ((srcLen) < sizeof((dest)))                             \
    {                                                          \
        memset((dest) + (srcLen), 0, sizeof(dest) - (srcLen)); \
    }                                                          \
    memcpy((dest), (src), (srcLen))

oe_socket_error_t ocall_WSAStartup(void)
{
    return 0;
}

oe_socket_error_t ocall_WSACleanup(void)
{
    return 0;
}

gethostname_Result ocall_gethostname(void)
{
    gethostname_Result result;
    int err = gethostname(result.name, sizeof(result.name));
    result.error = (err == -1) ? (oe_socket_error_t)errno : 0;
    return result;
}

socket_Result ocall_socket(
    oe_socket_address_family_t a_AddressFamily,
    oe_socket_type_t a_Type,
    int a_Protocol)
{
    socket_Result result = {0};
    int fd = socket((int)a_AddressFamily, (int)a_Type, a_Protocol);
    result.hSocket = (void*)(size_t)fd;
    result.error = (fd == -1) ? (oe_socket_error_t)errno : 0;

    if (fd == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    return result;
}

oe_socket_error_t ocall_listen(void* a_hSocket, int a_nMaxConnections)
{
    int fd = (int)a_hSocket;

    int s = listen(fd, a_nMaxConnections);

    if (s == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    return s == -1 ? (oe_socket_error_t)s : 0;
}

GetSockName_Result ocall_getsockname(void* a_hSocket, int a_nNameLen)
{
    GetSockName_Result result = {0};
    socklen_t socklen = (socklen_t)a_nNameLen;
    int fd = (int)a_hSocket;
    int err = getsockname(fd, (struct sockaddr*)result.addr, &socklen);

    if (err == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    result.addrlen = (int)socklen;
    result.error = (err == -1) ? (oe_socket_error_t)errno : 0;
    return result;
}

GetSockName_Result ocall_getpeername(void* a_hSocket, int a_nNameLen)
{
    GetSockName_Result result = {0};
    socklen_t socklen = (socklen_t)a_nNameLen;
    int fd = (int)a_hSocket;
    int err = getpeername(fd, (struct sockaddr*)result.addr, &socklen);

    if (err == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    result.addrlen = (int)socklen;
    result.error = (err == -1) ? (oe_socket_error_t)errno : 0;
    return result;
}

send_Result ocall_send(
    void* a_hSocket,
    const void* a_Message,
    size_t a_nMessageLen,
    int a_Flags)
{
    send_Result result = {0};
    int fd = (int)a_hSocket;

    result.bytesSent = (int)send(fd, a_Message, a_nMessageLen, a_Flags);
    if (result.bytesSent == -1)
    {
        OE_TRACE_ERROR("errno = %d", errno);
        result.error = (oe_socket_error_t)errno;
    }

    return result;
}

ssize_t ocall_recv(
    void* a_hSocket,
    void* a_Buffer,
    size_t a_nBufferSize,
    int a_Flags,
    oe_socket_error_t* a_Error)
{
    int fd = (int)a_hSocket;

    *a_Error = OE_SOCKET_OK;
    ssize_t bytesReceived = recv(fd, a_Buffer, a_nBufferSize, a_Flags);
    if (bytesReceived == -1)
    {
        OE_TRACE_ERROR("errno = %d %s", errno, (errno == ENOTCONN) ? "ENOTCONN" : "look up in openenclave/libc/bits/errno.h");
        *a_Error = (oe_socket_error_t)errno;
    }

    return bytesReceived;
}

ssize_t ocall_recvfrom(
    void* a_hSocket,
    void* a_Buffer,
    size_t a_nBufferSize,
    int a_Flags,
    const void *src_addr,
    int addrlen,
    oe_socket_error_t* a_Error)
{
    int fd = (int)a_hSocket;

    ssize_t bytesReceived = recvfrom(fd, a_Buffer, a_nBufferSize, a_Flags,
                                    (struct sockaddr *)src_addr, (socklen_t*) &addrlen);
    if (bytesReceived == -1)
    {
        OE_TRACE_INFO("errno = %d", errno);
        *a_Error = (oe_socket_error_t)errno;
    }

    return bytesReceived;
}

getaddrinfo_Result ocall_getaddrinfo(
    const char* a_NodeName,
    const char* a_ServiceName,
    int a_Flags,
    int a_Family,
    int a_SockType,
    int a_Protocol)
{
    // oe_result_t status;
    getaddrinfo_Result result = {0};

    struct addrinfo* ai;
    struct addrinfo* ailist = NULL;
    struct addrinfo hints = {0};

    int i;
    int s;
    // int size;
    void* aibufhandle;
    addrinfo_Buffer* aibuf;

    hints.ai_flags = a_Flags;
    hints.ai_family = a_Family;
    hints.ai_socktype = a_SockType;
    hints.ai_protocol = a_Protocol;

    s = getaddrinfo(a_NodeName, a_ServiceName, &hints, &ailist);
    if (s != 0)
    {
        OE_TRACE_ERROR("s = %d", s);
        result.error = (oe_socket_error_t)s;
        return result;
    }

    /* Count number of addresses. */
    for (ai = ailist; ai != NULL; ai = ai->ai_next)
    {
        result.addressCount++;
    }

    // aibufhandle = CreateBuffer(result.addressCount * sizeof(*aibuf));
    aibufhandle = (void*)calloc((unsigned long)(result.addressCount) * sizeof(*aibuf), 1);
    if (aibufhandle == NULL)
    {
        OE_TRACE_ERROR("calloc failled", s);
        freeaddrinfo(ailist);
        result.error = OE_ENOBUFS;
        return result;
    }

    /* Serialize ailist. */
    // status = GetBuffer(aibufhandle, (char **)&aibuf, &size);
    // if (status != OE_OK) {
    //     FreeBuffer(aibufhandle);
    //     freeaddrinfo(ailist);
    //     result.error = OE_EFAULT;
    //     return result;
    // }
    aibuf = (addrinfo_Buffer*)aibufhandle;

    for (i = 0, ai = ailist; ai != NULL; ai = ai->ai_next, i++)
    {
        addrinfo_Buffer* aib = &aibuf[i];
        aib->ai_flags = ai->ai_flags;
        aib->ai_family = ai->ai_family;
        aib->ai_socktype = ai->ai_socktype;
        aib->ai_protocol = ai->ai_protocol;
        aib->ai_addrlen = (int)ai->ai_addrlen;
        COPY_MEMORY_BUFFER_FROM_STRING(
            aib->ai_canonname,
            (ai->ai_canonname != NULL) ? ai->ai_canonname : "");
        COPY_MEMORY_BUFFER(aib->ai_addr, ai->ai_addr, ai->ai_addrlen);
    }

    freeaddrinfo(ailist);

    // result.hMessage = aibufhandle;
    result.hMessage = aibuf;

    return result;
}

getsockopt_Result ocall_getsockopt(
    void* a_hSocket,
    int a_nLevel,
    int a_nOptName,
    int a_nOptLen)
{
    getsockopt_Result result = {0};
    int fd = (int)a_hSocket;
    socklen_t len = (socklen_t)a_nOptLen;
    int err = getsockopt(fd, a_nLevel, a_nOptName, result.buffer, &len);
    if (err == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    result.len = (int)len;
    result.error = (err == -1) ? (oe_socket_error_t)errno : 0;
    return result;
}

oe_socket_error_t ocall_setsockopt(
    void* a_hSocket,
    int a_nLevel,
    int a_nOptName,
    const void* a_OptVal,
    int a_nOptLen)
{
    int fd = (int)a_hSocket;
    int err = setsockopt(fd, a_nLevel, a_nOptName, a_OptVal, (socklen_t)a_nOptLen);
    if (err == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    return (err == -1) ? (oe_socket_error_t)errno : 0;
}

ioctlsocket_Result ocall_ioctlsocket(
    void* a_hSocket,
    int a_nCommand,
    unsigned int a_uInputValue)
{
    ioctlsocket_Result result = {0};
    int fd = (int)a_hSocket;
    result.outputValue = a_uInputValue;
    int err = fcntl(fd, a_nCommand, result.outputValue);
    if (err == -1)
    {
        OE_TRACE_ERROR("errno = %d", errno);
    }
    result.error = (err == -1) ? (oe_socket_error_t)errno : 0;
    return result;
}

static void copy_output_fds(
    oe_fd_set_internal* dest,
    const fd_set* src,
    const oe_fd_set_internal* orig)
{
    unsigned int i;

    for (i = 0; i < orig->fd_count; i++)
    {
        dest->fd_array[i] =
            FD_ISSET((int)orig->fd_array[i], src) ? orig->fd_array[i] : NULL;
    }

    for (; i < sizeof(dest->fd_array); i++)
    {
        dest->fd_array[i] = NULL;
    }
}

static void copy_input_fds(
    fd_set* dest,
    const oe_fd_set_internal* src,
    int* nfds)
{
    unsigned int i;
    FD_ZERO(dest);
    for (i = 0; i < src->fd_count; i++)
    {
        FD_SET((int)src->fd_array[i], dest);
        *nfds = MAX(*nfds, (int)src->fd_array[i]);
    }
}

select_Result ocall_select(
    int a_nFds,
    oe_fd_set_internal a_ReadFds,
    oe_fd_set_internal a_WriteFds,
    oe_fd_set_internal a_ExceptFds,
    struct timeval a_Timeout)
{
    select_Result result = {0};
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    int nfds = 0;
    // TODO: the following line was added 
nfds = a_nFds;

    copy_input_fds(&readfds, &a_ReadFds, &nfds);
    copy_input_fds(&writefds, &a_WriteFds, &nfds);
    copy_input_fds(&exceptfds, &a_ExceptFds, &nfds);

    result.socketsSet = select(
        nfds + 1, &readfds, &writefds, &exceptfds, (struct timeval*)&a_Timeout);
    if (result.socketsSet == -1)
    {
        result.error = (oe_socket_error_t)errno;
    }
    else
    {
        copy_output_fds(&result.readFds, &readfds, &a_ReadFds);
        copy_output_fds(&result.writeFds, &writefds, &a_WriteFds);
        copy_output_fds(&result.exceptFds, &exceptfds, &a_ExceptFds);
    }

    return result;
}

oe_socket_error_t ocall_shutdown(void* a_hSocket, oe_shutdown_how_t a_How)
{
    int fd = (int)a_hSocket;
    int err = shutdown(fd, (int)a_How);
    return (err == -1) ? (oe_socket_error_t)errno : 0;
}

oe_socket_error_t ocall_closesocket(void* a_hSocket)
{
    int fd = (int)a_hSocket;
    int s = close(fd);

    if (s == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    return s == -1 ? (oe_socket_error_t)errno : 0;
}

oe_socket_error_t ocall_bind(
    void* a_hSocket,
    const void* a_Name,
    int a_nNameLen)
{
    int fd = (int)a_hSocket;
    int s = bind(fd, (const struct sockaddr*)a_Name, (socklen_t)a_nNameLen);
    if (s == -1)
        OE_TRACE_ERROR("errno = %d", errno);

    return s == -1 ? (oe_socket_error_t)errno : 0;
}

oe_socket_error_t ocall_connect(
    void* a_hSocket,
    const void* a_Name,
    int a_nNameLen)
{
    int fd = (int)a_hSocket;
    int s = connect(fd, (const struct sockaddr*)a_Name, (socklen_t)a_nNameLen);
    if (s == -1)
    {
        OE_TRACE_ERROR("errno = %d (%s)", errno, 
                       (errno == ECONNREFUSED) ? "ECONNREFUSED" : 
                       "look up in openenclave/libc/bits/errno.h");
    }
    return s == -1 ? (oe_socket_error_t)errno : 0;
}

accept_Result ocall_accept(void* a_hSocket, int a_nAddrLen)
{
    int fd = (int)a_hSocket;
    socklen_t len = (socklen_t)a_nAddrLen;
    accept_Result result = {0};
    result.hNewSocket = (void*)(size_t)accept(
        fd,
        ((a_nAddrLen > 0) ? (struct sockaddr*)result.addr : NULL),
        ((a_nAddrLen > 0) ? &len : NULL));
    result.addrlen = (int)len;
    result.error =
        (result.hNewSocket == (void*)(-1)) ? (oe_socket_error_t)errno : 0;
    return result;
}

getnameinfo_Result ocall_getnameinfo(
    const void* a_Addr,
    int a_AddrLen,
    int a_Flags)
{
    getnameinfo_Result result = {0};
    result.error = (oe_socket_error_t)getnameinfo(
        (const struct sockaddr*)a_Addr,
        (socklen_t)a_AddrLen,
        result.host,
        sizeof(result.host),
        result.serv,
        sizeof(result.serv),
        a_Flags);
    return result;
}
