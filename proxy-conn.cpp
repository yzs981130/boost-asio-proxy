/**
 * @file   proxy-conn.cpp
 * @author Alex Ott <alexott@gmail.com>
 * 
 * @brief  
 * 
 * 
 */

#include "proxy-conn.hpp"

/** 
 * 
 * 
 * @param io_service 
 */
connection::connection(ba::io_service &io_service) : io_service_(io_service),
                                                     bsocket_(io_service),
                                                     ssocket_(io_service),
                                                     resolver_(io_service),
                                                     proxy_closed(false),
                                                     isPersistent(false),
                                                     isOpened(false),
                                                     isVideoMeta(false),
                                                     isBigBuckBunny(false),
                                                     isVideoChunk(false) {
  fHeaders.reserve(8192);
}

/** 
 * Start read data of request from browser
 * 
 */
void connection::start() {
//  	std::cout << "start" << std::endl;
  fHeaders.clear();
  reqHeaders.clear();
  respHeaders.clear();

  handle_browser_read_headers(bs::error_code(), 0);
}

/** 
 * Read header of HTTP request from browser
 * 
 * @param err 
 * @param len 
 */
void connection::handle_browser_read_headers(const bs::error_code &err, size_t len) {
//  	std::cout << "handle_browser_read_headers. Error: " << err << ", len=" << len << std::endl;
  if (!err) {
    if (fHeaders.empty())
      fHeaders = std::string(bbuffer.data(), len);
    else
      fHeaders += std::string(bbuffer.data(), len);
    if (fHeaders.find("\r\n\r\n") == std::string::npos) { // going to read rest of headers
      async_read(bsocket_, ba::buffer(bbuffer), ba::transfer_at_least(1),
                 boost::bind(&connection::handle_browser_read_headers,
                             shared_from_this(),
                             ba::placeholders::error,
                             ba::placeholders::bytes_transferred));
    } else { // analyze headers
      //std::cout << "fHeaders:\n" << fHeaders << std::endl;
      std::string::size_type idx = fHeaders.find("\r\n");
      std::string reqString = fHeaders.substr(0, idx);
      fHeaders.erase(0, idx + 2);

      idx = reqString.find(" ");
      if (idx == std::string::npos) {
        std::cout << "Bad first line: " << reqString << std::endl;
        return;
      }

      fMethod = reqString.substr(0, idx);
      reqString = reqString.substr(idx + 1);
      idx = reqString.find(" ");
      if (idx == std::string::npos) {
        std::cout << "Bad first line of request: " << reqString << std::endl;
        return;
      }
      fURL = reqString.substr(0, idx);
      fReqVersion = reqString.substr(idx + 1);
      idx = fReqVersion.find("/");
      if (idx == std::string::npos) {
        std::cout << "Bad first line of request: " << reqString << std::endl;
        return;
      }
      fReqVersion = fReqVersion.substr(idx + 1);

      // string outputs to console completely, even when using multithreading
      //std::cout << std::string("\n fMethod: " + fMethod + ", fURL: " + fURL + ", fReqVersion: " + fReqVersion + "\n");
      // analyze headers, etc
      parseHeaders(fHeaders, reqHeaders);

      // start to connect to servers (including DNS requests)
      start_connect();
    }
  } else {
    shutdown();
  }
}

/** 
 * Start connecting to the web-server, initially to resolve the DNS-name of web-server into the IP address
 * 
 */
