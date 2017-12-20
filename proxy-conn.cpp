#include "proxy-conn.hpp"
using namespace std;
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
                //std::cout << "Bad first line: " << reqString << std::endl;
                return;
            }

            fMethod = reqString.substr(0, idx);
            reqString = reqString.substr(idx + 1);
            idx = reqString.find(" ");
            if (idx == std::string::npos) {
                //std::cout << "Bad first line of request: " << reqString << std::endl;
                return;
            }
            fURL = reqString.substr(0, idx);
            fReqVersion = reqString.substr(idx + 1);
            idx = fReqVersion.find("/");
            if (idx == std::string::npos) {
               // std::cout << "Bad first line of request: " << reqString << std::endl;
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
    std::string server;
    std::string port = "80";
    boost::regex rHTTP("http://(.*?)(:(\\d+))?(/.*)");
    boost::smatch m;

    if (boost::regex_search(fURL, m, rHTTP, boost::match_extra)) {
        server = m[1].str();
        if (m[2].str() != "") {
            port = m[3].str();
        }
        fNewURL = m[4].str();
        if (server == "video.pku.edu.cn") {
            if (wwwip.empty())
                server = query_name("\005video\003pku\003edu\002cn");
            else
                server = wwwip;
        }
    } else if (server.empty() && !reqHeaders["Host"].empty() && fMethod != "CONNECT") {
        std::cout << "Log: reverse proxy " << std::endl;
        //std::cout << fHeaders << std::endl;
        fNewURL = fURL;
        server = "video.pku.edu.cn";
        port = "8080";
        if (wwwip.empty()){
            server = query_name("\005video\003pku\003edu\002cn");
        }
		else{
            server = wwwip;
			std::cout << wwwip << std::endl;
		}
    } else {
        //std::cout << "Can't parse URL " << std::endl;
        //std::cout << fHeaders << std::endl;
        return;
    }
	std::cout << "fuck1!" << std::endl;
    // check and mark requests
    check_video_requests(fNewURL);
	std::cout << "fuck2!" << std::endl;
   	ssocket_.open(ba::ip::tcp::v4());
	std::cout << "fuck3!" << std::endl;
    std::shared_ptr<ba::ip::tcp::endpoint> local_ep = make_shared<ba::ip::tcp::endpoint>(fake_ip, 0);
    std::cout << ssocket_.local_endpoint().address().to_string() << std::endl;
    ssocket_.bind(*local_ep);
    std::cout << server << " " << port << " " << fNewURL << std::endl;

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
	std::cout << "handle_connect. Error: " << err << "\n";
    if (!err && !first_time) {
        isOpened = true;
        start_write_to_server();
    } else if (endpoint_iterator != ba::ip::tcp::resolver::iterator()) {
        //ssocket_.close();
        std::shared_ptr<ba::ip::tcp::endpoint> endpoint = make_shared<ba::ip::tcp::endpoint>(*endpoint_iterator);
        std::cout << "fuck endpoint " << endpoint->address().to_v4().to_string() << std::endl;
        ssocket_.async_connect(*endpoint,
                               boost::bind(&connection::handle_connect, shared_from_this(),
                                           boost::asio::placeholders::error,
                                           ++endpoint_iterator, false));
		cout<<"fuck is_open()"<<ssocket_.is_open()<<endl;
		//while(!ssocket_.is_open());
		//cout<<"fuck before sleep"<<endl;
		//sleep(1);
	std::cout << "fuck remote endpoint after async connect "<< ssocket_.remote_endpoint().address().to_v4().to_string() << std::endl;
    } else {
		cout<<"fuck shutdown by handle_connect"<<endl;
        shutdown();
    }
}

/** 
 * Write data to the web-server
 * 
 */
