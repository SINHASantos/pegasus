/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

# include <dsn/ports.h>
# include <dsn/service_api_c.h>
# include <dsn/cpp/address.h>
# include <dsn/internal/task.h>
# include "group_address.h"

# ifdef _WIN32


# else
# include <sys/socket.h>
# include <netdb.h>
# include <ifaddrs.h>
# include <netinet/in.h>
# include <arpa/inet.h>

# if defined(__FreeBSD__)
# include <netinet/in.h>
# endif

# endif

static void net_init()
{
    static std::once_flag flag;
    static bool flag_inited = false;
    if (!flag_inited)
    {
        std::call_once(flag, [&]()
        {
#ifdef _WIN32
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
            flag_inited = true;
        });
    }
}

// name to ip etc.
DSN_API uint32_t dsn_ipv4_from_host(const char* name)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    if ((addr.sin_addr.s_addr = inet_addr(name)) == (unsigned int)(-1))
    {
        hostent* hp = ::gethostbyname(name);
        int err =
# ifdef _WIN32
            (int)::WSAGetLastError()
# else
            h_errno
# endif
            ;
        dassert(hp != nullptr, "gethostbyname failed, name = %s, err = %d.", name, err);

        if (hp != nullptr)
        {
            memcpy(
                (void*)&(addr.sin_addr.s_addr),
                (const void*)hp->h_addr,
                (size_t)hp->h_length
                );
        }
    }

    // converts from network byte order to host byte order
    return (uint32_t)ntohl(addr.sin_addr.s_addr);
}

DSN_API uint32_t dsn_ipv4_local(const char* network_interface)
{
# ifdef _WIN32
    return 0;
# else
    struct ifaddrs* ifa = nullptr;
    getifaddrs(&ifa);

    struct ifaddrs* i = ifa;
    while (i != nullptr)
    {
        if (i->ifa_addr->sa_family == AF_INET && strcmp(i->ifa_name, network_interface) == 0)
        {
            return  (uint32_t)ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
        }
        i = i->ifa_next;
    }
    dassert(i != nullptr, "get local ip failed, network_interface=", network_interface);

    if (ifa != nullptr)
    {
        // remember to free it
        freeifaddrs(ifa);
    }
#endif
    return 0;
}

DSN_API const char*   dsn_address_to_string(dsn_address_t addr)
{
    char* p = dsn::tls_dsn.scatch_buffer;
    auto sz = sizeof(dsn::tls_dsn.scatch_buffer);

    switch (addr.u.v4.type)
    {
    case HOST_TYPE_IPV4:
        snprintf_p(
            p, sz,
            "%u.%u.%u.%u:%hu",
            ((uint32_t)addr.u.v4.ip) & 0xff,
            ((uint32_t)addr.u.v4.ip >> 8) & 0xff,
            ((uint32_t)addr.u.v4.ip >> 16) & 0xff,
            ((uint32_t)addr.u.v4.ip >> 24) & 0xff,
            (uint16_t)addr.u.v4.port
            );
        break;
    case HOST_TYPE_URI:
        p = (char*)(long)addr.u.uri.uri;
        break;
    case HOST_TYPE_GROUP:
        p = (char*)(((dsn::rpc_group_address*)(long)(addr.u.group.group))->name());
        break;
    default:
        p = (char*)"invalid address";
        break;
    }

    return (const char*)p;
}

DSN_API dsn_address_t dsn_address_build(
    const char* host,
    uint16_t port
    )
{
    dsn::rpc_address addr(host, port);
    return addr.c_addr();
}

DSN_API dsn_address_t dsn_address_build_ipv4(
    uint32_t ipv4,
    uint16_t port
    )
{
    dsn::rpc_address addr(ipv4, port);
    return addr.c_addr();
}

DSN_API dsn_address_t dsn_address_build_group(
    dsn_group_t g
    )
{
    dsn::rpc_address addr;
    addr.assign_group(g);
    return addr.c_addr();
}

DSN_API dsn_address_t dsn_address_build_uri(
    dsn_uri_t uri
    )
{
    dsn::rpc_address addr;
    addr.assign_uri(uri);
    return addr.c_addr();
}

DSN_API dsn_uri_t dsn_uri_build(const char* url) // must be paired with destroy later
{
    return (dsn_uri_t)strdup(url);
}

DSN_API void dsn_uri_destroy(dsn_uri_t uri)
{
    free((void*)uri);
}

DSN_API dsn_group_t dsn_group_build(const char* name) // must be paired with release later
{
    auto g = new ::dsn::rpc_group_address(name);
    return g;
}

DSN_API bool dsn_group_add(dsn_group_t g, dsn_address_t ep)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    ::dsn::rpc_address addr(ep);
    return grp->add(addr);
}

DSN_API void dsn_group_set_leader(dsn_group_t g, dsn_address_t ep)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    ::dsn::rpc_address addr(ep);
    grp->set_leader(addr);
}

DSN_API dsn_address_t dsn_group_get_leader(dsn_group_t g)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    return grp->leader().c_addr();
}

DSN_API bool dsn_group_is_leader(dsn_group_t g, dsn_address_t ep)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    return grp->leader() == ep;
}

DSN_API dsn_address_t dsn_group_next(dsn_group_t g, dsn_address_t ep)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    ::dsn::rpc_address addr(ep);
    return grp->next(addr).c_addr();
}

DSN_API bool dsn_group_remove(dsn_group_t g, dsn_address_t ep)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    ::dsn::rpc_address addr(ep);
    return grp->remove(addr);
}

DSN_API void dsn_group_destroy(dsn_group_t g)
{
    auto grp = (::dsn::rpc_group_address*)(g);
    delete grp;
}