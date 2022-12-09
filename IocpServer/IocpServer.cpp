//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#include "StdAfx.h"

#include "IocpServer.h"
#include "IocpException.h"

#include "detail/WorkerThread.h"
#include "detail/SharedIocpData.h"
#include "detail/Connection.h"
#include "detail/Utils.h"

namespace iocp
{

class CIocpServer::CImpl
{
public:

    enum { DefaultRcvBufferSize = 4096 };

    CImpl( uint16_t                   port,
           shared_ptr< CIocpHandler > iocpHandler,
           uint32_t                   addressToListenOn,
           uint32_t                   rcvbufferSize,
           uint32_t                   numThread
         )
    {
        // Initialize the port binding/listening first before setting the
        // handler. This way, we will never fire callbacks prematurely
        // to the user when the server is partially constructed.
        Initialize( addressToListenOn, 
                    port, 
                    rcvbufferSize, 
                    numThread
                  );

        m_iocpServerControl.m_iocpHandler = iocpHandler;
    }
    
    ~CImpl( )
    {
        Uninitialize( );
    }

    void Initialize( uint32_t addressToListenOn, 
                     uint16_t port, 
                     uint32_t rcvbufferSize, 
                     uint32_t numThread
                   )
    {
        try
        {
            if ( 0 == rcvbufferSize )
            {
                m_iocpServerControl.m_rcvBufferSize = DefaultRcvBufferSize;
            }
            else
            {
                m_iocpServerControl.m_rcvBufferSize = rcvbufferSize;
            }

            m_iocpServerControl.m_shutdownEvent = ::CreateEvent( NULL,  // lpEventAttributes
                                                                 TRUE,  // bManualReset
                                                                 FALSE, // bInitialState
                                                                 NULL
                                                               ); // lpName
            InitializeWinsock    ( );
            InitializeIocp       ( );
            InitializeThreadPool ( numThread );
            InitializeSocket     ( addressToListenOn,
                                   port
                                 );
            InitializeAcceptEvent( );
        }
        catch (...)
        {
            // Always uninitialize everything here upon all exception.
            // This is to ensure that we don't leak any resources upon
            // construction failure.
            Uninitialize( );

            // Re-throw to notify the clients.
            throw;
        }
    }

    void InitializeThreadPool( uint32_t numThread ) 
    {
        if ( 0 == numThread )
        {
            numThread = detail::sGetNumIocpThreads( );
        }

        m_threadPool.reserve( numThread );

        for( uint32_t i = 0
             ;
             i < numThread
             ;
             ++i
           )
        {
            m_threadPool.push_back( shared_ptr<detail::CWorkerThread>( new detail::CWorkerThread( m_iocpServerControl ) ) );
        }
    }

    void InitializeIocp( ) 
    {
        //Create I/O completion port
        // See http://msdn.microsoft.com/en-us/library/aa363862%28VS.85%29.aspx
        m_iocpServerControl.m_ioCompletionPort = detail::sCreateIocp( );

        if (NULL == m_iocpServerControl.m_ioCompletionPort)
        {
            throw CWin32Exception( ::WSAGetLastError( ) );
        }
    }

    void InitializeWinsock() 
    {
        // Initialize Winsock
        WSADATA wsaData;
        int nResult = ::WSAStartup( MAKEWORD( 2, 2 ),
                                    & wsaData
                                  );
        if ( NO_ERROR != nResult )
        {
            throw CWin32Exception(nResult);
        }
    }

