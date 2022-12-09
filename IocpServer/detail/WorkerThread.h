//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once

namespace iocp { namespace detail { class CIOCPServerControl; } }
namespace iocp { namespace detail { class CIocpOperation; } }

namespace iocp 
{
namespace detail 
{

class CWorkerThread
{
public:

    explicit CWorkerThread( CIOCPServerControl& iocpServerControl );

    ~CWorkerThread();

    void Run( );

private:

    void HandleIocpOperation    ( CIocpOperation& iocpOperation, DWORD bytesTransferred );
    void HandleCompletionFailure( OVERLAPPED*     overlapped,    DWORD bytesTransferred, int error );
    void HandleReceive          ( CIocpOperation& iocpOperation, DWORD bytesTransferred );
    void HandleSend             ( CIocpOperation& iocpOperation, DWORD bytesTransferred );
    void HandleAccept           ( CIocpOperation& iocpOperation, DWORD bytesTransferred );
    void HandleDisconnect       ( CIocpOperation& iocpOperation );

private:

    CIOCPServerControl& m_iocpServerControl;

    boost::thread       m_thread;
};
}
} // end namespace
