#include "recipient.h"

#define SOCKS_V4    4
#define CONNECT     1

recipient_t::recipient_t()
{
    port = 127;
    session_data = new session_t;
}
recipient_t::~recipient_t()
{
    delete session_data;
}

void recipient_t::receive()
{
    int result, max_set, transfer_socket;
    proxy_socket = start();

    char data[4344]; // ~ mss x 3    
    struct sockaddr reply;
    socklen_t sockaddr_size = sizeof( struct sockaddr );
    
    while( true )
    {        
        fd_set read_set, write_set;
        FD_ZERO( &read_set );
        FD_ZERO( &write_set );
        FD_SET( proxy_socket, &read_set );
        max_set = proxy_socket;
        
        for( session = sessions.begin(); session != sessions.end(); session++ )
        {
            if( session->data_size > 0 )
            {
                FD_SET( session->client_socket, &write_set );
                FD_SET( session->web_socket, &write_set );
            }
            if( session->data_size == 0 )
            {
                FD_SET( session->client_socket, &read_set );
                FD_SET( session->web_socket, &read_set );
            }
            
            max_set = session->client_socket > max_set?session->client_socket:max_set;
            max_set = session->web_socket > max_set?session->web_socket:max_set;
        }

        // use epoll()
        if(( select( max_set + 1, &read_set, &write_set, NULL, NULL )) < 0 )
        {
            if( errno == EINTR )
                continue;
            notify( "select()", errno );
        }

        for( session = sessions.begin(); session != sessions.end(); session++ )
        {
            if( FD_ISSET( session->client_socket, &write_set ) && session->request )
                memcpy( &session->transfer_socket, &session->web_socket, sizeof( int ));
            if( FD_ISSET( session->web_socket, &write_set ) && !session->request )
                memcpy( &session->transfer_socket, &session->client_socket, sizeof( int ));
            
            if( FD_ISSET( session->client_socket, &read_set ) && session->request )
                memcpy( &session->transfer_socket, &session->client_socket, sizeof( int ));
            if( FD_ISSET( session->web_socket, &read_set ) && !session->request )
                memcpy( &session->transfer_socket, &session->web_socket, sizeof( int ));

            if( FD_ISSET( session->transfer_socket, &write_set ))
            {
                int snd = send( session->transfer_socket, session->data, session->data_size, 0 );
                if( snd < 0 )
                {
                    if(( errno != EINTR ) && ( errno != EAGAIN ) && ( errno != EWOULDBLOCK ))
                        notify( "\nsend()", errno );
                }
                else
                {   // std::cout << "transfer " << snd << " from " << session->data_size << " byte" << std::endl;
                    if( session->data_size == snd )
                    {
                        delete[] session->data;
                        session->data = NULL;
                    }

                    session->data_size = session->data_size - snd;
                }        
                continue;
            }
            if( FD_ISSET( session->web_socket, &read_set ) || FD_ISSET( session->client_socket, &read_set ))
            {
                result = recv( session->transfer_socket, data, sizeof( data ), 0 );
                if( result < 0 )
                {
                    if(( errno != EINTR ) && ( errno != EAGAIN ) && ( errno != EWOULDBLOCK ))
                    {
                        std::cout << "error on recv() " << strerror( errno ) << std::endl;
                        
                        if( session->remote_name != NULL )
                            delete[] session->remote_name;

                        tmp_link = session;
                        session++;
                        shutdown( tmp_link->web_socket, 2 );
                        shutdown( tmp_link->client_socket, 2 );
                        
                        sessions.erase( tmp_link );
                        break;
                    }
                }
                else
                {
                    if( result == 0 )
                    {
                        std::cout << "End Of Data, session " << session->web_socket << " close" << std::endl;
                        
                        if( session->remote_name != NULL )
                            delete[] session->remote_name;

                        tmp_link = session;
                        session++;
                        shutdown( tmp_link->web_socket, 2 );
                        shutdown( tmp_link->client_socket, 2 );
                        
                        sessions.erase( tmp_link );                        
                        break;
                    }
                    else
                    {   
                        if( session->synset )
                        {    
                            session->data_size = result;
                            session->data = new char[session->data_size];
                            memcpy( session->data, data, session->data_size );

                            if( session->transfer_socket == session->client_socket )
                                session->request = true;
                            if( session->transfer_socket == session->web_socket )
                                session->request = false;
                        } 
                        else
                        {   // std::cout << "make syn_set \n";
                            struct sockaddr_in* sync_socks = ( struct sockaddr_in* )data;
                            unsigned char* sin_zero = sync_socks->sin_zero;
            /* check socks version  */
                            char command[2];
                            memcpy( command, &sync_socks->sin_family, sizeof( command ));
                            // std::cout << "use socks version " << ( int )command[0] << std::endl;
                            // std::cout << "command " << ( int )command[1] << std::endl;
            /* check valid addess   */
                            uint32_t addr_socks;
                            memcpy( &addr_socks, &sync_socks->sin_addr, sizeof( uint32_t ));
                            
                            if(( command[0] == SOCKS_V4 ) && ( command[1] == CONNECT ))
                            {
                                if(( addr_socks = ntohl( addr_socks )) < 256 )
                                {
                                    // std::cout << "use socks_v4a \n";
            /*   name resolve    *
             * SOCKS4a extends the SOCKS4 protocol to allow a client to specify a destination domain name rather than
             * an IP address;[15] this is useful when the client itself cannot resolve the destination host's domain 
             * name to an IP address.
             * The client should set the first three bytes of DSTIP to NULL and the last byte to a non-zero value. 
             * ( This corresponds to IP address 0.0.0.x, with x nonzero, an inadmissible destination address
             * and thus should never occur if the client can resolve the domain name.)
             * Following the NULL byte terminating USERID, the client must send the destination domain name and terminate 
             * it with another NULL byte. This is used for both "connect" and "bind" requests. 
             * 
             * https://en.wikipedia.org/wiki/SOCKS#SOCKS4   */
            
                                    uint16_t name_size = strlen(( const char* )sync_socks->sin_zero + 1 ) + 1;
                                    session->remote_name = new char[name_size];                                    
                                    
                                    memcpy( session->remote_name, sync_socks->sin_zero + 1, name_size );
                                    session->remote_name[name_size] = 0;
                                    session->server_port = ntohs( sync_socks->sin_port );
                                    
                                    std::cout << "request " << session->remote_name << ":" << session->server_port << std::endl;
                                    struct hostent *he;
                                    he = gethostbyname( session->remote_name );
                                    if( !he )
                                    {
                                        std::cout << strerror( h_errno ) << std::endl;
                                        std::cout << session->remote_name << " cannot retrieve address" << std::endl << std::endl;
                                        
                                        // send socks response deny;
                                        
                                        if( session->remote_name != NULL )
                                            delete[] session->remote_name;
                                        tmp_link = session;
                                        session++;
                                        shutdown( tmp_link->client_socket, 1 );
                                        
                                        sessions.erase( tmp_link );
                                        break;
                                    }
                                    else
                                    {
                                        session->server_addr = *( struct in_addr* )*he->h_addr_list;
                                        std::cout << "retrieve addess " << inet_ntoa( session->server_addr ) << std::endl << std::endl;
                                    }
                                }
                                else
                                {   // std::cout << "use socks_v4 \n";
                                    session->server_addr = sync_socks->sin_addr;
                                    session->server_port = ntohs( sync_socks->sin_port );
                                    std::cout << "request " << inet_ntoa( session->server_addr ) << ":" << session->server_port << std::endl << std::endl;
                                }
                            }
                            else
                            {
                                std::cout << "try connect with no socks_v4 or another command \n\n";
                                
                                // send socks response deny;
                                
                                tmp_link = session;
                                session++;
                                shutdown( tmp_link->client_socket, 1 );
                                
                                sessions.erase( tmp_link );
                                break;
                            }
            /*    make 'websocket'  */
                            int enable = 1;
                            int web_socket = socket( AF_INET, SOCK_STREAM, 0 );
                            if( web_socket < 0 )
                                notify( "soket()", errno );

                            struct sockaddr_in server_sockaddr;
                            server_sockaddr.sin_addr.s_addr = session->server_addr.s_addr;
                            server_sockaddr.sin_family = AF_INET;
                            server_sockaddr.sin_port = htons( session->server_port );

                            if( connect( web_socket, ( struct sockaddr* )&server_sockaddr, sizeof( server_sockaddr )) < 0 )
                                notify( "connect()", errno );
                            setsockopt( web_socket, SOL_SOCKET, O_NONBLOCK, &enable, sizeof( int ));
                            
                            session->web_socket = web_socket;
                            session->request = false;
                            session->synset = true;

            /*    accept socks connection */           
                            if( send( session->client_socket, socks4accept, sizeof( socks4accept ), 0 ) < 0 )
                                notify( "send()", errno );
                        }
                    }
                }    
            }
        }
        
        if( FD_ISSET( proxy_socket, &read_set ))
        {   // std::cout << "make accept \n";
            while( true )
            {
                session_data->client_socket = accept( proxy_socket, &reply, &sockaddr_size );
                if( session_data->client_socket < 0 )
                {   
                    if( errno == EAGAIN || errno == EWOULDBLOCK )
                        break;
                    notify( "accept()", errno );
                }
                else
                {
                    if( fcntl( session_data->client_socket, F_SETFL, O_NONBLOCK ) < 0 )
                        notify( "fcntl()", errno );
                    
                    session_data->data = NULL;
                    session_data->remote_name = NULL;
                    session_data->data_size = 0;
                    session_data->synset = false;
                    session_data->request = true;
                    
                    struct sockaddr_in *reply_in = ( struct sockaddr_in* )&reply;
                    session->client_addr = reply_in->sin_addr;
                    session_data->client_port = htons( reply_in->sin_port );
                    
                    sessions.push_back( *session_data );
                    std::cout << "accept new session, from " << inet_ntoa( session->client_addr ) << ":" << session_data->client_port << std::endl;
                }
            }
        }
    }
}