    void InitializeSocket(uint32_t addressToListenOn, uint16_t portNumber)
    {
        if (INVALID_SOCKET != m_iocpServerControl.m_listenSocket)
        {
            ::closesocket(m_iocpServerControl.m_listenSocket);
        }

        {
            //Overlapped I/O follows the model established in Windows and can be performed only on 
            //sockets created through the WSASocket function 
            m_iocpServerControl.m_listenSocket = ::WSASocket( AF_INET, 
                                                              SOCK_STREAM, 
                                                              IPPROTO_TCP, 
                                                              NULL, 
                                                              0, 
                                                              WSA_FLAG_OVERLAPPED
                                                            );

            if ( INVALID_SOCKET == m_iocpServerControl.m_listenSocket ) 
            {
                throw CWin32Exception( ::WSAGetLastError( ) );
            }

            //Cleanup and Init with 0 the ServerAddress
            struct sockaddr_in serverAddress;
            ZeroMemory((char *)&serverAddress, sizeof(serverAddress));

            //Fill up the address structure
            {
                serverAddress.sin_family      = AF_INET;
                serverAddress.sin_addr.s_addr = addressToListenOn;
                serverAddress.sin_port        = htons( portNumber );    //comes from commandline
            }

            //Assign local address and port number
            if ( SOCKET_ERROR == ::bind( m_iocpServerControl.m_listenSocket, 
                                         ( struct sockaddr* ) & serverAddress, 
                                         sizeof( serverAddress )
                                       )
               )
            {
                ::closesocket( m_iocpServerControl.m_listenSocket );
                m_iocpServerControl.m_listenSocket = INVALID_SOCKET;

                throw CWin32Exception( ::WSAGetLastError( ) );
            }
        }

        //! @remark
        //! The maximum length of the queue of pending connections. 
        //! If set to SOMAXCONN, the underlying service provider responsible 
        //! for socket s will set the backlog to a maximum reasonable value. 
        //! There is no standard provision to obtain the actual backlog value.
        if ( SOCKET_ERROR == ::listen( m_iocpServerControl.m_listenSocket,
                                       SOMAXCONN
                                     )
           )
        {
            ::closesocket( m_iocpServerControl.m_listenSocket );
            m_iocpServerControl.m_listenSocket = INVALID_SOCKET;

            throw CWin32Exception( ::WSAGetLastError( ) );
        }

        m_iocpServerControl.m_acceptExFn = detail::sLoadAcceptEx( m_iocpServerControl.m_listenSocket );

        if ( NULL == m_iocpServerControl.m_acceptExFn )
        {
            throw CWin32Exception( ::GetLastError( ) );
        }

        detail::sAssociateDevice( ( HANDLE ) m_iocpServerControl.m_listenSocket,
                                  m_iocpServerControl
                                );
    }

    void InitializeAcceptEvent()
    {
        m_iocpServerControl.m_acceptOperation.m_socket = detail::sCreateOverlappedSocket( );

        detail::sPostAccept( m_iocpServerControl );
    }

    void Uninitialize( )
    {
        if ( INVALID_HANDLE_VALUE != m_iocpServerControl.m_shutdownEvent )
        {
            // Set the shutdown event so all worker threads can quit when
            // they unblock.
            SetEvent( m_iocpServerControl.m_shutdownEvent );
        }

        //! @remark
        //! See Network Programming for Microsoft Windows, SE Chapter 5
        //! for the following shutdown strategy.

        // Close all socket handles to flush out all pending overlapped
        // I/O operation.
        m_iocpServerControl.m_connectionManager.CloseAllConnections( );

        // Give out a NULL completion status to help unblock all worker
        // threads. This is retract all I/O request made to the threads, and
        // it may not be a graceful shutdown. It is the user's job to
        // graceful shutdown all connection before shutting down the server.
        for( const auto& thread : m_threadPool )
        {
            //Help threads get out of blocking - GetQueuedCompletionStatus()
            ::PostQueuedCompletionStatus( m_iocpServerControl.m_ioCompletionPort, 
                                          0, 
                                          ( DWORD ) NULL, 
                                          NULL
                                        );
        }

        //! @remark
        //! Some background information from Windows via CC++ by Mr. Richter.
        //!
        //! Before Windows Vista, when a thread issued an I/O request against 
        //! a device associated with a completion port, it was mandatory that 
        //! the thread remain alive until the request completed; otherwise, 
        //! Windows canceled any outstanding requests made by the thread. With 
        //! Windows Vista, this is no longer necessary: threads can now issue 
        //! requests and terminate; the request will still be processed and 
        //! the result will be queued to the completion port.
        m_threadPool.clear( );

        if ( INVALID_SOCKET != m_iocpServerControl.m_listenSocket )
        {
            ::closesocket( m_iocpServerControl.m_listenSocket );
            m_iocpServerControl.m_listenSocket = INVALID_SOCKET;
        }

        if ( INVALID_HANDLE_VALUE != m_iocpServerControl.m_shutdownEvent )
        {
            ::CloseHandle( m_iocpServerControl.m_shutdownEvent );
            m_iocpServerControl.m_shutdownEvent = INVALID_HANDLE_VALUE;
        }

        if ( INVALID_HANDLE_VALUE != m_iocpServerControl.m_ioCompletionPort )
        {
            ::CloseHandle( m_iocpServerControl.m_ioCompletionPort );
            m_iocpServerControl.m_ioCompletionPort = INVALID_HANDLE_VALUE;
        }

        if ( m_iocpServerControl.m_iocpHandler != NULL )
        {
            m_iocpServerControl.m_iocpHandler->OnServerClose( 0 );
            m_iocpServerControl.m_iocpHandler.reset( );
        }
    }

