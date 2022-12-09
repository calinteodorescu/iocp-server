//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#include "StdAfx.h"
#include "SendQueue.h"
#include "IocpContext.h"

namespace iocp { namespace detail {


CSendQueue::~CSendQueue()
{
	// This shouldn't be necessary because the server is programmed to
	// wait for all outstanding context to come back before going down.
	// But just in case...
	CloseAllSends();
}

void CSendQueue::AddSendOperation( shared_ptr<CIocpOperation> sendOperation )
{
	mutex::scoped_lock l(m_mutex);

	bool inserted = m_sendOperationMap.insert( std::make_pair( sendOperation.get(), sendOperation ) ).second;

	assert(true == inserted);
}

int CSendQueue::RemoveSendOperation( CIocpOperation* sendOperation )
{
	mutex::scoped_lock l(m_mutex);

	m_sendOperationMap.erase( sendOperation );

	return m_sendOperationMap.size();
}

uint32_t CSendQueue::NumOutstandingOperation()
{
	mutex::scoped_lock l(m_mutex);

	return m_sendOperationMap.size();
}

void CSendQueue::CloseAllSends()
{
	mutex::scoped_lock l(m_mutex);

	SendOperationMap_t::iterator itr = m_sendOperationMap.begin();
	while(m_sendOperationMap.end() != itr)
	{
		if(INVALID_HANDLE_VALUE != itr->second->hEvent)
		{
			CloseHandle(itr->second->hEvent);
			itr->second->hEvent = INVALID_HANDLE_VALUE;
		}
		++itr;
	}
}
} } // end namespace