void recipient_t::stop( int code )
{
    
}

int recipient_t::start()
{
    int listen_socket, enable = 1;
    struct sockaddr_in listen_sockaddr;

    if(( listen_socket = socket( PF_INET, SOCK_STREAM, 0 )) < 0 )
        notify( "socket()", errno );
    
    setsockopt( listen_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof( int ));

    memset( &listen_sockaddr, 0, sizeof( listen_sockaddr ));
    listen_sockaddr.sin_family = PF_INET;
    listen_sockaddr.sin_port = htons( port );
    // listen_sockaddr.sin_addr.s_addr = INADDR_LOOPBACK;
    listen_sockaddr.sin_addr.s_addr = inet_addr( "127.0.0.1" );

    if( bind( listen_socket, ( struct sockaddr* )&listen_sockaddr, sizeof( listen_sockaddr )) < 0 )
        notify( "bind()", errno );
    if( listen( listen_socket, 5 ) < 0 )
        notify( "listen()", errno );
    if( fcntl( listen_socket, F_SETFL, O_NONBLOCK ) < 0 )
        notify( "fcntl()", errno );

    return listen_socket;
}

void recipient_t::notify( const char *func, int code )
{
    std::cout << func << ": " << strerror( code ) << " code " << code << std::endl;
    // exit( 0 );
}
