//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "ConnectionManager.h"
#include "IocpContext.h"
#include "../ConnectionInformation.h"

namespace iocp { class CIocpHandler; }

namespace iocp 
{ 
namespace detail
{

class CIOCPServerControl : boost::noncopyable
{
public:
    CIOCPServerControl() 
    :     m_shutdownEvent   ( INVALID_HANDLE_VALUE )
        , m_ioCompletionPort( INVALID_HANDLE_VALUE )
        , m_listenSocket    ( INVALID_SOCKET )
        , m_acceptOperation ( INVALID_SOCKET,
                              0,
                              CIocpOperation::OpAccept,
                              4096
                            )
        , m_rcvBufferSize   ( 0 )
        , m_currentId       ( 0 )
        , m_acceptExFn      ( NULL )
    {
    }

    LONGLONG GetNextId( )
    {
        {
            mutex::scoped_lock l( m_cidMutex );
            ++m_currentId;
        }

        return m_currentId;
    }

    SOCKET                   m_listenSocket;

    HANDLE                   m_shutdownEvent;
    HANDLE                   m_ioCompletionPort;

    CConnectionManager       m_connectionManager;
    shared_ptr<CIocpHandler> m_iocpHandler;
    CIocpOperation           m_acceptOperation;
    uint32_t                 m_rcvBufferSize;

    LPFN_ACCEPTEX            m_acceptExFn;

private:

    mutex                    m_cidMutex;
    LONGLONG                 m_currentId;
};

}
} // end namespace
