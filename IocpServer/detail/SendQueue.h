//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <map>
#include <boost/smart_ptr/shared_ptr.hpp>

namespace iocp 
{ 
namespace detail 
{ 
class CIocpOperation;
}
};

namespace iocp
{ 
namespace detail 
{

class CSendQueue
{
public:
    ~CSendQueue();

    void   QueueForExecutionSendOperation( ::boost::shared_ptr<CIocpOperation> sendOperation);
    size_t RemoveSendOperation           ( CIocpOperation*                     sendOperation );
    void   CloseAllSends                 ( void );
    size_t NumOutstandingOperation       ( void );

private:
    using SendOperationMap_t = std::map< CIocpOperation*, shared_ptr<CIocpOperation> >;

    SendOperationMap_t m_queuedForExecutionSendOperations;

    mutex m_mutex;
};

}
} // end namespace