void connection::start_write_to_server() {

	std::cout << "fuck write!" << std::endl;

    // modify specific uri for video related requests
    if (isVideoChunk) {
		std::cout << "fuck choose_bitrate start!" << std::endl;
		std::cout << "fuck remote endpoint "<< ssocket_.remote_endpoint().address().to_v4().to_string() << std::endl;
        int32_t r = choose_bitrate(ssocket_.remote_endpoint().address().to_v4().to_string());
        //better than none
		std::cout << "fuck choose_bitrate done!" << std::endl;
        if (r == 0)
            fNewURL = pathToVideo + reqRate + "Seg" + segNum + "-Frag" + fragNum;
        else
            fNewURL = pathToVideo + std::to_string(r) + "Seg" + segNum + "-Frag" + fragNum;
    }
	if (isBigBuckBunny) {
		fNewURL = pathToVideo + "big_buck_bunny_nolist.f4m";
	}
    fReq = fMethod;
    fReq += " ";
    fReq += fNewURL;
    fReq += " HTTP/";
    fReq += "1.0";
//	fReq+=fReqVersion;
    fReq += "\r\n";
    fReq += fHeaders;
    //std::cout << "Request: " << fReq << std::endl;
    tStart = bc::system_clock::now();
	std::cout << "fuck clock now!" << std::endl;

    std::cout << "bind to content server" << std::endl;
    ba::async_write(ssocket_, ba::buffer(fReq),
                    boost::bind(&connection::handle_server_write, shared_from_this(),
                                ba::placeholders::error,
                                ba::placeholders::bytes_transferred));

	std::cout << "fuck after write!" << std::endl;
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
//std::cout << "handle_server_read_headers. Error: " << err << ", len=" << len << std::endl;
	std::cout << "fuck read headers!" << std::endl;
    if (!err) {
        std::string::size_type idx;
        if (fHeaders.empty())
            fHeaders = std::string(sbuffer.data(), len);
        else
            fHeaders += std::string(sbuffer.data(), len);
        idx = fHeaders.find("\r\n\r\n");
	std::cout << "fuck go to if!" << std::endl;
        if (idx == std::string::npos) { // going to read rest of headers
            async_read(ssocket_, ba::buffer(sbuffer), ba::transfer_at_least(1),
                       boost::bind(&connection::handle_server_read_headers,
                                   shared_from_this(),
                                   ba::placeholders::error,
                                   ba::placeholders::bytes_transferred));
        } else { // analyze headers
			//std::cout << "Response: " << fHeaders << std::endl;
	std::cout << "fuck analyze headers!" << std::endl;
	//std::cout << "fuck fheaders " << fHeaders << std::endl;
	//std::cout << "fuck URL "<< fNewURL << std::endl;

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
	std::cout << "fuck end header!" << std::endl;
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
	cout << "fuck write to browser" << endl;
    if (!err) {
        if (!proxy_closed && (RespLen == -1 || RespReaded < RespLen))
            async_read(ssocket_, ba::buffer(sbuffer, len), ba::transfer_at_least(1),
                       boost::bind(&connection::handle_server_read_body,
                                   shared_from_this(),
                                   ba::placeholders::error,
                                   ba::placeholders::bytes_transferred));
        else {
            // assert no flags collision
            assert(!(isVideoChunk && (isVideoMeta || isBigBuckBunny)));
            if (isVideoMeta) {
                // special case: parse metafiles
                // it seems only big_buck_bunny.f4m need parsing
            }
			if (isBigBuckBunny) {
            	// special case: query again with list
                fNewURL = pathToVideo + "big_buck_bunny.f4m";
				isBigBuckBunny = false;
				isVideoMeta = false;
                fReq = fMethod;
    			fReq += " ";
    			fReq += fNewURL;
    			fReq += " HTTP/";
    			fReq += "1.0";
    			fReq += "\r\n\r\n";
                std::cout << fReq << std::endl;
                proxy_closed = false;
    			ba::async_write(ssocket_, ba::buffer(fReq),
                				boost::bind(&connection::handle_bunny_write, shared_from_this(),
                                			ba::placeholders::error,
                                			ba::placeholders::bytes_transferred));
                proxy_closed = true;
                isVideoMeta = false;
                isBigBuckBunny = false;
                isVideoChunk = false;
                return;                 // jump out of loop to avoid premature shutdown
            }
            if (isVideoChunk) {
                auto server_ip = ssocket_.remote_endpoint().address().to_v4().to_string();
                update_throughput(RespReaded, tStart, server_ip);
            }

            // clear flags
            isVideoMeta = false;
            isBigBuckBunny = false;
            isVideoChunk = false;

			shutdown();
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
    cout << "fuck read body" << endl;
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
	std::cout << "fuck  ssocket closed " << std::endl;
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
            //std::cout << "Bad header line: " << t << std::endl;
            break;
        }
        //std::cout << "Name: " << t.substr(0,idx)
        //		  << " Value: " << t.substr(idx+2) << std::endl;
        hm.insert(std::make_pair(t.substr(0, idx), t.substr(idx + 2)));
    }
}

void connection::update_throughput(const int32_t &size,
                                   const bc::system_clock::time_point &timeStart,
                                   const std::string &ip) {
    auto timeFinish = bc::system_clock::now();
    bc::system_clock::duration diff = timeFinish - timeStart;

    double time = timeFinish.time_since_epoch().count();
    double duration = diff.count();

    // calculate current throughput
    // fSize in bytes; diff.count in ns; throughput in kbps
    double tCur = size * (1e9 / (double)(1 << 13)) / diff.count();
    double avg_tput;

    boost::unique_lock<boost::shared_mutex> wlock(tm_mutex);
    auto iter = throughputMap.find(ip);
    if (iter != throughputMap.end()) {
        avg_tput = iter->second = update_alpha * tCur + (1 - update_alpha) * iter->second;
    } else {
		avg_tput = throughputMap[ip] = tCur;
    }
    wlock.release();

    loggger << std::setprecision(10) << time / (double)1e9 << " "
            << duration / (double)1e9 << " "
            << tCur << " "
            << avg_tput << " "
            << chosen_rate << " "
            << ip << " "
            << fNewURL << std::endl;
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
                reqRate = m[6].str();
                segNum = m[7].str();
                fragNum = m[8].str();
            }
        }
    }
}