void connection::start_connect() {
  std::string server = "";
  std::string port = "80";
  boost::regex rHTTP("http://(.*?)(:(\\d+))?(/.*)");
  boost::smatch m;

  if (boost::regex_search(fURL, m, rHTTP, boost::match_extra)) {
    server = m[1].str();
    if (m[2].str() != "") {
      port = m[3].str();
    }
    fNewURL = m[4].str();
  } else if (server.empty() && !reqHeaders["Host"].empty() && fMethod != "CONNECT") {
    std::cout << "Log: reverse proxy " << std::endl;
    std::cout << fHeaders << std::endl;
    //TODO: reverse proxy not complete
    fNewURL = fURL;
  } else {
    std::cout << "Can't parse URL " << std::endl;
    //std::cout << fHeaders << std::endl;
    return;
  }

  check_video_requests(fNewURL);
  if (isVideoChunk)
    fNewURL = adapt_bitrate(fServer, pathToVideo, segNum, fragNum);

  //std::cout << server << " " << port << " " << fNewURL << std::endl;

  if (!isOpened || server != fServer || port != fPort) {
    fServer = server;
    fPort = port;
    ba::ip::tcp::resolver::query query(server, port);
    resolver_.async_resolve(query,
                            boost::bind(&connection::handle_resolve, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
  } else {
    start_write_to_server();
  }
}

/** 
 * If successful, after the resolved DNS-names of web-server into the IP addresses, try to connect
 * 
 * @param err 
 * @param endpoint_iterator 
 */
void connection::handle_resolve(const boost::system::error_code &err,
                                ba::ip::tcp::resolver::iterator endpoint_iterator) {
//	std::cout << "handle_resolve. Error: " << err.message() << "\n";
  if (!err) {
    const bool first_time = true;
    handle_connect(boost::system::error_code(), endpoint_iterator, first_time);
  } else {
    shutdown();
  }
}

/** 
 * Try to connect to the web-server
 * 
 * @param err 
 * @param endpoint_iterator 
 */
void connection::handle_connect(const boost::system::error_code &err,
                                ba::ip::tcp::resolver::iterator endpoint_iterator, const bool first_time) {
//	std::cout << "handle_connect. Error: " << err << "\n";
  if (!err && !first_time) {
    isOpened = true;
    start_write_to_server();
  } else if (endpoint_iterator != ba::ip::tcp::resolver::iterator()) {
    //ssocket_.close();
    ba::ip::tcp::endpoint endpoint = *endpoint_iterator;
    ssocket_.async_connect(endpoint,
                           boost::bind(&connection::handle_connect, shared_from_this(),
                                       boost::asio::placeholders::error,
                                       ++endpoint_iterator, false));
  } else {
    shutdown();
  }
}

/** 
 * Write data to the web-server
 * 
 */
void connection::start_write_to_server() {
  fReq = fMethod;
  fReq += " ";
  fReq += fNewURL;
  fReq += " HTTP/";
  fReq += "1.0";
//	fReq+=fReqVersion;
  fReq += "\r\n";
  fReq += fHeaders;
  //std::cout << "Request: " << fReq << std::endl;
  tStart = bc::steady_clock::now();
  ba::async_write(ssocket_, ba::buffer(fReq),
                  boost::bind(&connection::handle_server_write, shared_from_this(),
                              ba::placeholders::error,
                              ba::placeholders::bytes_transferred));

  fHeaders.clear();
}

/** 
 * If successful, read the header that came from a web server
 * 
 * @param err
 * @param len 
 */
void connection::handle_server_write(const bs::error_code &err, size_t len) {
// 	std::cout << "handle_server_write. Error: " << err << ", len=" << len << std::endl;
  if (!err) {
    handle_server_read_headers(bs::error_code(), 0);
  } else {
    shutdown();
  }
}

/** 
 * Read header of data returned from the web-server
 * 
 * @param err 
 * @param len 
 */
void connection::handle_server_read_headers(const bs::error_code &err, size_t len) {
// 	std::cout << "handle_server_read_headers. Error: " << err << ", len=" << len << std::endl;
  if (!err) {
    std::string::size_type idx;
    if (fHeaders.empty())
      fHeaders = std::string(sbuffer.data(), len);
    else
      fHeaders += std::string(sbuffer.data(), len);
    idx = fHeaders.find("\r\n\r\n");
    if (idx == std::string::npos) { // going to read rest of headers
      async_read(ssocket_, ba::buffer(sbuffer), ba::transfer_at_least(1),
                 boost::bind(&connection::handle_server_read_headers,
                             shared_from_this(),
                             ba::placeholders::error,
                             ba::placeholders::bytes_transferred));
    } else { // analyze headers
//			std::cout << "Response: " << fHeaders << std::endl;
      RespReaded = len - idx - 4;
      idx = fHeaders.find("\r\n");
      std::string respString = fHeaders.substr(0, idx);
      RespLen = -1;
      parseHeaders(fHeaders.substr(idx + 2), respHeaders);
      std::string reqConnString = "", respConnString = "";

      std::string respVersion = respString.substr(respString.find("HTTP/") + 5, 3);

      headersMap::iterator it = respHeaders.find("Content-Length");
      if (it != respHeaders.end())
        RespLen = boost::lexical_cast<int>(it->second);
      it = respHeaders.find("Connection");
      if (it != respHeaders.end())
        respConnString = it->second;
      it = reqHeaders.find("Connection");
      if (it != reqHeaders.end())
        reqConnString = it->second;

      if (isVideoMeta) {
        idx = fHeaders.find("\r\n\r\n");
        std::string xml;
        xml = fHeaders.substr(idx + 4, RespLen);
        std::vector<int32_t> bitRates = get_bitrates(xml);
        if (!bitRates.empty()) {
          throughputMap[fServer] = std::make_pair(*(bitRates.begin()), bitRates);
          if (isBigBuckBunny) {
            // special_case: query again for nolist
            std::cout << "Log: query for big_buck_bunny_nolist to forward" << std::endl;
            fNewURL = pathToVideo + "big_buck_bunny_nolist.f4m";
            start_write_to_server();
          }
        } else {
          std::cout << "Error: receive ill-formed manifest" << std::endl;
          shutdown();
        }
        isVideoMeta = false;
      }
      if (isVideoChunk) {
        update_throughput(RespLen, tStart, fServer);
        isVideoChunk = false;
      }

      isPersistent = (
          ((fReqVersion == "1.1" && reqConnString != "close") ||
           (fReqVersion == "1.0" && reqConnString == "keep-alive")) &&
          ((respVersion == "1.1" && respConnString != "close") ||
           (respVersion == "1.0" && respConnString == "keep-alive")) &&
          RespLen != -1);
// 			std::cout << "RespLen: " << RespLen << " RespReaded: " << RespReaded
// 					  << " isPersist: " << isPersistent << std::endl;

      // sent data
      ba::async_write(bsocket_, ba::buffer(fHeaders),
                      boost::bind(&connection::handle_browser_write,
                                  shared_from_this(),
                                  ba::placeholders::error,
                                  ba::placeholders::bytes_transferred));
    }
  } else {
    shutdown();
  }
}

/** 
 * Writing data to the browser, are recieved from web-server
 * 
 * @param err 
 * @param len 
 */
void connection::handle_browser_write(const bs::error_code &err, size_t len) {
//   	std::cout << "handle_browser_write. Error: " << err << " " << err.message()
//  			  << ", len=" << len << std::endl;
  if (!err) {
    if (!proxy_closed && (RespLen == -1 || RespReaded < RespLen))
      async_read(ssocket_, ba::buffer(sbuffer, len), ba::transfer_at_least(1),
                 boost::bind(&connection::handle_server_read_body,
                             shared_from_this(),
                             ba::placeholders::error,
                             ba::placeholders::bytes_transferred));
    else {
//			shutdown();
      if (isPersistent && !proxy_closed) {
        std::cout << "Starting read headers from browser, as connection is persistent" << std::endl;
        start();
      }
    }
  } else {
    shutdown();
  }
}

/** 
 * Reading data from a Web server, for the writing them to the browser
 * 
 * @param err 
 * @param len 
 */
void connection::handle_server_read_body(const bs::error_code &err, size_t len) {
//   	std::cout << "handle_server_read_body. Error: " << err << " " << err.message()
//  			  << ", len=" << len << std::endl;
  if (!err || err == ba::error::eof) {
    RespReaded += len;
// 		std::cout << "len=" << len << " resp_readed=" << RespReaded << " RespLen=" << RespLen<< std::endl;
    if (err == ba::error::eof)
      proxy_closed = true;
    ba::async_write(bsocket_, ba::buffer(sbuffer, len),
                    boost::bind(&connection::handle_browser_write,
                                shared_from_this(),
                                ba::placeholders::error,
                                ba::placeholders::bytes_transferred));
  } else {
    shutdown();
  }
}

/** 
 * Close both sockets: for browser and web-server
 * 
 */
void connection::shutdown() {
  ssocket_.close();
  bsocket_.close();
}


void connection::parseHeaders(const std::string &h, headersMap &hm) {
  std::string str(h);
  std::string::size_type idx;
  std::string t;
  while ((idx = str.find("\r\n")) != std::string::npos) {
    t = str.substr(0, idx);
    str.erase(0, idx + 2);
    if (t == "")
      break;
    idx = t.find(": ");
    if (idx == std::string::npos) {
      std::cout << "Bad header line: " << t << std::endl;
      break;
    }
    //std::cout << "Name: " << t.substr(0,idx)
    //		  << " Value: " << t.substr(idx+2) << std::endl;
    hm.insert(std::make_pair(t.substr(0, idx), t.substr(idx + 2)));
  }
}

void connection::update_throughput(const int32_t &size,
                                   const bc::steady_clock::time_point &timeStart,
                                   const std::string &ip) {
  bc::steady_clock::duration diff = bc::steady_clock::now() - timeStart;

  // calculate current throughput
  // fSize in bytes; diff.count in ns; throughput in kbps
  double tCur = size * (1e9 / (1 << 13)) / diff.count();

  boost::unique_lock<boost::shared_mutex> wlock(tm_mutex);
  auto iter = throughputMap.find(ip);
  if (iter != throughputMap.end()) {
    iter->second.first = update_alpha * tCur + (1 - update_alpha) * iter->second.first;
  } else {
    // Note: this is not supposed to happen
    std::cout << "Error: request a video chunk from " << ip
              << " without querying metafiles." << std::endl;
  }
}

void connection::check_video_requests(const std::string &uri) {
  boost::regex rVideoFile(R"((.*?)((([^/]*)\.f4m)|((\d+)Seg(\d+)-Frag(\d+))))");
  boost::smatch m;
  if (boost::regex_search(uri, m, rVideoFile, boost::match_extra)) {
    pathToVideo = m[1];
    if (!m[2].str().empty()) {
      if (!m[3].str().empty()) {
        isVideoMeta = true;
        metafileName = m[3].str();
        if (metafileName == "big_buck_bunny.f4m")
          isBigBuckBunny = true;
        else if (metafileName == "big_buck_bunny_nolist.f4m") {
          // just forward the big_buck_bunny back to server, clear flags
          isVideoMeta = false;
          isBigBuckBunny = false;
        }
      } else if (!m[5].str().empty()) {
        isVideoChunk = true;
        segNum = m[7].str();
        fragNum = m[8].str();
      }
    }
  }
}

std::string connection::adapt_bitrate(const std::string &ip, const std::string &path, const std::string &seg,
                                      const std::string &frag) {
  boost::shared_lock<boost::shared_mutex> rlock(tm_mutex);
  auto link = throughputMap.find(ip);
  if (link != throughputMap.end()) {
    double t = link->second.first;

    // empty bitrate list, this are not supposed to happen
    if (link->second.second.empty())
      shutdown();
    auto iter = std::upper_bound(link->second.second.begin(),
                                 link->second.second.end(), t * (2 / 3));

    // if all bitrate are too large, choose the lowest
    if (iter != link->second.second.begin())
      iter--;

    return path + std::to_string(*iter) + "Seg" + seg + "-Frag" + frag;
  } else {
    std::cout << "Error: no metadata recorded" << std::endl;
    shutdown();
  }
}

std::vector<int32_t> connection::get_bitrates(const std::string &xml) {
  pt::ptree pt;
  std::stringstream is;
  is << xml;
  pt::read_xml(is, pt);
  std::vector<int32_t> bitrates;
  BOOST_FOREACH(pt::ptree::value_type const &v, pt.get_child("manifest")) {
          if (v.first == "media") {
            std::string st = v.second.get<std::string>("<xmlattr>.bitrate");
            int32_t r = std::stoi(st);
            bitrates.push_back(r);
          }
        }
  std::sort(bitrates.begin(), bitrates.end());
  return bitrates;
}

boost::unordered_map<std::string, std::pair<double, std::vector<int32_t>>> connection::throughputMap;
boost::shared_mutex connection::tm_mutex;
double connection::update_alpha = 1.0;
std::string connection::reverse_name;
