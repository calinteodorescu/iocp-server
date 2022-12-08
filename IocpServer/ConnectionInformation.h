//! Copyright Alan Ning 2010
//! Distributed under the Boost Software License, Version 1.0.
//! (See accompanying file LICENSE_1_0.txt or copy at
//! http://www.boost.org/LICENSE_1_0.txt)

#pragma once  

namespace iocp 
{
class ConnectionInformation
{
public:
	ConnectionInformation()
		: m_remotePortNumber(0)
	{

	}
	tstring			m_remoteIpAddress;
	tstring			m_remoteHostName;
	boost::uint16_t m_remotePortNumber;
};
} // end namespace
