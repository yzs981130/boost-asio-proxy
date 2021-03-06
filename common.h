#ifndef _COMMON_H
#define _COMMON_H 1

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/regex.hpp>

#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include <boost/chrono.hpp>

#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>

#include <iostream>
#include <string>
#include <sstream>

namespace ba=boost::asio;
namespace bs=boost::system;
namespace bc=boost::chrono;
namespace pt=boost::property_tree;

typedef boost::shared_ptr<ba::ip::tcp::socket> socket_ptr;
typedef boost::shared_ptr<ba::io_service> io_service_ptr;

extern std::ofstream loggger;

#endif /* _COMMON_H */

