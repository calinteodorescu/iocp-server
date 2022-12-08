//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "../ConnectionInformation.h"

namespace iocp { namespace detail { class CIOCPServerControl; } };
namespace iocp { namespace detail { class CIocpContext; } };
namespace iocp { namespace detail { class CConnection; } };

namespace iocp
{
namespace detail 
{
    int                   sGetNumIocpThreads       ( void );
    SOCKET                sCreateOverlappedSocket  ( void );
    ConnectionInformation sGetConnectionInformation( SOCKET socket );
    void                  sPostAccept              ( CIOCPServerControl& iocpServerControl );
    int                   sPostRecv                ( CIocpContext&       iocpContext );
    int                   sPostSend                ( CIocpContext&       iocpContext );
    int                   sPostDisconnect          ( CIOCPServerControl& iocpServerControl,
                                                     CConnection&        c
                                                   );                    
    void                  sAssociateDevice         ( HANDLE              h,
                                                     CIOCPServerControl& iocpServerControl
                                                   );
    HANDLE                sCreateIocp              ( int                 maxConcurrency = 2 );
    
    LPFN_ACCEPTEX         sLoadAcceptEx            ( SOCKET s );
}
} // end namespace
