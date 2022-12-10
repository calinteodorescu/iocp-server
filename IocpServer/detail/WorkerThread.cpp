//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#include "StdAfx.h"
#include "WorkerThread.h"
#include "SharedIocpData.h"
#include "IocpContext.h"
#include "Connection.h"
#include "Utils.h"

#include "../IocpHandler.h"

namespace iocp { namespace detail {

CWorkerThread::CWorkerThread( CIOCPServerControl& iocpControlAsServer )
:   m_iocpControlAsServer( iocpControlAsServer )
{
    m_thread = thread( bind( & CWorkerThread::AttachToIOCPAndRun,
                             this
                           )
                     );
}

CWorkerThread::~CWorkerThread()
{
    m_thread.join();
}

void CWorkerThread::AttachToIOCPAndRun( void )
{
    for( ;; )
    {
        void*       key              = NULL;
        OVERLAPPED* overlapped       = NULL;
        DWORD       bytesTransferred = 0;

        BOOL completionStatus = ::GetQueuedCompletionStatus( m_iocpControlAsServer.m_ioCompletionPort,
                                                             & bytesTransferred,
                                                             ( PULONG_PTR ) & key,
                                                             & overlapped,
                                                             INFINITE
                                                           );

        if ( FALSE == completionStatus )
        {
            HandleCompletionFailure( overlapped, 
                                     bytesTransferred, 
                                     ::GetLastError( )
                                   );
            continue;
        }

        // NULL key packet is a special status that unblocks the worker
        // thread to initial a shutdown sequence. The thread should be going
        // down soon.
        if ( NULL == key )
        {
            break;
        }

        CIocpOperation& iocpOperation = * reinterpret_cast< CIocpOperation* >( overlapped );

        HandleIocpOperation( iocpOperation,
                             bytesTransferred
                           );
    }
}

void CWorkerThread::HandleReceive( CIocpOperation& rcvOperation,
                                   DWORD           bytesTransferred
                                 )
{
    shared_ptr< CConnection > c = m_iocpControlAsServer.m_connectionManager.GetConnection( rcvOperation.m_cid );
    if ( c == nullptr )
    {
        assert(false);

        return;
    }

    // If nothing is transferred, we are about to be disconnected. In this
    // case, don't notify the client that nothing is received because
    // they are about to get a disconnection callback.
    if ( 0 != bytesTransferred )
    {
        // Shrink the buffer to fit the byte transferred
        rcvOperation.m_data.resize( bytesTransferred );
        assert(rcvOperation.m_data.size() == bytesTransferred);

        if ( m_iocpControlAsServer.m_iocpHandler != NULL )
        {
            // Invoke the callback for the client
            m_iocpControlAsServer.m_iocpHandler->OnReceiveData( rcvOperation.m_cid,
                                                                rcvOperation.m_data
                                                              );
        }
    }

    // Resize it back to the original buffer size and prepare to post
    // another completion status.
    rcvOperation.m_data.resize(rcvOperation.m_rcvBufferSize);
    rcvOperation.ResetWsaBuf();

    int lastError = NO_ERROR;

    // 0 bytes transferred, or if a recv context can't be posted to the 
    // IO completion port, that implies the socket at least half-closed.
    if( ( bytesTransferred == 0 )
        || 
        ( ( lastError = sPostRecv( rcvOperation ) ) != WSA_IO_PENDING )
      )
    {
        uint64_t cid = rcvOperation.m_cid;
        if ( c->CloseRcvOperation( ) == true )
        {
            ::shutdown( c->m_socket, SD_RECEIVE );

            if ( m_iocpControlAsServer.m_iocpHandler != NULL )
            {
                m_iocpControlAsServer.m_iocpHandler->OnClientDisconnect( cid,
                                                                         lastError
                                                                       );
            }
        }
    }
}

void CWorkerThread::HandleSend( CIocpOperation& iocpOperation, 
                                DWORD           bytesTransferred
                              )
{
    shared_ptr<CConnection> c = m_iocpControlAsServer.m_connectionManager.GetConnection( iocpOperation.m_cid );
    if(c == NULL)
    {
        assert(false);
        return;
    }

    uint64_t cid = iocpOperation.m_cid;

    if ( bytesTransferred > 0 )
    {
        if ( m_iocpControlAsServer.m_iocpHandler != NULL )
        {
            m_iocpControlAsServer.m_iocpHandler->OnSentData( cid,
                                                             bytesTransferred
                                                           );
        }
    }
    //No bytes transferred, that means send has failed.
    else
    {
        // what to do here?
    }

    //! @remark
    //! Remove the send context after notifying the user. Otherwise
    //! there is a race condition where a disconnect context maybe waiting 
    //! for the send queue to go to zero at the same time. In this case,
    //! the disconnect notification will come before we notify the user.
    int outstandingSend = c->m_queuedForExecutionSendOperations.RemoveSendOperation( & iocpOperation );

    // If there is no outstanding send context, that means all sends 
    // are completed for the moment. At this point, if we have a half-closed 
    // socket, and the connection is pending to be disconnected, post a 
    // disconnect context for a graceful shutdown.
    if ( 0 == outstandingSend )
    {
        if ( ( ::InterlockedExchangeAdd( & c->m_rcvClosed, 0 ) > 0 ) 
             &&
             ( ::InterlockedExchangeAdd( & c->m_disconnectPending, 0 ) > 0 )
           )
        {
            // Disconnect context is special (hacked) because it is not
            // tied to a connection. During graceful shutdown, it is very
            // difficult to determine when exactly is a good time to 
            // remove the connection. For example, a disconnect context 
            // might have already been sent by the user code, and you
            // wouldn't know it unless mutex are used. To keep it as 
            // lock-free as possible, the disconnect handler
            // will gracefully handle redundant disconnect context.
            sPostDisconnect( m_iocpControlAsServer,
                             * c
                           );
        }
    }
}

void CWorkerThread::HandleAccept( CIocpOperation& acceptOperation,
                                  DWORD           bytesTransferred
                                )
{
    // We should be accepting immediately without waiting for any data.
    // If this has change, we need to account for that accept buffer and post
    // it to the receive side.
    assert( 0 == bytesTransferred );

    // Update the socket option with SO_UPDATE_ACCEPT_CONTEXT so that
    // getpeername will work on the accept socket.
    if ( ::setsockopt( acceptOperation.m_socket, 
                       SOL_SOCKET, 
                       SO_UPDATE_ACCEPT_CONTEXT, 
                       ( char* ) & m_iocpControlAsServer.m_listenSocket, 
                       sizeof( m_iocpControlAsServer.m_listenSocket )
                     ) != 0
       )
    {
        if ( m_iocpControlAsServer.m_iocpHandler != NULL )
        {
            // This shouldn't happen, but if it does, report the error. 
            // Since the connection has not been established, it is not necessary
            // to notify the client to remove any connections.
            m_iocpControlAsServer.m_iocpHandler->OnServerError( ::WSAGetLastError( ) );
        }
    }
    // If the socket is up, allocate the connection and notify the client.
    else
    {
        ConnectionInformation cinfo = sGetConnectionInformation( acceptOperation.m_socket );

        shared_ptr< CConnection > c( new CConnection( acceptOperation.m_socket, 
                                                      m_iocpControlAsServer.GetNextId(),
                                                      m_iocpControlAsServer.m_rcvBufferSize
                                                    )
                                   );

        m_iocpControlAsServer.m_connectionManager.AddConnection( c );

        sListenOnIOCPToThisHandle( m_iocpControlAsServer,
                                   ( HANDLE ) c->m_socket
                                 );

        if ( m_iocpControlAsServer.m_iocpHandler != NULL )
        {
            m_iocpControlAsServer.m_iocpHandler->OnNewConnection( c->m_id,
                                                                  cinfo
                                                                );
        }

        // have something to wait for in case the Disconnect happens before any incoming traffic
        int lasterror = sPostRecv( c->m_rcvOperation );

        // Failed to post a queue a receive context. It is likely that the
        // connection is already terminated at this point (by user or client).
        // In such case, just remove the connection.
        if ( WSA_IO_PENDING != lasterror )
        {
            if( true == c->CloseRcvOperation( ) )
            {
                sPostDisconnect( m_iocpControlAsServer,
                                 * c
                               );
            }
        }
    }

    // Post another Accept context to IOCP for another new connection.
    //! @remark
    //! For higher performance, it is possible to preallocate these sockets
    //! and have a pool of accept context waiting. That adds complexity, and
    //! unnecessary for now.
    acceptOperation.m_socket = sCreateOverlappedSocket( );

    if ( INVALID_SOCKET != acceptOperation.m_socket )
    {
        sPostAccept( m_iocpControlAsServer );
    }
    else
    {
        if ( m_iocpControlAsServer.m_iocpHandler != NULL )
        {
            m_iocpControlAsServer.m_iocpHandler->OnServerError( ::WSAGetLastError( ) );
        }
    }
}

void CWorkerThread::HandleIocpOperation( CIocpOperation& iocpOperation, 
                                         DWORD           bytesTransferred
                                       )
{
    switch ( iocpOperation.m_type )
    {
        case CIocpOperation::OpRcv:
            HandleReceive( iocpOperation,
                           bytesTransferred
                         );
            break;
        case CIocpOperation::OpSend:
            HandleSend( iocpOperation,
                        bytesTransferred
                      );
            break;
        case CIocpOperation::OpAccept:
            HandleAccept( iocpOperation, 
                          bytesTransferred
                        );
            break;
        case CIocpOperation::OpDisconnect:
            HandleDisconnect( iocpOperation );

            break;
        default:
            assert( false );

            break;
    }
}

void CWorkerThread::HandleCompletionFailure( OVERLAPPED* overlapped, 
                                             DWORD       bytesTransferred, 
                                             int         error
                                           )
{
    if (NULL != overlapped ) 
    {
        // Process a failed completed I/O request
        // dwError contains the reason for failure
        CIocpOperation& iocpOperation = * reinterpret_cast<CIocpOperation*>( overlapped );

        HandleIocpOperation( iocpOperation,
                             bytesTransferred
                           );
    } 
    else 
    {
        // Currently, GetQueuedCompletionStatus is called with an INFINITE
        // timeout flag, so it should not be possible for the status to 
        // timeout.
        assert(WAIT_TIMEOUT != error);
        
        if(m_iocpControlAsServer.m_iocpHandler != NULL)
        {
            // GetQueuedCompletionStatus failed. Notify the user on this event.
            m_iocpControlAsServer.m_iocpHandler->OnServerError( error );
        }
    }
}

void CWorkerThread::HandleDisconnect( CIocpOperation &iocpOperation )
{
    uint64_t cid = iocpOperation.m_cid;

    // Disconnect context isn't tied to the connection. Therefore, it must
    // be deleted manually at all times.
    delete &iocpOperation;

    shared_ptr<CConnection> c = m_iocpControlAsServer.m_connectionManager.GetConnection(cid);
    if(c == NULL)
    {
        //! @remark
        //! Disconnect context may come from several different source at 
        //! once, and only the first one to remove the connection. If the
        //! connection is already removed, gracefully handle it.

        //std::cout <<"Extra disconnect packet received" << std::endl;
        return;
    }

    if(c->HasOutstandingOperation() == true)
    {
        return;
    }

    if(true == m_iocpControlAsServer.m_connectionManager.RemoveConnection(cid) )
    {
        if(m_iocpControlAsServer.m_iocpHandler != NULL)
        {
            m_iocpControlAsServer.m_iocpHandler->OnDisconnect(cid,0);
        }
    }
}
} } // end namespace
