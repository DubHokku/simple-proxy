#include <iostream>
#include <list>
#include <iterator>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define FORWARD_SERVERS 2

class recipient_t
{
    public:
    recipient_t();
    ~recipient_t();
    
    int start();
    void receive();
    void stop( int );
    
    private:
    struct session_t
    {
        int client_socket;
        int web_socket;
        int transfer_socket;
        
        uint32_t data_size;
                
        uint16_t client_port;
        uint16_t server_port;
        
        uint64_t sent_data;
        uint64_t recv_data;
        
        bool synset;
        bool request;
        
        struct sockaddr_in syn_socks;
        struct in_addr client_addr;
        struct in_addr server_addr;
        char* remote_name;
        char* data;
        // char data[4344];
    };
    
    short port;
    int proxy_socket;
    char socks4accept[8] = { 0, 90, 0, 0, 0, 0, 0, 0 };
    session_t *session_data;
    
    int forwad_socket[FORWARD_SERVERS];
    int init_forward();
    void notify( const char*, int );
    
    std::list < session_t > sessions;
    std::list < session_t > :: iterator session;
    std::list < session_t > :: iterator tmp_link;
};
