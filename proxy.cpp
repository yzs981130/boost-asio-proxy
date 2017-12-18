/**
 * @file   proxy.cpp
 * @author Alex Ott <alexott@gmail.com>
 * 
 * @brief  
 * 
 * 
 */

#include "proxy-server.hpp"
boost::unordered_map<std::string, std::pair<double, std::vector<int32_t>>> connection::throughputMap;
boost::shared_mutex connection::tm_mutex;
double connection::update_alpha = 1.0;
std::string connection::wwwip;
ba::ip::address_v4 connection::dns_ip = ba::ip::address_v4::from_string("127.0.0.1");
unsigned short connection::dns_port = 10053;

int main(int argc, char **argv) {
    try {
        int thread_num = 2, port = 10001;
        std::string interface_address;

        //TODO: reset command-line arguments
        if (argc != 7 && argc != 8) {
            std::cerr << "Usage: " << argv[0]
                      << " <log> <alpha> <listen-port> <fake-ip> <dns-ip> <dns-port> [<www-ip>]."
                      << std::endl;
            exit(1);
        }
        // read thread numbers form command line, if provided
        if (argc > 1)
            thread_num = boost::lexical_cast<int>(argv[1]);

        // read port number from command line, if provided
        if (argc > 2)
            port = boost::lexical_cast<int>(argv[2]);

        // read local interface address from command line, if provided
        if (argc > 3)
            interface_address = argv[3];

        ios_deque io_services;
        std::deque<ba::io_service::work> io_service_work;

        boost::thread_group thr_grp;

        for (int i = 0; i < thread_num; ++i) {
            io_service_ptr ios(new ba::io_service);
            io_services.push_back(ios);
            io_service_work.push_back(ba::io_service::work(*ios));
            thr_grp.create_thread(boost::bind(&ba::io_service::run, ios));
        }
        server server(io_services, port, interface_address);
        thr_grp.join_all();
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }


    return 0;
}

