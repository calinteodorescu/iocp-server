//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#include "StdAfx.h"
#include "Connection.h"
#include "IocpContext.h"

// Using "this" ptr in initializer list. Yes, it is safe..
#pragma warning(disable : 4355)

namespace iocp 
{ 
namespace detail 
{

CConnection::CConnection( SOCKET   socket, 
                          uint64_t cid, 
                          uint32_t rcvBufferSize
                        )
:     m_socket             ( socket )
    , m_id                 ( cid )
    , m_disconnectPending  ( false )
    , m_sendClosePending   ( false )
    , m_rcvClosed          ( false )
    , m_rcvOperation       ( m_socket, 
                             m_id,
                             CIocpOperation::OpRcv,
                             rcvBufferSize
                           )
    , m_disconnectOperation( m_socket, 
                             m_id, 
                             CIocpOperation::OpDisconnect, 
                             0
                           )
{    
}

CConnection::~CConnection()
{
    closesocket(m_socket);
}

shared_ptr<CIocpOperation> CConnection::CreateAndQueueSendOperation( )
{
    shared_ptr<CIocpOperation> op( new CIocpOperation( m_socket, 
                                                       m_id, 
                                                       CIocpOperation::OpSend,
                                                       0
                                                     )
                                 );

    m_queuedForExecutionSendOperations.QueueForExecutionSendOperation( op );

    return op;
}

bool CConnection::HasOutstandingOperation( )
{

    if ( ::InterlockedExchangeAdd( & m_rcvClosed, 0 ) == 0 )
    {
        return true;
    }

    if ( m_queuedForExecutionSendOperations.NumOutstandingOperation( ) > 0 )
    {
        return true;
    }

    return false;
}


bool CConnection::CloseRcvOperation()
{
    if ( 0 == ::InterlockedExchange( & m_rcvClosed, 1 ) )
    {
        if ( INVALID_HANDLE_VALUE != m_rcvOperation.hEvent )
        {
            ::CloseHandle( m_rcvOperation.hEvent );
            m_rcvOperation.hEvent = INVALID_HANDLE_VALUE;
        }

        return true;
    }

    return false;
}
}
} // end namespace