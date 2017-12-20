#ifndef _PROXY_CONN_H
#define _PROXY_CONN_H 1

#include "common.h"
#include <boost/unordered_map.hpp>
#include "dns_packet.h"
#include <iomanip>
class connection : public boost::enable_shared_from_this<connection> {
public:
    typedef boost::shared_ptr<connection> pointer;

    static pointer create(ba::io_service &io_service) {
        return pointer(new connection(io_service));
    }

    ba::ip::tcp::socket &socket() {
        return bsocket_;
    }

    /// Start read data of request from browser
    void start();

private:
    connection(ba::io_service &io_service);

    /// Read header of HTTP request from browser
    void handle_browser_read_headers(const bs::error_code &err, size_t len);

    /// Start connecting to the web-server, initially to resolve the DNS-name of Web server into the IP address
    void start_connect();

    std::string query_name(const std::string &qname);

    void handle_resolve(const boost::system::error_code &err,
                        ba::ip::tcp::resolver::iterator endpoint_iterator);

    void handle_connect(const boost::system::error_code &err,
                        ba::ip::tcp::resolver::iterator endpoint_iterator, const bool first_time);

    /// Write data to the web-server
    void start_write_to_server();

    void handle_server_write(const bs::error_code &err, size_t len);

    /// Read header of data returned from the web-server
    void handle_server_read_headers(const bs::error_code &err, size_t len);

    /// Reading data from a Web server, and writing it to the browser
    void handle_browser_write(const bs::error_code &err, size_t len);

    void handle_server_read_body(const bs::error_code &err, size_t len);

	/// special case for bigbunny
    void handle_bunny_write(const bs::error_code &err, size_t len);
    void handle_bunny_read(const bs::error_code &err, size_t len);

    /// Close both sockets: for browser and web-server
    void shutdown();

    ba::io_service &io_service_;
    ba::ip::tcp::socket bsocket_;
    ba::ip::tcp::socket ssocket_;

    ba::ip::tcp::resolver resolver_;
    bool proxy_closed;
    bool isPersistent;
    int32_t RespLen;
    int32_t RespReaded;

    boost::array<char, 20000> bbuffer;
    boost::array<char, 20000> sbuffer;

    std::string fURL;
    std::string fHeaders;
    std::string fNewURL;
    std::string fMethod;
    std::string fReqVersion;
    std::string fServer;
    std::string fPort;
    bool isOpened;

    std::string fReq;

    // check if this request is about videos
    void check_video_requests(const std::string &uri);

    bool isVideoMeta;
    std::string pathToVideo;
    std::string metafileName;
    bool isBigBuckBunny;
    std::string buck_buf;

    void get_bitrates(std::stringstream& xml_s);

    bool isVideoChunk;
    uint32_t chosen_rate;
    std::string reqRate;
    std::string segNum;
    std::string fragNum;

    uint32_t choose_bitrate(const std::string &ip);

    typedef boost::unordered_map<std::string, std::string> headersMap;
    headersMap reqHeaders, respHeaders;

    void parseHeaders(const std::string &h, headersMap &hm);

    /// Record / Update throughput from proxy to servers
    void update_throughput(const int32_t &size,
                           const bc::system_clock::time_point &timeStart,
                           const std::string &ip);

    bc::system_clock::time_point tStart;

    static boost::unordered_map<std::string, double> throughputMap;
    static boost::shared_mutex tm_mutex;
	static std::vector<uint32_t> rates;

public:
    static double update_alpha;
    static std::string wwwip;
    static ba::ip::address_v4 dns_ip;
    static unsigned short dns_port;
    static ba::ip::address_v4 fake_ip;
};

#endif /* _PROXY-CONN_H */

