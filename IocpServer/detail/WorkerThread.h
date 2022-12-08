//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once

namespace iocp { namespace detail { class CIOCPServerControl; } }
namespace iocp { namespace detail { class CIocpContext; } }

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

    void HandleIocpContext      ( CIocpContext& iocpContext, DWORD bytesTransferred );
    void HandleCompletionFailure( OVERLAPPED*   overlapped,  DWORD bytesTransferred, int error );
    void HandleReceive          ( CIocpContext& iocpContext, DWORD bytesTransferred );
    void HandleSend             ( CIocpContext& iocpContext, DWORD bytesTransferred );
    void HandleAccept           ( CIocpContext& iocpContext, DWORD bytesTransferred );
    void HandleDisconnect       ( CIocpContext& iocpContext );

private:

    CIOCPServerControl& m_iocpServerControl;

    boost::thread       m_thread;
};
}
} // end namespace
