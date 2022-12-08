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
class CIocpContext;
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

	void AddSendContext   ( ::boost::shared_ptr<CIocpContext> sendContext);
	int  RemoveSendContext( CIocpContext*			sendContext );
	void CloseAllSends    ( );

	uint32_t NumOutstandingContext();

private:
	using SendContextMap_t = std::map< CIocpContext*, shared_ptr<CIocpContext> >;

	SendContextMap_t m_sendContextMap;

	mutex m_mutex;
};

}
} // end namespace