uint32_t connection::choose_bitrate(const std::string &ip) {
	std::cout << "fuck in choose_bitrate!" << std::endl;
    boost::shared_lock<boost::shared_mutex> rlock(tm_mutex);
    auto link = throughputMap.find(ip);
    if (link != throughputMap.end()) {
        double t = link->second;
		std::cout << "fuck not end choose_bitrate!" << std::endl;

        // empty bitrate list, this are not supposed to happen
        if (rates.empty()) {
			std::cout << "Warning: No metadata records" << std::endl;
			return 0;
		}
        auto iter = std::upper_bound(rates.begin(),
                                     rates.end(), t * (2 / 3));

        // if all bitrate are too large, choose the lowest
		std::cout << "fuck iter!" << std::endl;
        if (iter != rates.begin())
            iter--;
		std::cout << "fuck iter! end" << std::endl;

        chosen_rate = *iter;
		std::cout << "fuck out choose_bitrate!" << std::endl;

        return chosen_rate;
    } else {
        return 0;
    }
}

void connection::get_bitrates(stringstream& xml_s) {
    pt::ptree pt;
    pt::read_xml(xml_s, pt);
    std::vector<uint32_t> bitrates;
    BOOST_FOREACH(pt::ptree::value_type const &v, pt.get_child("manifest")) {
                    if (v.first == "media") {
                        uint32_t r = v.second.get<uint32_t>("<xmlattr>.bitrate");
                        bitrates.push_back(r);
                    }
                }
    std::sort(bitrates.begin(), bitrates.end());
    boost::unique_lock<boost::shared_mutex> wlock(tm_mutex);
	if (!bitrates.empty()) {
		rates.clear();
		rates = bitrates;
	}
}

std::string connection::query_name(const std::string &qname) {
	//std::cout<< "fuck qname = "<<qname<<std::endl;
    //boost::array<char, 128> dns_buffer;
    char dns_buffer[128];
    auto header = reinterpret_cast<DNS_HEADER *>(dns_buffer);

    header->qr = 0;
    header->opcode = 0;
    header->aa = 0;
    header->tc = 0;
    header->rd = 0;
    header->ra = 0;
    header->z = 0;
    header->ad = 0;
    header->cd = 0;
    header->rcode = 0;
    header->q_count = 1;
    header->ans_count = 0;
    header->auth_count = 0;
    header->add_count = 0;

    auto qname_field = (char *) (header + 1);
    std::strcpy(qname_field, qname.c_str());
    size_t name_len = qname.length() + 1;
    auto question = reinterpret_cast<QUESTION *>(qname_field + name_len);
    question->qtype = T_A;
    question->qclass = 1;

    size_t query_len = sizeof(DNS_HEADER) + name_len + sizeof(QUESTION);

    ba::ip::udp::socket dns_socket_(io_service_);
    dns_socket_.open(ba::ip::udp::v4());
    ba::ip::udp::endpoint dns_remote_(dns_ip, dns_port);
    dns_socket_.send_to(ba::buffer(dns_buffer, query_len), dns_remote_);

    boost::array<char, 128> dns_resp_buffer;
    ba::ip::udp::endpoint dns_receive_;
    size_t resp_len = dns_socket_.receive_from(
            ba::buffer(dns_resp_buffer), dns_receive_);
    //std::cout.write(dns_resp_buffer.data(), resp_len);
    uint32_t *bias = reinterpret_cast<uint32_t *>(dns_resp_buffer.c_array() + 46);
    //std::cout << *bias << std::endl;
    ba::ip::address_v4 res(htonl(*bias));
    //std::cout << res.to_string() << std::endl;
    return res.to_string();
}

void connection::handle_bunny_write(const bs::error_code &err, size_t len) {
	cout << "fuck bunny" << endl;
    if (!err) {
        handle_bunny_read(bs::error_code(), 0);
    }
}

void connection::handle_bunny_read(const bs::error_code &err, size_t len) {
 	cout << "fuck bunny body" << endl;
	if (!err || err == ba::error::eof) {
        if (buck_buf.empty()) {
            buck_buf = std::string(sbuffer.data(), len);
        } else {
            buck_buf += std::string(sbuffer.data(), len);
        }
        if (err == ba::error::eof) {
            proxy_closed = true;
            shutdown();
            std::string::size_type idx = buck_buf.find("\r\n\r\n");
            if (idx == std::string::npos) {
                std::cout << "Warning: fetch no content of metafile" << std::endl;
                return;
            }
            std::stringstream xml_s(buck_buf.substr(idx+4));
            get_bitrates(xml_s);
        } else {
            async_read(ssocket_, ba::buffer(sbuffer), ba::transfer_at_least(1),
                       boost::bind(&connection::handle_bunny_read,
                                   shared_from_this(),
                                   ba::placeholders::error,
                                   ba::placeholders::bytes_transferred));
        }
	} else {
        return;
	}
}