    void Send( uint64_t              cid,
               std::vector<uint8_t>& data
             )
    {
        shared_ptr<detail::CConnection> connection = m_iocpServerControl.m_connectionManager.GetConnection(cid);
        
        if(connection == NULL)
        {
            throw CIocpException(tstring(_T("Connection does not exist")));
            return;
        }

        shared_ptr<detail::CIocpOperation> sendOperation = connection->CreateSendOperation();

        // Take over user's data here and post it to the completion port.
        sendOperation->m_data.swap(data);
        sendOperation->ResetWsaBuf();

        int lastError = detail::sPostSend( * sendOperation );

        if ( WSA_IO_PENDING != lastError )
        {
            connection->m_queuedForExecutionSendOperations.RemoveSendOperation( sendOperation.get( ) );
            
            // Undo the swap here before throwing. This way, the user's
            // data is untouched and they may proceed to recover.
            data.swap(sendOperation->m_data);

            throw CWin32Exception( lastError );
        }
    }

    void Shutdown( uint64_t cid, int how )
    {
        shared_ptr<detail::CConnection> connection = m_iocpServerControl.m_connectionManager.GetConnection(cid);

        if(connection == NULL)
        {
            throw CIocpException(tstring(_T("Connection does not exist")));
            return;
        }

        ::shutdown(connection->m_socket, how);
    }

    void Disconnect( uint64_t cid)
    {
        shared_ptr<detail::CConnection> c = m_iocpServerControl.m_connectionManager.GetConnection( cid );

        if(c == NULL)
        {
            throw CIocpException(tstring(_T("Connection does not exist")));
            return;
        }

        Shutdown( cid, SD_BOTH );

        ::InterlockedIncrement( & c->m_disconnectPending );

        // Disconnect context is special (hacked) because it is not
        // tied to a connection. During graceful shutdown, it is very
        // difficult to determine when exactly is a good time to 
        // remove the connection. For example, a disconnect context 
        // might have already been sent by the IOCP thread send handler, 
        // and you wouldn't know it unless mutex are used. To keep it as 
        // lock-free as possible, this disconnect context may be redundant.
        // The disconnect handler will gracefully reject the redundant 
        // disconnect context.
        detail::sPostDisconnect( m_iocpServerControl, * c );
    }

public:

    detail::CIOCPServerControl m_iocpServerControl;

    typedef std::vector < shared_ptr<detail::CWorkerThread>> ThreadPool_t;

    ThreadPool_t m_threadPool;

    std::vector<uint8_t> outputBuffer_;
};

CIocpServer::CIocpServer( uint16_t                 port,
                          shared_ptr<CIocpHandler> iocpHandler,
                          uint32_t                 addressToListenOn /*= INADDR_ANY*/,
                          uint32_t                 rcvbufferSize /*= 0*/,
                          uint32_t                 numThread /*= 0*/
                        )
:   m_impl( new CIocpServer::CImpl( port, 
                                    iocpHandler, 
                                    addressToListenOn, 
                                    rcvbufferSize, 
                                    numThread
                                  )
          )
{
    if ( iocpHandler != NULL )
    {
        iocpHandler->m_iocpServer = this;
    }
}

CIocpServer::~CIocpServer()
{
}

void CIocpServer::Send( uint64_t cid, std::vector<uint8_t> &data )
{
    return m_impl->Send( cid, data );
}

void CIocpServer::Shutdown( uint64_t cid, int how )
{
    return m_impl->Shutdown( cid, how );
}

void CIocpServer::Disconnect( uint64_t cid)
{
    return m_impl->Disconnect( cid );
}

} // end namespace